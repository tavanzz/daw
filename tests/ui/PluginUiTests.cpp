// The desktop UI's plugin parts in every theme: plugin editor windows (the generic
// editor for the test plugins, which have none of their own), placeholders for
// plugins that are missing or kept from loading.

#include "daw/controller/PluginScanner.h"
#include "daw/ui/desktop/EffectEditorWindow.h"
#include "daw/ui/desktop/MainComponent.h"
#include "daw/ui/desktop/PluginEditorWindow.h"
#include "support/TestPlugins.h"

#include <catch2/catch_test_macros.hpp>

#include <algorithm>

using namespace daw;
using controller::ChainOwner;
using controller::PluginStatus;
using ui::desktop::MainComponent;
using ui::desktop::PluginEditorWindow;
using ui::desktop::ThemeManager;

namespace
{
constexpr double sampleRate = 48000.0;

void writeSnapshot(juce::Component& component, const juce::String& name)
{
    const auto folder = juce::SystemStats::getEnvironmentVariable("DAW_UI_SNAPSHOTS", {});
    if (folder.isEmpty())
        return;

    const auto image = component.createComponentSnapshot(component.getLocalBounds(), true, 2.0f);
    const auto file = juce::File(folder).getChildFile(name + ".png");
    file.deleteFile();
    juce::FileOutputStream stream(file);
    juce::PNGImageFormat().writeImageToStream(image, stream);
}

// The VST3 test plugins, scanned in this process (they behave).
controller::PluginCatalogue testCatalogue()
{
    controller::PluginCatalogue catalogue;
    juce::AudioPluginFormatManager formats;
    juce::addHeadlessDefaultFormatsToManager(formats);
    for (const auto* kind : {"Gain", "Synth"})
    {
        const controller::ScanItem item{"VST3", test::testVst3(kind).getFullPathName()};
        auto outcome = controller::scanItem(formats, item, {});
        REQUIRE_FALSE(outcome.failure);
        catalogue.addScanned(item, std::move(outcome.plugins));
    }
    return catalogue;
}

// A session with the test plugins in its catalogue: an audio track with the gain.
struct PluginStudio
{
    PluginStudio()
    {
        engine.prepare(sampleRate, 512);
        session.setSampleRate(sampleRate);
        track = session.addTrack("Vocals");
        gain = session.addPlugin(ChainOwner::track(track), description("Gain"));
        REQUIRE(gain != 0);
        settle();
        REQUIRE(session.getPluginStatus(gain) == PluginStatus::ready);
    }

    [[nodiscard]] juce::PluginDescription description(const juce::String& kind) const
    {
        const auto& plugins = catalogue.getPlugins();
        const auto found = std::find_if(plugins.begin(), plugins.end(), [&kind](const auto& info) {
            return info.description.name == "DAW Test " + kind;
        });
        REQUIRE(found != plugins.end());
        return found->description;
    }

    // Plugins load at once with the headless formats; the session takes them at its
    // next updates.
    void settle()
    {
        juce::AudioBuffer<float> block(2, 512);
        for (int i = 0; i < 4; ++i)
        {
            engine.process(block);
            session.update();
        }
    }

    controller::SessionOptions options()
    {
        controller::SessionOptions result;
        result.streaming = engine::io::DiskStreamer::Mode::manual;
        result.plugins = &loader;
        result.catalogue = &catalogue;
        return result;
    }

    controller::PluginCatalogue catalogue = testCatalogue();
    controller::PluginLoader loader{controller::PluginLoader::formatsWithoutEditors()};
    engine::Engine engine;
    controller::Session session{engine, options()};
    model::ObjectId track = 0;
    model::ObjectId gain = 0;
};

struct Desktop
{
    explicit Desktop(controller::Session& session) : view(session, devices, commands, themes)
    {
        commands.registerAllCommandsForTarget(&view);
        commands.setFirstCommandTarget(&view); // no window has the focus in tests
        view.setSize(1600, 1000);
    }

    bool invoke(juce::CommandID command) { return commands.invokeDirectly(command, false); }

    ThemeManager themes{{}};
    juce::AudioDeviceManager devices;
    juce::ApplicationCommandManager commands;
    MainComponent view;
};
} // namespace

