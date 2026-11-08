#include "support/TestPlugins.h"

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
#elif !JUCE_PLUGINHOST_LV2
    CHECK_FALSE(names.contains("LV2")); // unless switched on to test it (DAW_HOST_LV2)
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

TEST_CASE("The VST3 test plugins load and do what they say, in this process", "[plugins]")
{
    SECTION("gain: unity at its default, a bit-exact copy")
    {
        const auto description = daw::test::describeTestVst3("Gain");
        REQUIRE(description != nullptr);
        CHECK(description->name == "DAW Test Gain");
        CHECK(description->manufacturerName == "DAW Tests");
        CHECK_FALSE(description->isInstrument);

        auto plugin = daw::test::loadTestVst3("Gain");
        REQUIRE(plugin != nullptr);
        REQUIRE(plugin->getParameters().size() >= 1);
        plugin->setPlayConfigDetails(2, 2, 48000.0, 64);
        plugin->prepareToPlay(48000.0, 64);
        juce::AudioBuffer<float> buffer(2, 64);
        juce::Random random(3);
        for (int channel = 0; channel < 2; ++channel)
            for (int i = 0; i < 64; ++i)
                buffer.setSample(channel, i, random.nextFloat() * 2.0f - 1.0f);
        const juce::AudioBuffer<float> input(buffer);
        juce::MidiBuffer midi;
        plugin->processBlock(buffer, midi);
        for (int channel = 0; channel < 2; ++channel)
            for (int i = 0; i < 64; ++i)
                REQUIRE(buffer.getSample(channel, i) == input.getSample(channel, i));

        plugin->getParameters()[0]->setValue(0.25f); // gain 0.5
        plugin->processBlock(buffer, midi);
        CHECK(buffer.getSample(0, 10) == input.getSample(0, 10) * 0.5f);
        plugin->releaseResources();
    }
    SECTION("latency: reports 777 samples and delays by that much")
    {
        auto plugin = daw::test::loadTestVst3("Latency");
        REQUIRE(plugin != nullptr);
        plugin->setPlayConfigDetails(2, 2, 48000.0, 512);
        plugin->prepareToPlay(48000.0, 512);
        CHECK(plugin->getLatencySamples() == 777);
        juce::AudioBuffer<float> buffer(2, 512);
        juce::MidiBuffer midi;
        float found = -1.0f;
        int at = -1;
        for (int block = 0; block < 3; ++block)
        {
            buffer.clear();
            if (block == 0)
                buffer.setSample(0, 5, 1.0f);
            plugin->processBlock(buffer, midi);
            for (int i = 0; i < 512; ++i)
                if (buffer.getSample(0, i) != 0.0f)
                {
                    found = buffer.getSample(0, i);
                    at = block * 512 + i;
                }
        }
        CHECK(at == 5 + 777);
        CHECK(found == 1.0f);
        plugin->releaseResources();
    }
    SECTION("synth: an instrument holding the notes it is given, at their samples")
    {
        const auto description = daw::test::describeTestVst3("Synth");
        REQUIRE(description != nullptr);
        CHECK(description->isInstrument);
        auto plugin = daw::test::loadTestVst3("Synth");
        REQUIRE(plugin != nullptr);
        plugin->setPlayConfigDetails(0, 2, 48000.0, 512);
        plugin->prepareToPlay(48000.0, 512);
        juce::AudioBuffer<float> buffer(2, 512);
        buffer.clear();
        juce::MidiBuffer midi;
        midi.addEvent(juce::MidiMessage::noteOn(1, 60, static_cast<juce::uint8>(127)), 100);
        midi.addEvent(juce::MidiMessage::noteOff(1, 60), 300);
        plugin->processBlock(buffer, midi);
        CHECK(buffer.getSample(0, 99) == 0.0f);
        CHECK(buffer.getSample(0, 100) == 1.0f);
        CHECK(buffer.getSample(1, 100) == 1.0f);
        CHECK(buffer.getSample(0, 299) == 1.0f);
        CHECK(buffer.getSample(0, 300) == 0.0f);
        plugin->releaseResources();
    }
}

