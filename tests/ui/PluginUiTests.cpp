// The desktop UI's plugin parts in every theme: plugin editor windows (the generic
// editor for the test plugins, which have none of their own), placeholders for
// plugins that are missing or kept from loading, the plugin browser, the mixer's
// slots, and items dragged from the browser.

#include "daw/controller/PluginScanner.h"
#include "daw/ui/desktop/EffectEditorWindow.h"
#include "daw/ui/desktop/EffectSlots.h"
#include "daw/ui/desktop/MainComponent.h"
#include "daw/ui/desktop/PluginEditorWindow.h"
#include "daw/ui/desktop/PluginManagerPanel.h"
#include "support/TestAudioFiles.h"
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
void addTestPlugins(controller::PluginCatalogue& catalogue)
{
    juce::AudioPluginFormatManager formats;
    juce::addHeadlessDefaultFormatsToManager(formats);
    for (const auto* kind : {"Gain", "Synth"})
    {
        const controller::ScanItem item{"VST3", test::testVst3(kind).getFullPathName()};
        auto outcome = controller::scanItem(formats, item, {});
        REQUIRE_FALSE(outcome.failure);
        catalogue.addScanned(item, std::move(outcome.plugins));
    }
}

// A session with the test plugins in its catalogue: an audio track with the gain.
struct PluginStudio
{
    PluginStudio()
    {
        addTestPlugins(library.getCatalogue());
        REQUIRE(library.update()); // saves it
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
        const auto plugins = library.getCatalogue().getPlugins();
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
        result.catalogue = &library.getCatalogue();
        return result;
    }

    controller::PluginLibrary::Options libraryOptions()
    {
        controller::PluginLibrary::Options result;
        result.catalogueFile = directory.file("Plugins.json");
        return result;
    }

    test::TemporaryDirectory directory;
    controller::PluginLibrary library{libraryOptions()};
    controller::PluginLoader loader{controller::PluginLoader::formatsWithoutEditors()};
    engine::Engine engine;
    controller::Session session{engine, options()};
    model::ObjectId track = 0;
    model::ObjectId gain = 0;
};

struct Desktop
{
    explicit Desktop(controller::Session& session, controller::PluginLibrary* library = nullptr)
        : view(session, devices, commands, themes, library)
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
        loads.add({{studio.gain, "DAW Test Gain", 0.62f, 0.8f, 0}}, 1.0);
        PluginEditorWindow window(studio.session, studio.gain, positions, loads);
        window.refresh(0.05);
        auto* content = window.getContentComponent();
        REQUIRE(content != nullptr);
        CHECK(content->createComponentSnapshot(content->getLocalBounds()).isValid());
        writeSnapshot(*content, "plugin-editor-" + id);
    }
}

TEST_CASE("The plugin browser lists, filters, keeps favourites and adds what is double-clicked",
          "[ui][plugins]")
{
    PluginStudio studio;
    Desktop desktop(studio.session, &studio.library);
    REQUIRE(desktop.invoke(MainComponent::togglePluginBrowser));
    auto& browser = desktop.view.getPluginBrowserForTesting();
    CHECK(browser.isVisible());
    CHECK(browser.getWidth() == ui::desktop::PluginBrowserPanel::width);

    const auto names = [&browser] {
        juce::StringArray result;
        for (const auto& item : browser.getItems())
            result.add(item.name);
        return result;
    };
    CHECK(names().contains("Equaliser"));
    CHECK(names().contains("Synth"));
    CHECK(names().contains("DAW Test Gain"));
    CHECK(names().contains("DAW Test Synth"));

    browser.setKind(controller::PluginQuery::Kind::instruments);
    for (const auto& item : browser.getItems())
        CHECK(item.isInstrument);
    browser.setKind(controller::PluginQuery::Kind::any);
    browser.setSearchText("test gain");
    REQUIRE(names() == juce::StringArray{"DAW Test Gain"});

    // A favourite is kept in the catalogue's file.
    browser.toggleFavourite(0);
    CHECK(browser.getItems()[0].favourite);
    browser.setSearchText({});
    browser.setFavouritesOnly(true);
    CHECK(names() == juce::StringArray{"DAW Test Gain"});
    CHECK(studio.library.update());
    CHECK(controller::PluginCatalogue::loadFrom(studio.directory.file("Plugins.json"))
              .isFavourite(controller::PluginKey::of(studio.description("Gain"))));

    // A double-click with no clip selected: a new track with it, its editor open.
    const int tracks = studio.session.getProject().getNumTracks();
    const auto added = browser.addRow(0);
    REQUIRE(added != 0);
    CHECK(studio.session.getProject().getNumTracks() == tracks + 1);
    CHECK(studio.session.getUndoDescription() == "Add DAW Test Gain");
    CHECK(desktop.view.getEffectEditorsForTesting().find(added) != nullptr);
    studio.session.undo();
    CHECK(studio.session.getProject().getNumTracks() == tracks);
}

