#pragma once

#include <juce_audio_processors_headless/juce_audio_processors_headless.h>

#include <memory>

namespace daw::test
{

/** Where the test plugins are built (tests/plugins/CMakeLists.txt). */
inline juce::File testPluginsFolder()
{
    return juce::File(DAW_TEST_PLUGINS_DIR);
}

/** A well-behaved VST3 test plugin's bundle: "Gain", "Latency", "Synth" or "NaN". */
inline juce::File testVst3(const juce::String& kind)
{
    return testPluginsFolder().getChildFile("VST3").getChildFile("DAW Test " + kind + ".vst3");
}

/** The test plugins that crash or hang when loaded (only the scanner may load them). */
inline juce::File faultyTestVst3(const juce::String& kind)
{
    return testPluginsFolder().getChildFile("Faulty/VST3").getChildFile("DAW Test " + kind + ".vst3");
}

/** The description of a VST3 test plugin, read in this process (never a faulty one). */
inline std::unique_ptr<juce::PluginDescription> describeTestVst3(const juce::String& kind)
{
    juce::VST3PluginFormatHeadless format;
    juce::OwnedArray<juce::PluginDescription> found;
    format.findAllTypesForFile(found, testVst3(kind).getFullPathName());
    if (found.size() != 1)
        return nullptr;
    return std::make_unique<juce::PluginDescription>(*found[0]);
}

/** A VST3 test plugin instance (headless: no editor), or null. */
inline std::unique_ptr<juce::AudioPluginInstance> loadTestVst3(const juce::String& kind,
                                                               double sampleRate = 48000.0,
                                                               int blockSize = 512)
{
    const auto description = describeTestVst3(kind);
    if (description == nullptr)
        return nullptr;
    juce::VST3PluginFormatHeadless format;
    juce::String error;
    return format.createInstanceFromDescription(*description, sampleRate, blockSize, error);
}

#if JUCE_PLUGINHOST_LV2
/** An LV2 test plugin's bundle ("Gain", "Latency", "Synth", "NaN"). */
inline juce::File testLv2(const juce::String& kind)
{
    return testPluginsFolder().getChildFile("LV2").getChildFile("DAW Test " + kind + ".lv2");
}

/** The description of an LV2 test plugin, or null. */
std::unique_ptr<juce::PluginDescription> describeTestLv2(const juce::String& kind);

/** An LV2 test plugin instance (headless), or null. */
std::unique_ptr<juce::AudioPluginInstance> loadTestLv2(const juce::String& kind, double sampleRate = 48000.0,
                                                       int blockSize = 512);
#endif

#if JUCE_MAC
/** The identifier of an Audio Unit test plugin ("Gain", "Latency", "NaN", "Synth"),
    registered in this process by registerTestAudioUnits(). */
inline juce::String testAudioUnitIdentifier(const juce::String& kind)
{
    if (kind == "Synth")
        return "AudioUnit:Synths/aumu,DtSy,DAWt";
    const auto subtype = kind == "Gain" ? "DtGn" : kind == "Latency" ? "DtLt" : "DtNn";
    return juce::String("AudioUnit:Effects/aufx,") + subtype + ",DAWt";
}

/** The description of an Audio Unit test plugin, or null. */
std::unique_ptr<juce::PluginDescription> describeTestAudioUnit(const juce::String& kind);

/** An Audio Unit test plugin instance (headless: no editor), or null. */
std::unique_ptr<juce::AudioPluginInstance> loadTestAudioUnit(const juce::String& kind,
                                                             double sampleRate = 48000.0,
                                                             int blockSize = 512);
#endif

/** The formats the test plugins come in on this build: VST3, AU on macOS, LV2 where
    LV2 is hosted. */
juce::StringArray testPluginFormats();

/** A well-behaved test plugin ("Gain", "Latency", "Synth", "NaN") of a format, loaded
    headless (no editor); null if not found. */
std::unique_ptr<juce::AudioPluginInstance> loadTestPlugin(const juce::String& format, const juce::String& kind,
                                                          double sampleRate = 48000.0, int blockSize = 512);

} // namespace daw::test