TEST_CASE("A plugin opens in a window of its own, where it was last", "[ui][plugins]")
{
    PluginStudio studio;
    Desktop desktop(studio.session);
    auto& editors = desktop.view.getEffectEditorsForTesting();
    auto& positions = desktop.view.getPluginWindowPositions();
    const auto key = controller::PluginKey::of(studio.description("Gain")).toString();
    int changes = 0;
    positions.onChange = [&changes] { ++changes; };

    editors.open(studio.gain);
    auto* window = dynamic_cast<PluginEditorWindow*>(editors.find(studio.gain));
    REQUIRE(window != nullptr);
    editors.refresh(0.05);
    // The test plugins have no editor of their own: the generic one, with every
    // automatable parameter.
    CHECK_FALSE(window->isShowingPluginEditor());
    CHECK(window->getName() == "DAW Test Gain - Vocals");
    CHECK(window->isAlwaysOnTop());
    CHECK(window->getContentComponent()->getHeight() > 100);

    window->setTopLeftPosition(140, 160);
    REQUIRE(positions.get(key).has_value());
    CHECK(*positions.get(key) == juce::Point<int>(140, 160));
    CHECK(changes > 0);

    // Positions go to the user settings as JSON and come back.
    const auto json = juce::JSON::toString(positions.toJson());
    const auto read = ui::desktop::WindowPositions::fromJson(juce::JSON::parse(json));
    CHECK(read.get(key) == positions.get(key));
    CHECK_FALSE(ui::desktop::WindowPositions::fromJson(juce::JSON::parse("[1, 2]")).get(key).has_value());

    // Another plugin of the same kind opens where the first one was.
    editors.closeAll();
    const auto second = studio.session.addPlugin(ChainOwner::track(studio.track), studio.description("Gain"));
    studio.settle();
    editors.open(second);
    auto* other = editors.find(second);
    REQUIRE(other != nullptr);
    CHECK(other->getPosition() == juce::Point<int>(140, 160));

    // Its window closes with the effect; undo brings the effect back, not the window.
    studio.session.removeEffect(second);
    editors.refresh(0.05);
    CHECK(editors.find(second) == nullptr);
}

TEST_CASE("A plugin's window shows a placeholder while it is missing or kept from loading",
          "[ui][plugins]")
{
    PluginStudio studio;
    Desktop desktop(studio.session);
    auto& editors = desktop.view.getEffectEditorsForTesting();

    // Not in the catalogue: missing, its settings kept.
    auto gone = studio.description("Gain");
    gone.name = "Gone";
    gone.uniqueId = 12345;
    gone.deprecatedUid = 12345;
    gone.fileOrIdentifier = "/nowhere/Gone.vst3";
    const auto missing = studio.session.addPlugin(ChainOwner::track(studio.track), gone);
    REQUIRE(missing != 0);
    studio.settle();
    REQUIRE(studio.session.getPluginStatus(missing) == PluginStatus::missing);
    editors.open(missing);
    REQUIRE(editors.find(missing) != nullptr);
    editors.refresh(0.05);
    CHECK(editors.find(missing)->getContentComponent()->getHeight() < 250);

    // Kept from loading, then loaded: the window turns into its editor.
    studio.session.holdBackPlugins({"DAW Test Synth (VST3)"});
    const auto synthTrack = studio.session.addMidiTrack("Keys");
    const auto synth = studio.session.setInstrumentPlugin(synthTrack, studio.description("Synth"));
    REQUIRE(synth != 0);
    studio.settle();
    REQUIRE(studio.session.getPluginStatus(synth) == PluginStatus::off);
    editors.open(synth);
    auto* window = dynamic_cast<PluginEditorWindow*>(editors.find(synth));
    REQUIRE(window != nullptr);
    editors.refresh(0.05);
    CHECK_FALSE(window->isShowingPluginEditor());

    studio.session.allowPlugin(synth);
    studio.settle();
    REQUIRE(studio.session.getPluginStatus(synth) == PluginStatus::ready);
    const int placeholderHeight = window->getContentComponent()->getHeight();
    editors.refresh(0.05);
    CHECK(window->getContentComponent()->getHeight() > placeholderHeight);
}

TEST_CASE("Every theme paints the plugin windows", "[ui][plugins]")
{
    PluginStudio studio;
    Desktop desktop(studio.session);
    for (int index = 0; index < 3; ++index)
    {
        REQUIRE(desktop.invoke(MainComponent::firstTheme + index));
        const auto& id = desktop.themes.getTheme().id;
        ui::desktop::WindowPositions positions;
        controller::PluginLoads loads;
        loads.add({{studio.gain, "DAW Test Gain", {0.62f, 0.8f, 0}}}, 1.0);
        PluginEditorWindow window(studio.session, studio.gain, positions, loads);
        window.refresh(0.05);
        auto* content = window.getContentComponent();
        REQUIRE(content != nullptr);
        CHECK(content->createComponentSnapshot(content->getLocalBounds()).isValid());
        writeSnapshot(*content, "plugin-editor-" + id);
    }
}
