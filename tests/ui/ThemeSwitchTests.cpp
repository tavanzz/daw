// The desktop window in every theme, with a project that uses everything the themes
// draw: tracks with clips, effects, a bus with a send, automation, the mixer, and a
// MIDI track playing the synth.

#include "daw/controller/ExportJob.h"
#include "daw/engine/fx/Compressor.h"
#include "daw/engine/fx/Equaliser.h"
#include "daw/engine/fx/Reverb.h"
#include "daw/model/ProjectFile.h"
#include "daw/ui/desktop/EffectEditorWindow.h"
#include "daw/ui/desktop/ExportDialog.h"
#include "daw/ui/desktop/MainComponent.h"
#include "daw/ui/desktop/MainWindow.h"
#include "support/TestAudioFiles.h"

#include <catch2/catch_test_macros.hpp>
#include <juce_audio_formats/juce_audio_formats.h>

#include <cmath>

using namespace daw;
using ui::desktop::MainComponent;
using ui::desktop::ThemeManager;

namespace
{
constexpr double sampleRate = 48000.0;
constexpr int blockSize = 512;

juce::File writeTone(const test::TemporaryDirectory& directory, const juce::String& name,
                     double frequency)
{
    const auto file = directory.file(name);
    juce::AudioBuffer<float> samples(2, 96000);
    for (int channel = 0; channel < 2; ++channel)
        for (int index = 0; index < samples.getNumSamples(); ++index)
            samples.setSample(channel, index,
                              0.3f *
                                  static_cast<float>(std::sin(juce::MathConstants<double>::twoPi *
                                                              frequency * index / sampleRate)));
    REQUIRE(test::writeAudioFile(file, test::AudioFileType::wav, samples, sampleRate, 32, true));
    return file;
}

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

// An engine run as a device would, a session with a full project, and the desktop UI
// over it when asked.
struct Studio
{
    explicit Studio(const test::TemporaryDirectory& directory)
    {
        engine.prepare(sampleRate, blockSize);

        const auto drums = writeTone(directory, "drums.wav", 110.0);
        const auto bass = writeTone(directory, "bass.wav", 55.0);
        session.importAudio({drums, bass}, 0);
        REQUIRE(session.waitForLoads(30000));

        const auto& project = session.getProject();
        const auto first = controller::ChainOwner::track(project.getTrack(0).getId());
        equaliser = session.addEffect(first, engine::fx::Equaliser::typeId);
        compressor = session.addEffect(first, engine::fx::Compressor::typeId);
        const auto bus = session.addBus("Reverb");
        session.addEffect(controller::ChainOwner::bus(bus), engine::fx::Reverb::typeId);
        session.addSend(project.getTrack(1).getId(), bus);
        session.addAutomationPoint(first, {}, {0, 0.5, 0.0});
        session.addAutomationPoint(first, {}, {model::ticksPerSecond, 0.8, 0.3});
        session.setAutomationMode(first, model::AutomationMode::read);

        const auto keys = session.addMidiTrack("Keys");
        const auto clip = session.addMidiClip(keys, 0, 2 * model::ticksPerSecond);
        session.setNotes(clip,
                         {{0, 0, model::ticksPerSecond / 2, 60, 100},
                          {0, model::ticksPerSecond / 4, model::ticksPerSecond, 67, 80}},
                         "Notes");
        session.update();
    }

    std::vector<float> play(int numBlocks, const std::function<void(int)>& betweenBlocks = {})
    {
        std::vector<float> samples;
        juce::AudioBuffer<float> block(2, blockSize);
        session.play();
        for (int index = 0; index < numBlocks; ++index)
        {
            engine.process(block);
            for (int channel = 0; channel < 2; ++channel)
                samples.insert(samples.end(), block.getReadPointer(channel),
                               block.getReadPointer(channel) + blockSize);
            session.update();
            if (betweenBlocks)
                betweenBlocks(index);
        }
        return samples;
    }

