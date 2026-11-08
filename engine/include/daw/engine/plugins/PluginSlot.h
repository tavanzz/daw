#pragma once

#include "daw/engine/plugins/PluginEffect.h"

#include <atomic>
#include <memory>

namespace daw::engine::plugins
{

/**
    One EffectInstance's hold on a plugin. A plugin cannot be copied as a built-in
    effect can, yet an EffectInstance is made for one sample rate, one latency and
    one place in a chain: when any of these changes, the plugin moves to a new
    EffectInstance, through a new slot, while the old slot is detached (detach()) and
    leaves the plugin alone from then on (an effect passes its input through, an
    instrument adds nothing) until the arrangements still holding it are gone.

    The slot prepares the plugin only if it is not prepared for the same format
    already, so a plugin moved within its chain keeps its state.

    Threads as for any fx::Effect; detach() on the message thread.
*/
class PluginSlot final : public fx::Effect
{
public:
    explicit PluginSlot(std::shared_ptr<PluginEffect> pluginToHold) noexcept;

    [[nodiscard]] const fx::EffectDescription& getDescription() const noexcept override;
    void prepare(const fx::ProcessSetup& setup) override;
    void reset() noexcept override;
    void process(const fx::ProcessBlock& block) noexcept override;
    void panic() noexcept override;
    [[nodiscard]] int getLatency() const noexcept override;
    [[nodiscard]] double getTailSeconds(std::span<const float> parameterValues) const override;
    [[nodiscard]] juce::MemoryBlock getExtraState() const override;
    bool setExtraState(const juce::MemoryBlock& state) override;

    /** Message thread. From now on the plugin is left alone; returns once the audio
        thread is out of it (it may be inside process() right now). */
    void detach() noexcept;
    [[nodiscard]] bool isDetached() const noexcept { return detached.load(); }

    [[nodiscard]] PluginEffect& getPlugin() const noexcept { return *plugin; }
    [[nodiscard]] const std::shared_ptr<PluginEffect>& getSharedPlugin() const noexcept { return plugin; }

private:
    std::shared_ptr<PluginEffect> plugin;
    std::atomic<bool> detached{false};
    std::atomic<bool> processing{false};
};

} // namespace daw::engine::plugins
