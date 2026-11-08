#include "daw/controller/PluginBrowser.h"

#include "daw/controller/Session.h"
#include "daw/engine/fx/BuiltInEffects.h"

#include <algorithm>

namespace daw::controller
{

namespace
{

constexpr const char* builtInPrefix = "builtin:";
constexpr const char* pluginPrefix = "plugin:";

bool matches(const BrowserItem& item, const PluginQuery& query)
{
    if (!query.formats.empty() && !query.formats.contains(item.format))
        return false;
    if ((query.kind == PluginQuery::Kind::effects && item.isInstrument) ||
        (query.kind == PluginQuery::Kind::instruments && !item.isInstrument))
        return false;
    if (query.manufacturer.isNotEmpty() && !item.manufacturer.equalsIgnoreCase(query.manufacturer))
        return false;
    if (query.favouritesOnly && !item.favourite)
        return false;

    juce::StringArray words;
    words.addTokens(query.text, " \t", "\"");
    words.removeEmptyStrings();
    for (const auto& word : words)
        if (!item.name.containsIgnoreCase(word) && !item.manufacturer.containsIgnoreCase(word) &&
            !item.category.containsIgnoreCase(word) && !item.format.containsIgnoreCase(word))
            return false;
    return true;
}

BrowserItem builtIn(const engine::fx::BuiltInEffect& effect, bool instrument, const PluginCatalogue& catalogue)
{
    BrowserItem item;
    item.builtInType = effect.type;
    item.name = effect.name;
    item.format = builtInFormat;
    item.category = instrument ? "Instrument" : "Effect";
    item.isInstrument = instrument;
    item.favourite = catalogue.isFavourite(item.key());
    return item;
}

BrowserItem pluginItem(const PluginInfo& info, const PluginCatalogue& catalogue)
{
    BrowserItem item;
    item.plugin = info.description;
    item.name = info.description.name;
    item.manufacturer = info.description.manufacturerName;
    item.format = info.description.pluginFormatName;
    item.category = info.description.category;
    item.isInstrument = info.description.isInstrument;
    item.favourite = catalogue.isFavourite(item.key());
    return item;
}

} // namespace

PluginKey BrowserItem::key() const
{
    if (plugin)
        return PluginKey::of(*plugin);
    return {builtInFormat, builtInType.hashCode()};
}

juce::String BrowserItem::dragDescription() const
{
    if (plugin)
        return pluginPrefix + key().toString();
    return builtInPrefix + builtInType;
}

std::vector<BrowserItem> browserItems(const PluginCatalogue& catalogue, const PluginQuery& query)
{
    std::vector<BrowserItem> items;
    for (const auto& effect : engine::fx::builtInEffects())
        items.push_back(builtIn(effect, false, catalogue));
    for (const auto& instrument : engine::fx::builtInInstruments())
        items.push_back(builtIn(instrument, true, catalogue));
    for (const auto& info : catalogue.getPlugins())
        if (info.isSupported())
            items.push_back(pluginItem(info, catalogue));

    std::erase_if(items, [&query](const BrowserItem& item) { return !matches(item, query); });
    std::stable_sort(items.begin(), items.end(), [](const BrowserItem& a, const BrowserItem& b) {
        if (const int byName = a.name.compareNatural(b.name); byName != 0)
            return byName < 0;
        return a.format.compareIgnoreCase(b.format) < 0;
    });
    return items;
}

std::optional<BrowserItem> browserItemFor(const PluginCatalogue& catalogue, const juce::String& dragDescription)
{
    if (dragDescription.startsWith(builtInPrefix))
    {
        const auto type = dragDescription.fromFirstOccurrenceOf(builtInPrefix, false, false);
        for (const auto& effect : engine::fx::builtInEffects())
            if (type == effect.type)
                return builtIn(effect, false, catalogue);
        for (const auto& instrument : engine::fx::builtInInstruments())
            if (type == instrument.type)
                return builtIn(instrument, true, catalogue);
        return std::nullopt;
    }
    if (dragDescription.startsWith(pluginPrefix))
        if (const auto key = PluginKey::fromString(dragDescription.fromFirstOccurrenceOf(pluginPrefix, false, false)))
            if (const auto info = catalogue.find(*key); info && info->isSupported())
                return pluginItem(*info, catalogue);
    return std::nullopt;
}

model::ObjectId addFromBrowser(Session& session, const BrowserItem& item, const BrowserTarget& target)
{
    const auto addEffect = [&](const ChainOwner& owner, int index) -> model::ObjectId {
        return item.plugin ? session.addPlugin(owner, *item.plugin, index)
                           : session.addEffect(owner, item.builtInType, index);
    };
    const auto setInstrument = [&](model::ObjectId trackId) -> model::ObjectId {
        return item.plugin ? session.setInstrumentPlugin(trackId, *item.plugin)
                           : session.setInstrument(trackId, item.builtInType);
    };
    const auto isMidiTrack = [&](model::ObjectId trackId) {
        const int index = session.getProject().indexOfTrack(trackId);
        return index >= 0 && session.getProject().getTrack(index).isMidi();
    };

    // A new track and what goes on it: one step.
    const auto onNewTrack = [&]() -> model::ObjectId {
        session.beginGesture("Add " + item.name);
        model::ObjectId added = 0;
        if (item.isInstrument)
        {
            const auto track = session.addMidiTrack({}, {});
            added = setInstrument(track);
        }
        else
        {
            const auto track = session.addTrack();
            added = addEffect(ChainOwner::track(track), -1);
        }
        session.endGesture();
        if (added == 0)
            session.undo();
        return added;
    };

    switch (target.kind)
    {
    case BrowserTarget::Kind::newTrack:
        return onNewTrack();

    case BrowserTarget::Kind::track:
        if (item.isInstrument)
            return isMidiTrack(target.owner.id) ? setInstrument(target.owner.id) : onNewTrack();
        return addEffect(ChainOwner::track(target.owner.id), -1);

    case BrowserTarget::Kind::chain:
        if (item.isInstrument)
            return target.owner.kind == ChainOwner::Kind::track && isMidiTrack(target.owner.id)
                       ? setInstrument(target.owner.id)
                       : 0;
        return addEffect(target.owner, target.index);
    }
    return 0;
}

} // namespace daw::controller