    engine::Engine engine;
    controller::Session session{engine, {engine::io::DiskStreamer::Mode::manual}};
    model::ObjectId equaliser = 0;
    model::ObjectId compressor = 0;
};

// The window's content as the app makes it, with its commands registered.
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

juce::String projectJson(const controller::Session& session,
                         const test::TemporaryDirectory& directory)
{
    return model::toJson(session.getProject(), directory.file("project"));
}
} // namespace

TEST_CASE("Switching themes through the menu changes neither the project nor the audio", "[ui]")
{
    test::TemporaryDirectory directory;

    // The same project played twice: once alone, once with the UI switching themes and
    // animation levels between audio blocks.
    Studio plain(directory);
    const auto expected = plain.play(200);

    Studio themed(directory);
    Desktop desktop(themed.session);
    const auto jsonBefore = projectJson(themed.session, directory);
    const auto revisionBefore = themed.session.getRevision();
    const auto undoBefore = themed.session.getUndoDescription();
    const bool unsavedBefore = themed.session.hasUnsavedChanges();

    const auto played =
        themed.play(200,
                    [&desktop](int index)
                    {
                        if (index % 20 == 0)
                            desktop.invoke(MainComponent::firstTheme + (index / 20) % 3);
                        if (index % 50 == 25)
                            desktop.invoke(MainComponent::toggleAllAnimations);
                    });

    CHECK(desktop.themes.getTheme().id != "");
    CHECK(played == expected); // bit for bit
    CHECK(projectJson(themed.session, directory) == jsonBefore);
    CHECK(themed.session.getRevision() == revisionBefore);
    CHECK(themed.session.getUndoDescription() == undoBefore);
    CHECK(themed.session.hasUnsavedChanges() == unsavedBefore);

    // An export after all that equals one from the untouched session.
    const auto exportFrom = [&directory](controller::Session& session, const juce::String& name)
    {
        controller::ExportSettings settings;
        settings.file = directory.file(name);
        settings.sampleRate = sampleRate;
        settings.bitsPerSample = 32;
        controller::ExportJob job(session, settings);
        REQUIRE(job.run().result.wasOk());
        return settings.file;
    };
    CHECK(exportFrom(plain.session, "plain.wav")
              .hasIdenticalContentTo(exportFrom(themed.session, "themed.wav")));
}

TEST_CASE("The theme commands tick the theme in use and follow the menu order", "[ui]")
{
    test::TemporaryDirectory directory;
    Studio studio(directory);
    Desktop desktop(studio.session);

    for (int index = 0; index < 3; ++index)
    {
        REQUIRE(desktop.invoke(MainComponent::firstTheme + index));
        const auto& theme =
            desktop.themes.getLibrary().getThemes()[static_cast<std::size_t>(index)];
        CHECK(desktop.themes.getTheme().id == theme.id);

        juce::ApplicationCommandInfo info(MainComponent::firstTheme + index);
        desktop.view.getCommandInfo(MainComponent::firstTheme + index, info);
        CHECK(info.shortName == theme.name);
        CHECK((info.flags & juce::ApplicationCommandInfo::isTicked) != 0);
        REQUIRE(!info.defaultKeypresses.isEmpty());
        CHECK(info.defaultKeypresses[0] == juce::KeyPress(static_cast<int>('1' + index),
                                                          juce::ModifierKeys::commandModifier, 0));
    }

    juce::ApplicationCommandInfo off(MainComponent::toggleAllAnimations);
    desktop.view.getCommandInfo(MainComponent::toggleAllAnimations, off);
    CHECK(off.defaultKeypresses[0] ==
          juce::KeyPress(
              'a', juce::ModifierKeys::commandModifier | juce::ModifierKeys::shiftModifier, 0));
    CHECK(desktop.invoke(MainComponent::toggleAllAnimations));
    CHECK(desktop.themes.getPreferences().animations.level == appearance::AnimationLevel::off);
    CHECK(desktop.invoke(MainComponent::toggleAllAnimations));
    CHECK(desktop.themes.getPreferences().animations.level == appearance::AnimationLevel::full);
}