TEST_CASE("Items dragged from the browser go onto tracks, below them and into slots", "[ui][plugins]")
{
    PluginStudio studio;
    Desktop desktop(studio.session, &studio.library);
    auto& target = desktop.view.getArrangeViewForTesting().getDropTargetForTesting();
    const auto drag = [](const juce::String& description, int y) {
        return juce::DragAndDropTarget::SourceDetails(description, nullptr, {400, y});
    };
    const auto gainDrag = "plugin:" + controller::PluginKey::of(studio.description("Gain")).toString();
    const auto synthDrag = "plugin:" + controller::PluginKey::of(studio.description("Synth")).toString();
    const auto lane = ui::desktop::ArrangeState::laneHeight;
    const auto& project = studio.session.getProject();

    // An effect onto a track: last in its chain.
    REQUIRE(target.isInterestedInDragSource(drag(gainDrag, lane / 2)));
    CHECK_FALSE(target.isInterestedInDragSource(drag("plugin:VST3:1", lane / 2)));
    target.itemDropped(drag(gainDrag, lane / 2));
    auto effects = studio.session.getEffects(ChainOwner::track(studio.track));
    REQUIRE(effects.getNumEffects() == 2);
    CHECK(effects.getEffect(1).getType() == model::pluginEffectType);

    // An instrument onto an audio track, or below the tracks: a new MIDI track with it.
    target.itemDropped(drag(synthDrag, lane / 2));
    REQUIRE(project.getNumTracks() == 2);
    CHECK(project.getTrack(1).isMidi());
    REQUIRE(project.getTrack(1).getInstrument().has_value());
    CHECK(project.getTrack(1).getInstrument()->getPlugin()->name == "DAW Test Synth");
    target.itemDropped(drag("builtin:synth", 5 * lane));
    REQUIRE(project.getNumTracks() == 3);
    CHECK(project.getTrack(2).getInstrument()->getType() == "synth");

    // A built-in effect into a slot: where it is dropped.
    ui::desktop::EffectSlots slots(studio.session, ChainOwner::track(studio.track));
    slots.setSize(160, 5 * ui::desktop::EffectSlots::rowHeight);
    REQUIRE(slots.isInterestedInDragSource(drag("builtin:equaliser", 0)));
    slots.itemDropped(drag("builtin:equaliser", 2));
    effects = studio.session.getEffects(ChainOwner::track(studio.track));
    REQUIRE(effects.getNumEffects() == 3);
    CHECK(effects.getEffect(0).getType() == "equaliser");
}