#if JUCE_MAC
TEST_CASE("The Audio Unit test plugins load and do what they say, in this process", "[plugins]")
{
    SECTION("gain: unity at its default, a bit-exact copy")
    {
        const auto description = daw::test::describeTestAudioUnit("Gain");
        REQUIRE(description != nullptr);
        CHECK(description->name == "DAW Test Gain");
        CHECK(description->manufacturerName == "DAW Tests");
        auto plugin = daw::test::loadTestAudioUnit("Gain");
        REQUIRE(plugin != nullptr);
        plugin->setPlayConfigDetails(2, 2, 48000.0, 64);
        plugin->prepareToPlay(48000.0, 64);
        juce::AudioBuffer<float> buffer(2, 64);
        juce::Random random(5);
        for (int channel = 0; channel < 2; ++channel)
            for (int i = 0; i < 64; ++i)
                buffer.setSample(channel, i, random.nextFloat() * 2.0f - 1.0f);
        const juce::AudioBuffer<float> input(buffer);
        juce::MidiBuffer midi;
        plugin->processBlock(buffer, midi);
        for (int channel = 0; channel < 2; ++channel)
            for (int i = 0; i < 64; ++i)
                REQUIRE(buffer.getSample(channel, i) == input.getSample(channel, i));
        plugin->releaseResources();
    }
    SECTION("latency: reported in samples, the audio delayed by that much")
    {
        auto plugin = daw::test::loadTestAudioUnit("Latency");
        REQUIRE(plugin != nullptr);
        plugin->setPlayConfigDetails(2, 2, 48000.0, 512);
        plugin->prepareToPlay(48000.0, 512);
        CHECK(plugin->getLatencySamples() == 777);
        juce::AudioBuffer<float> buffer(2, 512);
        juce::MidiBuffer midi;
        int at = -1;
        for (int block = 0; block < 3; ++block)
        {
            buffer.clear();
            if (block == 0)
                buffer.setSample(1, 7, 1.0f);
            plugin->processBlock(buffer, midi);
            for (int i = 0; i < 512; ++i)
                if (buffer.getSample(1, i) == 1.0f)
                    at = block * 512 + i;
        }
        CHECK(at == 7 + 777);
        plugin->releaseResources();
    }
    SECTION("synth: notes at their samples")
    {
        const auto description = daw::test::describeTestAudioUnit("Synth");
        REQUIRE(description != nullptr);
        CHECK(description->isInstrument);
        auto plugin = daw::test::loadTestAudioUnit("Synth");
        REQUIRE(plugin != nullptr);
        plugin->setPlayConfigDetails(0, 2, 48000.0, 512);
        plugin->prepareToPlay(48000.0, 512);
        juce::AudioBuffer<float> buffer(2, 512);
        buffer.clear();
        juce::MidiBuffer midi;
        midi.addEvent(juce::MidiMessage::noteOn(1, 60, static_cast<juce::uint8>(127)), 100);
        midi.addEvent(juce::MidiMessage::noteOff(1, 60), 300);
        plugin->processBlock(buffer, midi);
        CHECK(buffer.getSample(0, 99) == 0.0f);
        CHECK(buffer.getSample(0, 100) == 1.0f);
        CHECK(buffer.getSample(1, 299) == 1.0f);
        CHECK(buffer.getSample(1, 300) == 0.0f);
        plugin->releaseResources();
    }
}
#endif

#if JUCE_PLUGINHOST_LV2
TEST_CASE("The LV2 test plugins load and do what they say, in this process", "[plugins]")
{
    SECTION("gain: unity at its default, a bit-exact copy")
    {
        const auto description = daw::test::describeTestLv2("Gain");
        REQUIRE(description != nullptr);
        CHECK(description->manufacturerName == "DAW Tests");
        CHECK_FALSE(description->isInstrument);
        auto plugin = daw::test::loadTestLv2("Gain");
        REQUIRE(plugin != nullptr);
        plugin->setPlayConfigDetails(2, 2, 48000.0, 64);
        plugin->prepareToPlay(48000.0, 64);
        juce::AudioBuffer<float> buffer(2, 64);
        juce::Random random(9);
        for (int channel = 0; channel < 2; ++channel)
            for (int i = 0; i < 64; ++i)
                buffer.setSample(channel, i, random.nextFloat() * 2.0f - 1.0f);
        const juce::AudioBuffer<float> input(buffer);
        juce::MidiBuffer midi;
        plugin->processBlock(buffer, midi);
        for (int channel = 0; channel < 2; ++channel)
            for (int i = 0; i < 64; ++i)
                REQUIRE(buffer.getSample(channel, i) == input.getSample(channel, i));
        plugin->releaseResources();
    }
    SECTION("latency: reported, the audio delayed by that much")
    {
        auto plugin = daw::test::loadTestLv2("Latency");
        REQUIRE(plugin != nullptr);
        plugin->setPlayConfigDetails(2, 2, 48000.0, 512);
        plugin->prepareToPlay(48000.0, 512);
        juce::AudioBuffer<float> buffer(2, 512);
        juce::MidiBuffer midi;
        int at = -1;
        for (int block = 0; block < 3; ++block)
        {
            buffer.clear();
            if (block == 0)
                buffer.setSample(0, 3, 1.0f);
            plugin->processBlock(buffer, midi);
            for (int i = 0; i < 512; ++i)
                if (buffer.getSample(0, i) == 1.0f)
                    at = block * 512 + i;
        }
        CHECK(at == 3 + 777);
        CHECK(plugin->getLatencySamples() == 777);
        plugin->releaseResources();
    }
    SECTION("synth: notes at their samples")
    {
        const auto description = daw::test::describeTestLv2("Synth");
        REQUIRE(description != nullptr);
        CHECK(description->isInstrument);
        auto plugin = daw::test::loadTestLv2("Synth");
        REQUIRE(plugin != nullptr);
        plugin->setPlayConfigDetails(0, 2, 48000.0, 512);
        plugin->prepareToPlay(48000.0, 512);
        juce::AudioBuffer<float> buffer(2, 512);
        buffer.clear();
        juce::MidiBuffer midi;
        midi.addEvent(juce::MidiMessage::noteOn(1, 60, static_cast<juce::uint8>(127)), 100);
        midi.addEvent(juce::MidiMessage::noteOff(1, 60), 300);
        plugin->processBlock(buffer, midi);
        CHECK(buffer.getSample(0, 99) == 0.0f);
        CHECK(buffer.getSample(0, 100) == 1.0f);
        CHECK(buffer.getSample(1, 299) == 1.0f);
        CHECK(buffer.getSample(1, 300) == 0.0f);
        plugin->releaseResources();
    }
}
#endif
