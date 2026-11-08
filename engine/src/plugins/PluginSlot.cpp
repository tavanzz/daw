#include "daw/engine/plugins/PluginSlot.h"

#include <juce_core/juce_core.h>

#include <thread>

namespace daw::engine::plugins
{

PluginSlot::PluginSlot(std::shared_ptr<PluginEffect> pluginToHold) noexcept
    : plugin(std::move(pluginToHold))
{
}

const fx::EffectDescription& PluginSlot::getDescription() const noexcept
{
    return plugin->getDescription();
}

void PluginSlot::prepare(const fx::ProcessSetup& setup)
{
    if (!detached && !plugin->isPreparedFor(setup))
        plugin->prepare(setup);
}

void PluginSlot::reset() noexcept
{
    // The same handshake as process(): reset() runs on the audio thread too.
    processing.store(true);
    if (!detached.load())
        plugin->reset();
    processing.store(false);
}

void PluginSlot::process(const fx::ProcessBlock& block) noexcept
{
    // Sequentially consistent: detach() either sees this call running and waits for
    // it, or this call sees the slot detached and leaves the plugin alone.
    processing.store(true);
    if (!detached.load())
        plugin->process(block);
    processing.store(false);
}

void PluginSlot::panic() noexcept
{
    if (!detached.load())
        plugin->panic();
}

int PluginSlot::getLatency() const noexcept
{
    return plugin->getLatency();
}

double PluginSlot::getTailSeconds(std::span<const float> parameterValues) const
{
    return plugin->getTailSeconds(parameterValues);
}

juce::MemoryBlock PluginSlot::getExtraState() const
{
    return plugin->getExtraState();
}

bool PluginSlot::setExtraState(const juce::MemoryBlock& state)
{
    return plugin->setExtraState(state);
}

void PluginSlot::detach() noexcept
{
    detached.store(true);
    while (processing.load())
        std::this_thread::yield();
}

} // namespace daw::engine::plugins
