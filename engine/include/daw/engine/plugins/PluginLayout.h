#pragma once

#include <juce_audio_processors_headless/juce_audio_processors_headless.h>

#include <variant>

namespace daw::engine::plugins
{

/** The channels of a plugin's main buses once it is set up for a stereo track: 0 (no
    such bus, an instrument's input), 1 or 2. */
struct PluginLayout
{
    int inputs = 0;
    int outputs = 0;

    bool operator==(const PluginLayout&) const = default;
};

/**
    Message thread, before the plugin is prepared. Sets up a plugin's buses for a
    stereo track: every bus but the main ones off (no sidechain, no extra outputs),
    then the first layout the plugin accepts of stereo in and out, mono in and
    stereo out, mono in and out (an instrument tries no input first). A mono input
    gets (L + R) / 2, a mono output goes to both channels (see PluginEffect).

    Returns the layout, or why the plugin cannot be hosted (a MIDI effect, no audio
    output, more than two main channels).
*/
[[nodiscard]] std::variant<PluginLayout, juce::String> setUpStereoLayout(juce::AudioPluginInstance& plugin);

} // namespace daw::engine::plugins
