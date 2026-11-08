#include "daw/engine/plugins/PluginLayout.h"

#include <array>

namespace daw::engine::plugins
{

namespace
{

juce::AudioChannelSet channelsFor(int count)
{
    switch (count)
    {
    case 1:
        return juce::AudioChannelSet::mono();
    case 2:
        return juce::AudioChannelSet::stereo();
    default:
        return juce::AudioChannelSet::disabled();
    }
}

// The plugin's layout with only the main buses, of these sizes (0: off).
juce::AudioProcessor::BusesLayout mainBusesOnly(const juce::AudioPluginInstance& plugin, int inputs,
                                                int outputs)
{
    juce::AudioProcessor::BusesLayout layout;
    for (int bus = 0; bus < plugin.getBusCount(true); ++bus)
        layout.inputBuses.add(bus == 0 ? channelsFor(inputs) : juce::AudioChannelSet::disabled());
    for (int bus = 0; bus < plugin.getBusCount(false); ++bus)
        layout.outputBuses.add(bus == 0 ? channelsFor(outputs) : juce::AudioChannelSet::disabled());
    return layout;
}

} // namespace

std::variant<PluginLayout, juce::String> setUpStereoLayout(juce::AudioPluginInstance& plugin)
{
    if (plugin.isMidiEffect())
        return juce::String("a MIDI effect");
    if (plugin.getBusCount(false) == 0)
        return juce::String("no audio output");

    const bool hasInput = plugin.getBusCount(true) > 0;
    const bool instrument = plugin.getPluginDescription().isInstrument || !hasInput;

    std::array<PluginLayout, 5> candidates{};
    int count = 0;
    if (instrument)
    {
        candidates[static_cast<std::size_t>(count++)] = {0, 2};
        candidates[static_cast<std::size_t>(count++)] = {0, 1};
    }
    if (hasInput)
    {
        candidates[static_cast<std::size_t>(count++)] = {2, 2};
        candidates[static_cast<std::size_t>(count++)] = {1, 2};
        candidates[static_cast<std::size_t>(count++)] = {1, 1};
    }

    for (int i = 0; i < count; ++i)
    {
        const auto candidate = candidates[static_cast<std::size_t>(i)];
        const auto layout = mainBusesOnly(plugin, candidate.inputs, candidate.outputs);
        if (plugin.checkBusesLayoutSupported(layout) && plugin.setBusesLayout(layout))
            return candidate;
    }

    // None was accepted as asked: keep what it has, if two channels or fewer.
    plugin.disableNonMainBuses();
    const int inputs = hasInput ? plugin.getMainBusNumInputChannels() : 0;
    const int outputs = plugin.getMainBusNumOutputChannels();
    if (outputs < 1)
        return juce::String("no audio output");
    if (inputs > 2 || outputs > 2)
        return "needs " + juce::String(std::max(inputs, outputs)) + " channels";
    return PluginLayout{inputs, outputs};
}

} // namespace daw::engine::plugins
