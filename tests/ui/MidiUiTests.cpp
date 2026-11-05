// The desktop UI's MIDI parts in every theme: MIDI tracks and clips, the piano roll
// with its velocity and controller lanes, the instrument editors, and the commands
// that edit notes.

#include "daw/engine/fx/DrumSampler.h"
#include "daw/engine/fx/Sampler.h"
#include "daw/ui/desktop/EffectEditorWindow.h"
#include "daw/ui/desktop/MainComponent.h"
#include "daw/ui/desktop/PianoRoll.h"
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
constexpr model::Ticks bar = 2 * model::ticksPerSecond;      // 4/4 at 120
constexpr model::Ticks sixteenth = model::ticksPerSecond / 8; // at 120

juce::File writeTone(const test::TemporaryDirectory& directory, const juce::String& name,
                     double frequency)
{
    const auto file = directory.file(name);
    juce::AudioBuffer<float> samples(1, 24000);
    for (int index = 0; index < samples.getNumSamples(); ++index)
        samples.setSample(0, index,
                          0.5f * static_cast<float>(std::sin(juce::MathConstants<double>::twoPi *
                                                             frequency * index / sampleRate)) *
                              static_cast<float>(std::exp(-index / 6000.0)));
    REQUIRE(test::writeAudioFile(file, test::AudioFileType::wav, samples, sampleRate, 24, false));
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

// A session with three MIDI tracks: the synth with notes and controllers, a sampler and
// the drum sampler with samples.
struct MidiStudio
{
    explicit MidiStudio(const test::TemporaryDirectory& directory)
    {
        engine.prepare(sampleRate, 512);
        session.setSampleRate(sampleRate);

        synthTrack = session.addMidiTrack("Keys");
        clip = session.addMidiClip(synthTrack, 0, 2 * bar);
        session.setNotes(clip,
                         {{0, 0, sixteenth * 4, 60, 100},
                          {0, sixteenth * 4 + model::ticksPerSecond / 100, sixteenth * 2, 64, 70},
                          {0, sixteenth * 8, sixteenth * 8, 67, 120},
                          {0, bar, bar / 2, 72, 40}},
                         "Notes");
        session.setControllerEvents(clip, model::ControllerType::controlChange(1),
                                    {{model::ControllerType::controlChange(1), 0, 0},
                                     {model::ControllerType::controlChange(1), bar / 2, 90},
                                     {model::ControllerType::controlChange(1), bar, 30}},
                                    "Mod Wheel");
        session.setControllerEvents(clip, model::ControllerType::pitchBend(),
                                    {{model::ControllerType::pitchBend(), bar / 4, 12000},
                                     {model::ControllerType::pitchBend(), bar / 2, 8192}},
                                    "Pitch Bend");

        const auto samplerTrack = session.addMidiTrack("Sampler", engine::fx::Sampler::typeId);
        sampler = instrumentOf(samplerTrack);
        session.setInstrumentFile(sampler, engine::fx::Sampler::sampleSlot,
                                  writeTone(directory, "tone.wav", 440.0));

        const auto drumTrack = session.addMidiTrack("Drums", engine::fx::DrumSampler::typeId);
        drums = instrumentOf(drumTrack);
        session.setInstrumentFile(drums, engine::fx::DrumSampler::slotOf(0),
                                  writeTone(directory, "kick.wav", 55.0));
        session.setInstrumentFile(drums, engine::fx::DrumSampler::slotOf(2),
                                  writeTone(directory, "snare.wav", 220.0));
        REQUIRE(session.waitForLoads(30000));
        synth = instrumentOf(synthTrack);
    }

    [[nodiscard]] model::ObjectId instrumentOf(model::ObjectId trackId) const
    {
        const auto& project = session.getProject();
        const auto instrument = project.getTrack(project.indexOfTrack(trackId)).getInstrument();
        REQUIRE(instrument.has_value());
        return instrument->getId();
    }

    [[nodiscard]] std::vector<model::Note> notes() const { return session.findMidiClip(clip)->getNotes(); }

    engine::Engine engine;
    controller::Session session{engine, {engine::io::DiskStreamer::Mode::manual}};
    model::ObjectId synthTrack = 0;
    model::ObjectId clip = 0;
    model::ObjectId synth = 0;
    model::ObjectId sampler = 0;
    model::ObjectId drums = 0;
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

    [[nodiscard]] bool isTicked(juce::CommandID command)
    {
        juce::ApplicationCommandInfo info(command);
        view.getCommandInfo(command, info);
        return (info.flags & juce::ApplicationCommandInfo::isTicked) != 0;
    }

    ThemeManager themes{{}};
    juce::AudioDeviceManager devices;
    juce::ApplicationCommandManager commands;
    MainComponent view;
};

std::vector<int> keysOf(const std::vector<model::Note>& notes)
{
    std::vector<int> keys;
    for (const auto& note : notes)
        keys.push_back(note.key);
    return keys;
}
} // namespace

