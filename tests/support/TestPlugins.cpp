#include "support/TestPlugins.h"

#if JUCE_MAC
#include "TestAudioUnits.h"
#endif

namespace daw::test
{

#if JUCE_PLUGINHOST_LV2
namespace
{
// JUCE's LV2 format knows the plugins of the folders it searched (its own lilv world):
// one format, which has searched the test plugins' folder, describes and loads them.
juce::LV2PluginFormatHeadless& lv2Format()
{
    static juce::LV2PluginFormatHeadless format;
    static const juce::StringArray identifiers = format.searchPathsForPlugins(
        juce::FileSearchPath(testPluginsFolder().getChildFile("LV2").getFullPathName()), true, false);
    return format;
}
} // namespace

std::unique_ptr<juce::PluginDescription> describeTestLv2(const juce::String& kind)
{
    auto& format = lv2Format();
    const juce::FileSearchPath folders(testPluginsFolder().getChildFile("LV2").getFullPathName());
    for (const auto& identifier : format.searchPathsForPlugins(folders, true, false))
    {
        juce::OwnedArray<juce::PluginDescription> found;
        format.findAllTypesForFile(found, identifier);
        for (auto* description : found)
            if (description->name == "DAW Test " + kind)
                return std::make_unique<juce::PluginDescription>(*description);
    }
    return nullptr;
}

std::unique_ptr<juce::AudioPluginInstance> loadTestLv2(const juce::String& kind, double sampleRate,
                                                       int blockSize)
{
    const auto description = describeTestLv2(kind);
    if (description == nullptr)
        return nullptr;
    juce::String error;
    return lv2Format().createInstanceFromDescription(*description, sampleRate, blockSize, error);
}
#endif

#if JUCE_MAC
std::unique_ptr<juce::PluginDescription> describeTestAudioUnit(const juce::String& kind)
{
    testplugins::registerTestAudioUnits();
    juce::AudioUnitPluginFormatHeadless format;
    juce::OwnedArray<juce::PluginDescription> found;
    format.findAllTypesForFile(found, testAudioUnitIdentifier(kind));
    if (found.size() != 1)
        return nullptr;
    return std::make_unique<juce::PluginDescription>(*found[0]);
}

std::unique_ptr<juce::AudioPluginInstance> loadTestAudioUnit(const juce::String& kind, double sampleRate,
                                                             int blockSize)
{
    const auto description = describeTestAudioUnit(kind);
    if (description == nullptr)
        return nullptr;
    juce::AudioUnitPluginFormatHeadless format;
    juce::String error;
    return format.createInstanceFromDescription(*description, sampleRate, blockSize, error);
}
#endif

} // namespace daw::test
