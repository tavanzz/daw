#include <juce_audio_processors_headless/juce_audio_processors_headless.h>

#include <catch2/catch_test_macros.hpp>

#include <memory>

namespace
{

juce::StringArray formatNames(const juce::AudioPluginFormatManager& manager)
{
    juce::StringArray names;
    for (const auto* format : manager.getFormats())
        names.add(format->getName());
    return names;
}

} // namespace

TEST_CASE("Plugin hosting is compiled in: VST3 everywhere, AU on macOS, LV2 on Linux",
          "[plugins]")
{
    juce::AudioPluginFormatManager manager;
    juce::addHeadlessDefaultFormatsToManager(manager);
    const auto names = formatNames(manager);

    CHECK(names.contains("VST3"));
#if JUCE_MAC
    CHECK(names.contains("AudioUnit"));
#else
    CHECK_FALSE(names.contains("AudioUnit"));
#endif
#if JUCE_LINUX
    CHECK(names.contains("LV2"));
#else
    CHECK_FALSE(names.contains("LV2"));
#endif
    CHECK_FALSE(names.contains("VST"));
    CHECK_FALSE(names.contains("LADSPA"));

    for (auto* format : manager.getFormats())
        if (format->getName() == "VST3")
            CHECK(format->getDefaultLocationsToSearch().getNumPaths() > 0);
}

#if JUCE_MAC
TEST_CASE("An Audio Unit that comes with macOS loads and processes", "[plugins]")
{
    // Apple's AUDelay is on every Mac, the CI's runners included.
    juce::AudioPluginFormatManager manager;
    juce::addHeadlessDefaultFormatsToManager(manager);
    juce::AudioPluginFormat* audioUnits = nullptr;
    for (auto* format : manager.getFormats())
        if (format->getName() == "AudioUnit")
            audioUnits = format;
    REQUIRE(audioUnits != nullptr);

    juce::OwnedArray<juce::PluginDescription> found;
    audioUnits->findAllTypesForFile(found, "AudioUnit:Effects/aufx,dely,appl");
    REQUIRE(found.size() == 1);
    CHECK(found[0]->manufacturerName == "Apple");
    CHECK_FALSE(found[0]->isInstrument);

    juce::String error;
    const auto instance = manager.createPluginInstance(*found[0], 48000.0, 512, error);
    REQUIRE(instance != nullptr);
    CHECK(error.isEmpty());
    CHECK_FALSE(instance->hasEditor()); // headless formats make no editors
    CHECK(instance->getParameters().size() > 0);

    instance->setPlayConfigDetails(2, 2, 48000.0, 512);
    instance->prepareToPlay(48000.0, 512);
    juce::AudioBuffer<float> buffer(2, 512);
    buffer.clear();
    buffer.setSample(0, 0, 1.0f);
    juce::MidiBuffer midi;
    instance->processBlock(buffer, midi);
    bool finite = true;
    for (int channel = 0; channel < 2; ++channel)
        for (int i = 0; i < 512; ++i)
            finite = finite && std::isfinite(buffer.getSample(channel, i));
    CHECK(finite);
    instance->releaseResources();
}
#endif
