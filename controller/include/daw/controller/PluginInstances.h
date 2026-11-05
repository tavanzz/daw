#pragma once

#include "daw/controller/PluginCatalogue.h"
#include "daw/engine/fx/EffectInstance.h"
#include "daw/engine/plugins/PluginSlot.h"
#include "daw/model/Effect.h"

#include <functional>
#include <map>
#include <memory>
#include <optional>
#include <set>
#include <vector>

namespace daw::controller
{

/**
    Instantiates plugins. The app's loader uses JUCE's formats with editors; tests and
    anything without a UI use the headless ones (formatsWithoutEditors()). Message
    thread; the callback comes on the message thread too: at once, or later through
    the message loop for plugins that answer asynchronously (AUv3).
*/
class PluginLoader
{
public:
    using Created = std::function<void(std::unique_ptr<juce::AudioPluginInstance>, const juce::String& error)>;

    explicit PluginLoader(std::unique_ptr<juce::AudioPluginFormatManager> formatsToUse);

    /** JUCE's formats without editors (instances say hasEditor() false). */
    [[nodiscard]] static std::unique_ptr<juce::AudioPluginFormatManager> formatsWithoutEditors();

    void load(const juce::PluginDescription& description, double sampleRate, int blockSize, Created done);

    /** Loads at once, on any thread the format allows (exports); null and an error if
        it cannot. */
    [[nodiscard]] std::unique_ptr<juce::AudioPluginInstance>
    loadNow(const juce::PluginDescription& description, double sampleRate, int blockSize, juce::String& error);

    /** LV2 finds plugins in the folders it has read: the catalogue's. */
    void readFolders(const PluginCatalogue& catalogue);

    [[nodiscard]] juce::AudioPluginFormatManager& getFormats() noexcept { return *formats; }

private:
    std::unique_ptr<juce::AudioPluginFormatManager> formats;
};

/** What a plugin effect of the project is now. */
enum class PluginStatus
{
    loading, ///< being instantiated
    ready,   ///< playing
    missing, ///< not in the catalogue (not installed here, or not scanned yet)
    failed,  ///< in the catalogue, but it did not load, or cannot be hosted
    off      ///< kept from loading (after a crash, or opened without plugins)
};

/**
    The plugins of a project's effects, one per model effect of type "plugin": found
    in the catalogue by their reference, instantiated (asynchronously), given their
    state and parameters from the model, and hosted through engine::plugins::
    PluginSlot so that the same plugin can move to a new EffectInstance when the
    sample rate, its latency or its place in the chain changes (the old slot is
    detached first). A plugin that is missing or failed has no instance: its effect is
    left out of the chain (an effect passes the sound through, an instrument is
    silent) while the model keeps its reference, state and automation, saved as they
    are.

    Message thread.
*/
class PluginInstances
{
public:
    PluginInstances(PluginLoader& loaderToUse, const PluginCatalogue& catalogueToUse);
    ~PluginInstances();

    PluginInstances(const PluginInstances&) = delete;
    PluginInstances& operator=(const PluginInstances&) = delete;

    /** The format instances are made for; a new one moves every plugin to new slots. */
    void prepare(double sampleRate, int maximumBlockSize);

    /** The instance a plugin effect plays through now: null while it loads, or if it is
        missing or failed. renew: it moved within its chain (a new slot, the old one
        detached). Starts loading a plugin it has not seen. fadeIn: a new instance
        fades in. */
    [[nodiscard]] std::shared_ptr<engine::fx::EffectInstance> instanceFor(const model::Effect& effect,
                                                                          bool renew, bool fadeIn);

    /** Every plugin effect not asked for since the last call is let go (its plugin is
        destroyed once no arrangement holds it). */
    void forgetUnused();

    /** Forgets every plugin (another project). */
    void clear();

    /** Message thread, each update: instances that finished loading, plugins that
        changed latency (moved to new slots). True if the arrangement must be built
        again. */
    bool update();

    /** Missing and failed plugins are looked for again (the catalogue changed). */
    void retryMissing();

    /** Keeps every plugin from loading (a project opened without plugins), or no
        longer; their effects are placeholders (PluginStatus::off) until allowed. */
    void holdBackAll(bool hold);
    /** Keeps plugins of this name ("name (format)", as RunningPlugin reports them)
        from loading: the one running when the app crashed. */
    void holdBack(const juce::String& displayName);
    /** Lets one effect's plugin load after all. */
    void allow(model::ObjectId effectId);
    /** Lets every plugin load. */
    void allowAll();

    /** "name (format)": how RunningPlugin and holdBack() name a plugin. */
    [[nodiscard]] static juce::String displayNameOf(const model::PluginReference& reference);

    [[nodiscard]] PluginStatus getStatus(model::ObjectId effectId) const;
    /** Why it is missing or failed ("not installed", "crashed when scanned"...). */
    [[nodiscard]] juce::String getProblem(model::ObjectId effectId) const;

    /** The plugin of a ready effect, for its editor, parameters and reports; null
        otherwise. */
    [[nodiscard]] engine::plugins::PluginEffect* findPlugin(model::ObjectId effectId) const;

    /** The same, shared: an editor keeps its plugin alive until it closes (a plugin
        must outlive its editor). */
    [[nodiscard]] std::shared_ptr<engine::plugins::PluginEffect> sharePlugin(model::ObjectId effectId) const;

    /** Every ready plugin, by effect id. */
    [[nodiscard]] std::vector<std::pair<model::ObjectId, engine::plugins::PluginEffect*>> getPlugins() const;

    /** The description to load a referenced plugin with, from the catalogue; nothing if
        it is not there. */
    [[nodiscard]] std::optional<PluginInfo> findInCatalogue(const model::PluginReference& reference) const;

    /** A reference to a plugin of the catalogue, for the model. */
    [[nodiscard]] static model::PluginReference referenceTo(const juce::PluginDescription& description);

private:
    struct Entry
    {
        model::PluginReference reference;
        PluginStatus status = PluginStatus::loading;
        juce::String problem;
        std::shared_ptr<engine::plugins::PluginEffect> plugin;
        std::shared_ptr<engine::fx::EffectInstance> instance;
        engine::plugins::PluginSlot* slot = nullptr; ///< the instance's
        std::uint64_t request = 0;                   ///< the load in flight
        juce::MemoryBlock state;                     ///< to give it once loaded
        std::vector<std::pair<juce::String, double>> parameters;
        bool fadeIn = false;
    };

    void startLoading(model::ObjectId effectId, Entry& entry);
    void loaded(model::ObjectId effectId, std::uint64_t request,
                std::unique_ptr<juce::AudioPluginInstance> instance, const juce::String& error);
    void moveToNewSlot(Entry& entry, bool fadeIn);

    PluginLoader& loader;
    const PluginCatalogue& catalogue;
    double sampleRate = 0.0;
    int blockSize = 512;
    std::map<model::ObjectId, Entry> entries;
    std::vector<model::ObjectId> used;
    bool holdingAll = false;
    juce::StringArray heldNames;
    std::set<model::ObjectId> allowed; // despite the above
    std::uint64_t nextRequest = 1;
    bool changed = false;
    std::shared_ptr<bool> alive = std::make_shared<bool>(true); // for late callbacks
};

} // namespace daw::controller