TEST_CASE("Every theme paints the window, the mixer, the automation and the editors", "[ui]")
{
    test::TemporaryDirectory directory;
    Studio studio(directory);
    Desktop desktop(studio.session);
    studio.play(40); // the meters have something to show

    for (int index = 0; index < 3; ++index)
    {
        REQUIRE(desktop.invoke(MainComponent::firstTheme + index));
        const auto& id = desktop.themes.getTheme().id;

        for (const bool automation : {false, true})
        {
            if (automation != desktop.view.getArrangeViewForTesting().isShowingAutomation())
                REQUIRE(desktop.invoke(MainComponent::toggleAutomation));
            const auto image = desktop.view.createComponentSnapshot(desktop.view.getLocalBounds());
            CHECK(image.isValid());
            writeSnapshot(desktop.view, "window-" + id + (automation ? "-automation" : ""));
        }

        for (const auto effect : {studio.equaliser, studio.compressor})
        {
            ui::desktop::EffectEditorWindow editor(studio.session, effect);
            editor.refresh(0.05);
            auto* content = editor.getContentComponent();
            REQUIRE(content != nullptr);
            CHECK(content->createComponentSnapshot(content->getLocalBounds()).isValid());
            writeSnapshot(*content, "editor-" + id + "-" +
                                        (effect == studio.equaliser ? "equaliser" : "compressor"));
        }

        ui::desktop::ExportDialog exportDialog(studio.session);
        exportDialog.setSize(exportDialog.getWidth(), exportDialog.getHeight());
        writeSnapshot(exportDialog, "export-" + id);
    }
}

TEST_CASE("The animation clock stops when animations are off or the window is hidden", "[ui]")
{
    test::TemporaryDirectory directory;
    Studio studio(directory);
    ThemeManager themes({});
    themes.setSystemReducesMotionForTesting(false);
    juce::AudioDeviceManager devices;
    ui::desktop::MainWindow window("Test", studio.session, devices, themes);
    auto& clock = themes.getAnimationClock();

    // Hidden: no frames, whatever wants them.
    window.setVisible(false);
    CHECK_FALSE(clock.isHostVisible());
    CHECK_FALSE(clock.isRunning());

    window.setVisible(true);
#if !JUCE_MAC
    // (macOS tells a window it is no longer covered from its event loop, which tests
    // do not run: there the window stays "covered" until then.)
    CHECK(clock.isHostVisible());
#endif

    // Visible, as the window reports once it can be seen: Neon Grid's scanlines and
    // pulsing playhead want frames.
    clock.setHostVisible(true);
    CHECK(clock.isRunning());

    auto settings = themes.getPreferences().animations;
    settings.setLevel(appearance::AnimationLevel::off);
    themes.setAnimationSettings(settings);
    CHECK_FALSE(clock.isRunning());

    settings.setLevel(appearance::AnimationLevel::full);
    for (const auto effect : appearance::allAnimationEffects())
        settings.setSwitchedOn(effect, false);
    themes.setAnimationSettings(settings);
    CHECK_FALSE(clock.isRunning());
}

TEST_CASE("The transport bar's controls fit the narrowest window in every theme", "[ui]")
{
    test::TemporaryDirectory directory;
    Studio studio(directory);
    Desktop desktop(studio.session);
    desktop.view.setSize(daw::ui::desktop::MainWindow::minimumWidth, 700);

    for (int index = 0; index < 3; ++index)
    {
        REQUIRE(desktop.invoke(MainComponent::firstTheme + index));
        INFO(desktop.themes.getTheme().id);

        daw::ui::desktop::TransportBar* bar = nullptr;
        for (auto* child : desktop.view.getChildren())
            if (auto* found = dynamic_cast<daw::ui::desktop::TransportBar*>(child))
                bar = found;
        REQUIRE(bar != nullptr);

        // Every control has its own room, the logo included, at its full width.
        juce::Array<juce::Rectangle<int>> placed;
        for (auto* control : bar->getChildren())
        {
            const auto bounds = control->getBounds();
            CHECK_FALSE(bounds.isEmpty());
            CHECK(bar->getLocalBounds().contains(bounds));
            for (const auto& other : placed)
                CHECK_FALSE(bounds.intersects(other));
            placed.add(bounds);
        }
    }
}
