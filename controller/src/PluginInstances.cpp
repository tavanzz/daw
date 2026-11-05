#include "daw/controller/PluginInstances.h"

namespace daw::controller
{

PluginLoader::PluginLoader(std::unique_ptr<juce::AudioPluginFormatManager> formatsToUse)
    : formats(std::move(formatsToUse))
{
    jassert(formats != nullptr);
}

std::unique_ptr<juce::AudioPluginFormatManager> PluginLoader::formatsWithoutEditors()
{
    auto manager = std::make_unique<juce::AudioPluginFormatManager>();
    juce::addHeadlessDefaultFormatsToManager(*manager);
    return manager;
}

void PluginLoader::load(const juce::PluginDescription& description, double sampleRate, int blockSize,
                        Created done)
{
    // At once where the format allows it; plugins that answer later (AUv3) through the
    // message loop.
    juce::AudioPluginFormat* format = nullptr;
    for (auto* candidate : formats->getFormats())
        if (candidate->getName() == description.pluginFormatName)
            format = candidate;
    if (format == nullptr || !format->requiresUnblockedMessageThreadDuringCreation(description))
    {
        juce::String error;
        auto instance = formats->createPluginInstance(description, sampleRate, blockSize, error);
        done(std::move(instance), error);
        return;
    }

    formats->createPluginInstanceAsync(
        description, sampleRate, blockSize,
        [whenDone = std::move(done)](std::unique_ptr<juce::AudioPluginInstance> instance, const juce::String& error) {
            whenDone(std::move(instance), error);
        });
}

std::unique_ptr<juce::AudioPluginInstance> PluginLoader::loadNow(const juce::PluginDescription& description,
                                                                 double sampleRate, int blockSize,
                                                                 juce::String& error)
{
    return formats->createPluginInstance(description, sampleRate, blockSize, error);
}

void PluginLoader::readFolders(const PluginCatalogue& catalogue)
{
    for (auto* format : formats->getFormats())
    {
        if (format->getName() == "LV2")
        {
            [[maybe_unused]] const auto found = format->searchPathsForPlugins(catalogue.foldersFor(*format), true, true);
        }
    }
}

PluginInstances::PluginInstances(PluginLoader& loaderToUse, const PluginCatalogue& catalogueToUse)
    : loader(loaderToUse)
    , catalogue(catalogueToUse)
{
}

PluginInstances::~PluginInstances()
{
    *alive = false;
}

void PluginInstances::prepare(double newSampleRate, int maximumBlockSize)
{
    // The plugins move to new slots, prepared for the new format, at the next build.
    sampleRate = newSampleRate;
    blockSize = maximumBlockSize;
}

model::PluginReference PluginInstances::referenceTo(const juce::PluginDescription& description)
{
    model::PluginReference reference;
    reference.format = description.pluginFormatName;
    reference.uniqueId = description.uniqueId;
    reference.name = description.name;
    reference.manufacturer = description.manufacturerName;
    reference.version = description.version;
    reference.category = description.category;
    reference.identifier = description.fileOrIdentifier;
    reference.isInstrument = description.isInstrument;
    return reference;
}

std::optional<PluginInfo> PluginInstances::findInCatalogue(const model::PluginReference& reference) const
{
    if (auto found = catalogue.find({reference.format, reference.uniqueId}))
        return found;
    // A plugin whose id changed (another version): the same format, name and maker.
    for (auto& info : catalogue.getPlugins())
        if (info.description.pluginFormatName == reference.format && info.description.name == reference.name &&
            info.description.manufacturerName == reference.manufacturer)
            return info;
    return std::nullopt;
}

std::shared_ptr<engine::fx::EffectInstance> PluginInstances::instanceFor(const model::Effect& effect, bool renew,
                                                                         bool fadeIn)
{
    const auto id = effect.getId();
    used.push_back(id);
    const auto reference = effect.getPlugin();
    if (!reference)
        return nullptr;

    auto [found, isNew] = entries.try_emplace(id);
    auto& entry = found->second;
    if (isNew || entry.reference != *reference)
    {
        // A plugin not seen yet (or another one in its place): it loads with what the
        // model holds for it, and plays once loaded.
        entry = Entry{};
        entry.reference = *reference;
        entry.state = effect.getExtraState();
        entry.parameters = effect.getParameters();
        entry.fadeIn = fadeIn;
        startLoading(id, entry);
        return nullptr;
    }

    if (entry.status != PluginStatus::ready)
        return nullptr;

    const auto& setup = entry.instance->getSetup();
    if (renew || !juce::exactlyEqual(setup.sampleRate, sampleRate) || setup.maximumBlockSize != blockSize)
        moveToNewSlot(entry, fadeIn);
    return entry.instance;
}

juce::String PluginInstances::displayNameOf(const model::PluginReference& reference)
{
    return reference.name + " (" + reference.format + ")";
}

void PluginInstances::holdBackAll(bool hold)
{
    holdingAll = hold;
    allowed.clear();
}

void PluginInstances::holdBack(const juce::String& displayName)
{
    heldNames.addIfNotAlreadyThere(displayName);
}

void PluginInstances::allow(model::ObjectId effectId)
{
    allowed.insert(effectId);
    if (const auto found = entries.find(effectId);
        found != entries.end() && found->second.status == PluginStatus::off)
        startLoading(effectId, found->second);
}

void PluginInstances::allowAll()
{
    holdingAll = false;
    heldNames.clear();
    for (auto& [id, entry] : entries)
        if (entry.status == PluginStatus::off)
            startLoading(id, entry);
}

void PluginInstances::startLoading(model::ObjectId effectId, Entry& entry)
{
    if (!allowed.contains(effectId) && (holdingAll || heldNames.contains(displayNameOf(entry.reference))))
    {
        entry.status = PluginStatus::off;
        entry.problem = holdingAll ? "not loaded: the project was opened without plugins"
                                   : "not loaded: the app quit unexpectedly while it ran";
        changed = true;
        return;
    }

    const auto info = findInCatalogue(entry.reference);
    if (!info)
    {
        entry.status = PluginStatus::missing;
        entry.problem = "not installed on this computer, or not scanned yet";
        changed = true;
        return;
    }
    if (!info->isSupported())
    {
        entry.status = PluginStatus::failed;
        entry.problem = "cannot be hosted: " + info->unsupported;
        changed = true;
        return;
    }

    entry.status = PluginStatus::loading;
    entry.problem.clear();
    entry.request = nextRequest++;
    const auto request = entry.request;
    const std::weak_ptr<bool> stillAlive = alive;
    loader.load(info->description, sampleRate > 0.0 ? sampleRate : 48000.0, blockSize,
                [this, stillAlive, effectId, request](std::unique_ptr<juce::AudioPluginInstance> instance,
                                                      const juce::String& error) {
                    if (const auto living = stillAlive.lock(); living != nullptr && *living)
                        loaded(effectId, request, std::move(instance), error);
                });
}

void PluginInstances::loaded(model::ObjectId effectId, std::uint64_t request,
                             std::unique_ptr<juce::AudioPluginInstance> instance, const juce::String& error)
{
    const auto found = entries.find(effectId);
    if (found == entries.end() || found->second.request != request)
        return; // asked for no longer (removed, replaced): the instance just goes
    auto& entry = found->second;
    changed = true;

    if (instance == nullptr)
    {
        entry.status = PluginStatus::failed;
        entry.problem = error.isNotEmpty() ? error : juce::String("did not load");
        return;
    }

    auto created = engine::plugins::PluginEffect::create(std::move(instance));
    if (const auto* reason = std::get_if<juce::String>(&created))
    {
        entry.status = PluginStatus::failed;
        entry.problem = "cannot be hosted: " + *reason;
        return;
    }

    entry.plugin = std::move(std::get<std::unique_ptr<engine::plugins::PluginEffect>>(created));
    entry.status = PluginStatus::ready;
    moveToNewSlot(entry, entry.fadeIn);

    // Prepared, it takes its state, then the values the model holds (LV2 plugins
    // reset their ports when they are prepared).
    if (!entry.state.isEmpty())
        entry.plugin->setExtraState(entry.state);
    const auto& description = entry.plugin->getDescription();
    for (const auto& [parameterId, value] : entry.parameters)
        if (const int index = description.indexOf(parameterId); index >= 0)
            entry.plugin->setParameter(index, static_cast<float>(value));
    entry.state.reset();
    entry.parameters.clear();

    // A state may change the latency (a lookahead switched on): the instance is made
    // again with it.
    if (std::max(0, entry.plugin->getPlugin().getLatencySamples()) != entry.instance->getLatency())
        moveToNewSlot(entry, entry.fadeIn);
}

void PluginInstances::moveToNewSlot(Entry& entry, bool fadeIn)
{
    // The old slot lets go first: the new one may prepare the plugin again.
    if (entry.slot != nullptr)
        entry.slot->detach();
    entry.plugin->refreshLatency();

    auto slot = std::make_unique<engine::plugins::PluginSlot>(entry.plugin);
    entry.slot = slot.get();
    engine::fx::ProcessSetup setup;
    setup.sampleRate = sampleRate > 0.0 ? sampleRate : 48000.0;
    setup.maximumBlockSize = blockSize;
    entry.instance = std::make_shared<engine::fx::EffectInstance>(std::move(slot), setup, fadeIn);
}

void PluginInstances::forgetUnused()
{
    // Let go, not detached: a removed plugin still fades out in the arrangements that
    // hold it, and goes with them.
    const std::set<model::ObjectId> asked(used.begin(), used.end());
    for (auto entry = entries.begin(); entry != entries.end();)
        entry = asked.count(entry->first) > 0 ? std::next(entry) : entries.erase(entry);
    used.clear();
}

void PluginInstances::clear()
{
    entries.clear();
    used.clear();
}

bool PluginInstances::update()
{
    // A plugin that reported a new latency moves to an instance made with it (the
    // engine dips the output while the delays change).
    bool rebuild = std::exchange(changed, false);
    for (auto& [id, entry] : entries)
        if (entry.status == PluginStatus::ready && entry.plugin->takeLatencyChanged())
        {
            moveToNewSlot(entry, false);
            rebuild = true;
        }
    return rebuild;
}

void PluginInstances::retryMissing()
{
    for (auto& [id, entry] : entries)
        if (entry.status == PluginStatus::missing || entry.status == PluginStatus::failed)
            startLoading(id, entry);
}

PluginStatus PluginInstances::getStatus(model::ObjectId effectId) const
{
    const auto found = entries.find(effectId);
    return found != entries.end() ? found->second.status : PluginStatus::loading;
}

juce::String PluginInstances::getProblem(model::ObjectId effectId) const
{
    const auto found = entries.find(effectId);
    return found != entries.end() ? found->second.problem : juce::String();
}

engine::plugins::PluginEffect* PluginInstances::findPlugin(model::ObjectId effectId) const
{
    const auto found = entries.find(effectId);
    return found != entries.end() && found->second.status == PluginStatus::ready ? found->second.plugin.get()
                                                                                 : nullptr;
}

std::shared_ptr<engine::plugins::PluginEffect> PluginInstances::sharePlugin(model::ObjectId effectId) const
{
    const auto found = entries.find(effectId);
    return found != entries.end() && found->second.status == PluginStatus::ready ? found->second.plugin : nullptr;
}

std::vector<std::pair<model::ObjectId, engine::plugins::PluginEffect*>> PluginInstances::getPlugins() const
{
    std::vector<std::pair<model::ObjectId, engine::plugins::PluginEffect*>> plugins;
    for (const auto& [id, entry] : entries)
        if (entry.status == PluginStatus::ready)
            plugins.emplace_back(id, entry.plugin.get());
    return plugins;
}

} // namespace daw::controller