TEST_CASE("New MIDI Track and Duplicate add a track with the synth and copy clips", "[ui]")
{
    engine::Engine engine;
    controller::Session session{engine, {engine::io::DiskStreamer::Mode::manual}};
    Desktop desktop(session);

    juce::ApplicationCommandInfo info(MainComponent::newMidiTrack);
    desktop.view.getCommandInfo(MainComponent::newMidiTrack, info);
    REQUIRE(!info.defaultKeypresses.isEmpty());
    CHECK(info.defaultKeypresses[0] ==
          juce::KeyPress('t', juce::ModifierKeys::commandModifier | juce::ModifierKeys::altModifier, 0));

    REQUIRE(desktop.invoke(MainComponent::newMidiTrack));
    const auto& project = session.getProject();
    REQUIRE(project.getNumTracks() == 1);
    const auto track = project.getTrack(0);
    CHECK(track.isMidi());
    REQUIRE(track.getInstrument().has_value());
    CHECK(track.getInstrument()->getType() == controller::Session::defaultInstrument);

    const auto clip = session.addMidiClip(track.getId(), 0, bar);
    desktop.view.getArrangeViewForTesting().getStateForTesting().selection = {clip};
    REQUIRE(desktop.invoke(MainComponent::duplicate));
    REQUIRE(project.getTrack(0).getNumClips() == 2);
    CHECK(project.getTrack(0).getClip(1).getPosition() == bar);
    CHECK(project.getTrack(0).getClip(1).isMidi());
}

TEST_CASE("The piano roll opens a clip and edits its notes one undo step at a time", "[ui]")
{
    test::TemporaryDirectory directory;
    MidiStudio studio(directory);
    Desktop desktop(studio.session);
    auto& roll = desktop.view.getPianoRollForTesting();

    CHECK_FALSE(desktop.isTicked(MainComponent::togglePianoRoll));
    desktop.view.editClip(studio.clip);
    CHECK(desktop.isTicked(MainComponent::togglePianoRoll));
    CHECK(roll.getClip() == studio.clip);
    CHECK(roll.isVisible());

    // Q: the note just off the grid moves onto it; the others are on it already.
    const auto before = studio.notes();
    REQUIRE(desktop.invoke(MainComponent::quantiseNotes));
    auto after = studio.notes();
    REQUIRE(after.size() == before.size());
    CHECK(after[1].start == sixteenth * 4);
    CHECK(studio.session.getUndoDescription() == "Quantise");
    studio.session.undo();
    CHECK(studio.notes() == before);

    // Up a semitone, then down an octave (Shift), the selection only.
    roll.selectAll();
    CHECK(roll.hasSelection());
    REQUIRE(roll.keyPressed(juce::KeyPress(juce::KeyPress::upKey)));
    CHECK(keysOf(studio.notes()) == std::vector<int>{61, 65, 68, 73});
    REQUIRE(roll.keyPressed(juce::KeyPress(juce::KeyPress::downKey, juce::ModifierKeys::shiftModifier, 0)));
    CHECK(keysOf(studio.notes()) == std::vector<int>{49, 53, 56, 61});
    studio.session.undo();
    studio.session.undo();
    CHECK(studio.notes() == before);

    // Cmd+D copies the selected notes right after them; Backspace deletes them.
    REQUIRE(roll.keyPressed(juce::KeyPress('D', juce::ModifierKeys::commandModifier, 0)));
    CHECK(studio.notes().size() == 2 * before.size());
    REQUIRE(roll.keyPressed(juce::KeyPress(juce::KeyPress::backspaceKey)));
    CHECK(studio.notes() == before);

    // E hides it again; the clip stays for next time.
    REQUIRE(desktop.invoke(MainComponent::togglePianoRoll));
    CHECK_FALSE(desktop.isTicked(MainComponent::togglePianoRoll));
    CHECK(roll.getClip() == studio.clip);

    // A clip that goes away leaves the piano roll empty.
    studio.session.deleteClips({studio.clip});
    roll.refresh();
    CHECK(roll.getClip() == 0);
}

TEST_CASE("Every theme paints MIDI clips, the piano roll and the instrument editors", "[ui]")
{
    test::TemporaryDirectory directory;
    MidiStudio studio(directory);
    Desktop desktop(studio.session);
    desktop.view.editClip(studio.clip);

    for (int index = 0; index < 3; ++index)
    {
        REQUIRE(desktop.invoke(MainComponent::firstTheme + index));
        const auto& id = desktop.themes.getTheme().id;

        if (!desktop.isTicked(MainComponent::togglePianoRoll))
            REQUIRE(desktop.invoke(MainComponent::togglePianoRoll));
        const auto image = desktop.view.createComponentSnapshot(desktop.view.getLocalBounds());
        CHECK(image.isValid());
        writeSnapshot(desktop.view, "midi-window-" + id);

        // X puts the mixer (with the instrument slots) in the piano roll's place.
        REQUIRE(desktop.invoke(MainComponent::toggleMixer));
        CHECK(desktop.isTicked(MainComponent::toggleMixer));
        CHECK_FALSE(desktop.isTicked(MainComponent::togglePianoRoll));
        CHECK(desktop.view.createComponentSnapshot(desktop.view.getLocalBounds()).isValid());
        writeSnapshot(desktop.view, "midi-mixer-" + id);

        for (const auto& [effect, name] : {std::pair{studio.synth, "synth"}, std::pair{studio.sampler, "sampler"},
                                           std::pair{studio.drums, "drums"}})
        {
            ui::desktop::EffectEditorWindow editor(studio.session, effect);
            editor.refresh(0.05);
            auto* content = editor.getContentComponent();
            REQUIRE(content != nullptr);
            CHECK(content->getHeight() > 200);
            CHECK(content->createComponentSnapshot(content->getLocalBounds()).isValid());
            writeSnapshot(*content, "editor-" + id + "-" + name);
        }
    }
}