TEST_CASE("Mixer slots name plugins and show those that are missing, kept back or slow", "[ui][plugins]")
{
    PluginStudio studio;
    auto gone = studio.description("Gain");
    gone.name = "Gone";
    gone.uniqueId = 12345;
    gone.deprecatedUid = 12345;
    gone.fileOrIdentifier = "/nowhere/Gone.vst3";
    REQUIRE(studio.session.addPlugin(ChainOwner::track(studio.track), gone) != 0);
    studio.session.holdBackPlugins({"DAW Test Synth (VST3)"});
    const auto keys = studio.session.addMidiTrack("Keys");
    const auto synth = studio.session.setInstrumentPlugin(keys, studio.description("Synth"));
    studio.settle();

    ui::desktop::EffectSlots slots(studio.session, ChainOwner::track(studio.track));
    slots.setSize(160, 5 * ui::desktop::EffectSlots::rowHeight);
    slots.refresh(true);
    CHECK(slots.getRowTexts() == juce::StringArray{"DAW Test Gain", "Gone (missing)"});
    CHECK(studio.session.getUnavailablePlugins() == juce::StringArray{"Gone (VST3)"});

    ui::desktop::EffectSlots instrument(studio.session, ChainOwner::track(keys));
    instrument.setSize(160, 5 * ui::desktop::EffectSlots::rowHeight);
    instrument.refresh(true);
    CHECK(instrument.getRowTexts() == juce::StringArray{"DAW Test Synth (off)"});
    studio.session.allowPlugin(synth);
    studio.settle();
    instrument.refresh(false);
    CHECK(instrument.getRowTexts() == juce::StringArray{"DAW Test Synth"});

    Desktop desktop(studio.session, &studio.library);
    for (int index = 0; index < 3; ++index)
    {
        REQUIRE(desktop.invoke(MainComponent::firstTheme + index));
        const auto& id = desktop.themes.getTheme().id;
        if (!desktop.view.getPluginBrowserForTesting().isVisible())
            REQUIRE(desktop.invoke(MainComponent::togglePluginBrowser));
        CHECK(slots.createComponentSnapshot(slots.getLocalBounds()).isValid());
        CHECK(desktop.view.createComponentSnapshot(desktop.view.getLocalBounds()).isValid());
        writeSnapshot(desktop.view, "plugins-window-" + id);
    }
}

TEST_CASE("The Plugin Manager lists plugins, those not loaded and the folders searched", "[ui][plugins]")
{
    PluginStudio studio;
    auto& catalogue = studio.library.getCatalogue();
    catalogue.block({"VST3", "/Plugins/Broken.vst3"}, controller::ScanFailure::crashed, "signal 11");
    REQUIRE(studio.library.update());

    ui::desktop::PluginManagerPanel panel(studio.library);
    CHECK(panel.getNumPluginRows() == 2);
    CHECK(panel.getNumNotLoadedRows() == 1);

    // A folder of the user's own comes after the defaults, and goes again.
    panel.showPage(ui::desktop::PluginManagerPanel::Page::folders);
    const int defaults = panel.getFolderLines().size();
    panel.addFolder("VST3", studio.directory.file("More"));
    CHECK(catalogue.getCustomFolders("VST3") == juce::StringArray{studio.directory.file("More").getFullPathName()});
    CHECK(panel.getFolderLines().size() == defaults + 1);
    CHECK(panel.getFolderLines()[defaults] == studio.directory.file("More").getFullPathName());
    panel.removeFolder("VST3", 0);
    CHECK(catalogue.getCustomFolders("VST3").isEmpty());

    Desktop desktop(studio.session, &studio.library);
    for (int index = 0; index < 3; ++index)
    {
        REQUIRE(desktop.invoke(MainComponent::firstTheme + index));
        const auto& id = desktop.themes.getTheme().id;
        for (const auto page : {ui::desktop::PluginManagerPanel::Page::plugins,
                                ui::desktop::PluginManagerPanel::Page::notLoaded})
        {
            panel.showPage(page);
            CHECK(panel.createComponentSnapshot(panel.getLocalBounds()).isValid());
        }
        writeSnapshot(panel, "plugin-manager-" + id);
    }
}
