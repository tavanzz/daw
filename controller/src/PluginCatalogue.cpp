#include "daw/controller/PluginCatalogue.h"

#include <algorithm>

namespace daw::controller
{

namespace
{

constexpr int catalogueVersion = 1;

// A file item's modification time (ms), 0 for identifiers that are not files (AU,
// LV2 URIs) or files that are gone.
juce::int64 modificationTimeOf(const juce::String& item)
{
    if (!juce::File::isAbsolutePath(item))
        return 0;
    const juce::File file(item);
    return file.exists() ? file.getLastModificationTime().toMilliseconds() : 0;
}

bool comesBefore(const PluginInfo& a, const PluginInfo& b)
{
    const auto& x = a.description;
    const auto& y = b.description;
    if (const int byName = x.name.compareNatural(y.name); byName != 0)
        return byName < 0;
    if (const int byMaker = x.manufacturerName.compareIgnoreCase(y.manufacturerName); byMaker != 0)
        return byMaker < 0;
    return x.pluginFormatName.compareIgnoreCase(y.pluginFormatName) < 0;
}

bool matchesWords(const juce::PluginDescription& description, const juce::String& text)
{
    juce::StringArray words;
    words.addTokens(text, " \t", "\"");
    words.removeEmptyStrings();
    for (const auto& word : words)
    {
        if (!description.name.containsIgnoreCase(word) &&
            !description.descriptiveName.containsIgnoreCase(word) &&
            !description.manufacturerName.containsIgnoreCase(word) &&
            !description.category.containsIgnoreCase(word) &&
            !description.pluginFormatName.containsIgnoreCase(word))
            return false;
    }
    return true;
}

juce::var stringsToJson(const juce::StringArray& strings)
{
    juce::Array<juce::var> array;
    for (const auto& string : strings)
        array.add(string);
    return array;
}

juce::StringArray stringsFromJson(const juce::var& json)
{
    juce::StringArray strings;
    if (const auto* array = json.getArray())
        for (const auto& value : *array)
            if (value.isString())
                strings.add(value.toString());
    return strings;
}

} // namespace

PluginKey PluginKey::of(const juce::PluginDescription& description)
{
    return {description.pluginFormatName, description.uniqueId};
}

juce::String PluginKey::toString() const
{
    return format + ":" + juce::String(uniqueId);
}

std::optional<PluginKey> PluginKey::fromString(const juce::String& text)
{
    const int colon = text.lastIndexOfChar(':');
    if (colon <= 0 || colon == text.length() - 1)
        return std::nullopt;
    const auto number = text.substring(colon + 1);
    if (!number.containsOnly("-0123456789"))
        return std::nullopt;
    return PluginKey{text.substring(0, colon), number.getIntValue()};
}

juce::String toString(ScanFailure failure)
{
    switch (failure)
    {
    case ScanFailure::crashed:
        return "crashed";
    case ScanFailure::timedOut:
        return "timedOut";
    case ScanFailure::noPlugins:
        return "noPlugins";
    case ScanFailure::failedToLoad:
        return "failedToLoad";
    }
    return "crashed";
}

std::optional<ScanFailure> scanFailureFromString(const juce::String& text)
{
    for (const auto failure : {ScanFailure::crashed, ScanFailure::timedOut, ScanFailure::noPlugins,
                               ScanFailure::failedToLoad})
        if (toString(failure) == text)
            return failure;
    return std::nullopt;
}

juce::StringArray PluginCatalogue::hostedFormats()
{
    juce::StringArray formats{"VST3"};
#if JUCE_PLUGINHOST_AU
    formats.add("AudioUnit");
#endif
#if JUCE_PLUGINHOST_LV2
    formats.add("LV2");
#endif
    return formats;
}

juce::FileSearchPath PluginCatalogue::defaultFolders(juce::AudioPluginFormat& format)
{
    return format.getDefaultLocationsToSearch();
}

void PluginCatalogue::setCustomFolders(const juce::String& format, const juce::StringArray& folders)
{
    auto cleaned = folders;
    cleaned.trim();
    cleaned.removeEmptyStrings();
    cleaned.removeDuplicates(false);
    if (customFolders[format] != cleaned)
    {
        customFolders[format] = cleaned;
        changed = true;
    }
}

juce::StringArray PluginCatalogue::getCustomFolders(const juce::String& format) const
{
    const auto found = customFolders.find(format);
    return found != customFolders.end() ? found->second : juce::StringArray{};
}

juce::FileSearchPath PluginCatalogue::foldersFor(juce::AudioPluginFormat& format) const
{
    auto folders = defaultFolders(format);
    for (const auto& folder : getCustomFolders(format.getName()))
        folders.addIfNotAlreadyThere(juce::File(folder));
    return folders;
}

std::vector<ScanItem> PluginCatalogue::planScan(juce::AudioPluginFormatManager& formats,
                                                bool everything)
{
    std::vector<ScanItem> plan;
    std::set<ScanItem> present;
    const auto hosted = hostedFormats();

    for (auto* format : formats.getFormats())
    {
        if (!hosted.contains(format->getName()))
            continue;

        // Asynchronously instantiated plugins (AUv3) too: the scanner waits for them.
        for (const auto& name : format->searchPathsForPlugins(foldersFor(*format), true, true))
        {
            const ScanItem item{format->getName(), name};
            if (!present.insert(item).second)
                continue;

            if (const auto failed = blocked.find(item); failed != blocked.end())
            {
                // A file that changed since it failed gets another chance.
                const auto modified = modificationTimeOf(name);
                if (modified == 0 || modified == failed->second.modified)
                    continue;
                blocked.erase(failed);
                changed = true;
            }

            const auto known = scanned.find(item);
            const bool outdated =
                known != scanned.end() &&
                std::any_of(known->second.begin(), known->second.end(), [&](const PluginInfo& info) {
                    return format->pluginNeedsRescanning(info.description);
                });
            if (known == scanned.end() || everything || outdated)
                plan.push_back(item);
        }
    }

    // What is no longer there is forgotten (also from the blacklist).
    for (auto it = scanned.begin(); it != scanned.end();)
    {
        if (hosted.contains(it->first.format) && !present.contains(it->first))
        {
            it = scanned.erase(it);
            changed = true;
        }
        else
        {
            ++it;
        }
    }
    for (auto it = blocked.begin(); it != blocked.end();)
    {
        if (hosted.contains(it->first.format) && !present.contains(it->first))
        {
            it = blocked.erase(it);
            changed = true;
        }
        else
        {
            ++it;
        }
    }
    return plan;
}

void PluginCatalogue::addScanned(const ScanItem& item, std::vector<PluginInfo> plugins)
{
    blocked.erase(item);
    scanned[item] = std::move(plugins);
    changed = true;
}

void PluginCatalogue::block(const ScanItem& item, ScanFailure failure, const juce::String& detail)
{
    scanned.erase(item);
    blocked[item] = {item, failure, detail, modificationTimeOf(item.item)};
    changed = true;
}

void PluginCatalogue::unblock(const ScanItem& item)
{
    if (blocked.erase(item) > 0)
        changed = true;
}

std::vector<PluginCatalogue::Blocked> PluginCatalogue::getBlocked() const
{
    std::vector<Blocked> list;
    for (const auto& [item, entry] : blocked)
        list.push_back(entry);
    return list;
}

bool PluginCatalogue::isBlocked(const ScanItem& item) const
{
    return blocked.contains(item);
}

std::vector<PluginInfo> PluginCatalogue::getPlugins() const
{
    std::vector<PluginInfo> plugins;
    for (const auto& [item, infos] : scanned)
        plugins.insert(plugins.end(), infos.begin(), infos.end());
    std::sort(plugins.begin(), plugins.end(), comesBefore);
    return plugins;
}

std::vector<PluginInfo> PluginCatalogue::search(const PluginQuery& query) const
{
    std::vector<PluginInfo> found;
    for (auto& info : getPlugins())
    {
        const auto& description = info.description;
        if (!info.isSupported())
            continue;
        if (!query.formats.empty() && !query.formats.contains(description.pluginFormatName))
            continue;
        if ((query.kind == PluginQuery::Kind::effects && description.isInstrument) ||
            (query.kind == PluginQuery::Kind::instruments && !description.isInstrument))
            continue;
        if (query.manufacturer.isNotEmpty() &&
            !description.manufacturerName.equalsIgnoreCase(query.manufacturer))
            continue;
        if (query.favouritesOnly && !isFavourite(PluginKey::of(description)))
            continue;
        if (!matchesWords(description, query.text))
            continue;
        found.push_back(std::move(info));
    }
    return found;
}

juce::StringArray PluginCatalogue::getManufacturers() const
{
    juce::StringArray manufacturers;
    for (const auto& info : getPlugins())
        if (info.isSupported() && info.description.manufacturerName.isNotEmpty())
            manufacturers.addIfNotAlreadyThere(info.description.manufacturerName, true);
    manufacturers.sortNatural();
    return manufacturers;
}

std::optional<PluginInfo> PluginCatalogue::find(const PluginKey& key) const
{
    for (const auto& [item, infos] : scanned)
        for (const auto& info : infos)
            if (PluginKey::of(info.description) == key)
                return info;
    return std::nullopt;
}

void PluginCatalogue::setFavourite(const PluginKey& key, bool favourite)
{
    const bool was = favourites.contains(key);
    if (favourite == was)
        return;
    if (favourite)
        favourites.insert(key);
    else
        favourites.erase(key);
    changed = true;
}

bool PluginCatalogue::isFavourite(const PluginKey& key) const
{
    return favourites.contains(key);
}

juce::var descriptionToJson(const juce::PluginDescription& description)
{
    auto* object = new juce::DynamicObject();
    object->setProperty("name", description.name);
    object->setProperty("descriptiveName", description.descriptiveName);
    object->setProperty("format", description.pluginFormatName);
    object->setProperty("category", description.category);
    object->setProperty("manufacturer", description.manufacturerName);
    object->setProperty("version", description.version);
    object->setProperty("fileOrIdentifier", description.fileOrIdentifier);
    object->setProperty("lastFileModTime", description.lastFileModTime.toMilliseconds());
    object->setProperty("lastInfoUpdateTime", description.lastInfoUpdateTime.toMilliseconds());
    object->setProperty("uniqueId", description.uniqueId);
    object->setProperty("deprecatedUid", description.deprecatedUid);
    object->setProperty("isInstrument", description.isInstrument);
    object->setProperty("numInputChannels", description.numInputChannels);
    object->setProperty("numOutputChannels", description.numOutputChannels);
    object->setProperty("hasSharedContainer", description.hasSharedContainer);
    return juce::var(object);
}

std::optional<juce::PluginDescription> descriptionFromJson(const juce::var& json)
{
    if (!json.isObject() || !json["format"].isString() || !json["fileOrIdentifier"].isString())
        return std::nullopt;
    juce::PluginDescription description;
    description.name = json["name"].toString();
    description.descriptiveName = json["descriptiveName"].toString();
    description.pluginFormatName = json["format"].toString();
    description.category = json["category"].toString();
    description.manufacturerName = json["manufacturer"].toString();
    description.version = json["version"].toString();
    description.fileOrIdentifier = json["fileOrIdentifier"].toString();
    description.lastFileModTime = juce::Time(static_cast<juce::int64>(json["lastFileModTime"]));
    description.lastInfoUpdateTime = juce::Time(static_cast<juce::int64>(json["lastInfoUpdateTime"]));
    description.uniqueId = static_cast<int>(json["uniqueId"]);
    description.deprecatedUid = static_cast<int>(json["deprecatedUid"]);
    description.isInstrument = static_cast<bool>(json["isInstrument"]);
    description.numInputChannels = static_cast<int>(json["numInputChannels"]);
    description.numOutputChannels = static_cast<int>(json["numOutputChannels"]);
    description.hasSharedContainer = static_cast<bool>(json["hasSharedContainer"]);
    if (description.name.isEmpty() || description.fileOrIdentifier.isEmpty())
        return std::nullopt;
    return description;
}

juce::var PluginCatalogue::toJson() const
{
    auto* root = new juce::DynamicObject();
    root->setProperty("version", catalogueVersion);

    juce::Array<juce::var> scannedJson;
    for (const auto& [item, infos] : scanned)
    {
        auto* entry = new juce::DynamicObject();
        entry->setProperty("format", item.format);
        entry->setProperty("item", item.item);
        juce::Array<juce::var> plugins;
        for (const auto& info : infos)
        {
            auto plugin = descriptionToJson(info.description);
            if (info.unsupported.isNotEmpty())
                plugin.getDynamicObject()->setProperty("unsupported", info.unsupported);
            plugins.add(plugin);
        }
        entry->setProperty("plugins", plugins);
        scannedJson.add(juce::var(entry));
    }
    root->setProperty("scanned", scannedJson);

    juce::Array<juce::var> blockedJson;
    for (const auto& [item, entry] : blocked)
    {
        auto* object = new juce::DynamicObject();
        object->setProperty("format", item.format);
        object->setProperty("item", item.item);
        object->setProperty("failure", toString(entry.failure));
        object->setProperty("detail", entry.detail);
        object->setProperty("modified", entry.modified);
        blockedJson.add(juce::var(object));
    }
    root->setProperty("blocked", blockedJson);

    auto* folders = new juce::DynamicObject();
    for (const auto& [format, list] : customFolders)
        if (!list.isEmpty())
            folders->setProperty(format, stringsToJson(list));
    root->setProperty("folders", juce::var(folders));

    juce::StringArray favouriteKeys;
    for (const auto& key : favourites)
        favouriteKeys.add(key.toString());
    root->setProperty("favourites", stringsToJson(favouriteKeys));
    return juce::var(root);
}

PluginCatalogue PluginCatalogue::fromJson(const juce::var& json)
{
    PluginCatalogue catalogue;
    if (!json.isObject())
        return catalogue;

    // A catalogue written by a newer version of the app may describe plugins in ways this
    // one cannot read: its scan results (a cache) are left out and the plugins scanned
    // again; the user's own choices (folders, favourites) are kept.
    const bool newer = static_cast<int>(json.getProperty("version", catalogueVersion)) > catalogueVersion;

    if (const auto* entries = newer ? nullptr : json["scanned"].getArray())
        for (const auto& entry : *entries)
        {
            const ScanItem item{entry["format"].toString(), entry["item"].toString()};
            if (item.format.isEmpty() || item.item.isEmpty())
                continue;
            std::vector<PluginInfo> infos;
            if (const auto* plugins = entry["plugins"].getArray())
                for (const auto& plugin : *plugins)
                    if (auto description = descriptionFromJson(plugin))
                        infos.push_back({std::move(*description), plugin["unsupported"].toString()});
            catalogue.scanned[item] = std::move(infos);
        }

    if (const auto* entries = newer ? nullptr : json["blocked"].getArray())
        for (const auto& entry : *entries)
        {
            const ScanItem item{entry["format"].toString(), entry["item"].toString()};
            const auto failure = scanFailureFromString(entry["failure"].toString());
            if (item.format.isEmpty() || item.item.isEmpty() || !failure)
                continue;
            catalogue.blocked[item] = {item, *failure, entry["detail"].toString(),
                                       static_cast<juce::int64>(entry["modified"])};
        }

    if (const auto* folders = json["folders"].getDynamicObject())
        for (const auto& property : folders->getProperties())
            catalogue.customFolders[property.name.toString()] = stringsFromJson(property.value);

    for (const auto& text : stringsFromJson(json["favourites"]))
        if (const auto key = PluginKey::fromString(text))
            catalogue.favourites.insert(*key);

    return catalogue;
}

bool PluginCatalogue::saveTo(const juce::File& file) const
{
    // Written whole to a temporary file that replaces the old one, so that a crash
    // never leaves half a catalogue.
    const juce::TemporaryFile temporary(file);
    if (!temporary.getFile().replaceWithText(juce::JSON::toString(toJson()) + "\n", false, false, "\n"))
        return false;
    return temporary.overwriteTargetFileWithTemporary();
}

PluginCatalogue PluginCatalogue::loadFrom(const juce::File& file)
{
    if (!file.existsAsFile())
        return {};
    return fromJson(juce::JSON::parse(file));
}

} // namespace daw::controller
