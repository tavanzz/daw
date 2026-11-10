#include "RandomProjectEdits.h"

#include "daw/model/MidiClip.h"
#include "daw/model/ProjectChanges.h"

#include <array>

namespace daw::test
{

namespace
{

const std::array<const char*, 4> effectTypes{"equaliser", "compressor", "delay", "reverb"};
const std::array<const char*, 3> parameterIds{"gain", "mix", "time"};
const std::array<const char*, 4> fileNames{"kick", "snare", "bass take", "voice"};

std::vector<model::Note> randomNotes(juce::Random& random)
{
    std::vector<model::Note> notes;
    const int count = random.nextInt(12);
    for (int index = 0; index < count; ++index)
    {
        model::Note note;
        note.id = index + 1;
        note.start = random.nextInt(4) * 70'560'000LL + random.nextInt(1000);
        note.length = 1 + random.nextInt(70'560'000);
        note.key = 36 + random.nextInt(48);
        note.velocity = 1 + random.nextInt(127);
        notes.push_back(note);
    }
    return notes;
}

std::vector<model::ControllerEvent> randomControllers(juce::Random& random)
{
    std::vector<model::ControllerEvent> events;
    const int count = random.nextInt(6);
    for (int index = 0; index < count; ++index)
    {
        model::ControllerEvent event;
        event.type = random.nextBool() ? model::ControllerType::controlChange(1)
                                       : model::ControllerType::pitchBend();
        event.position = random.nextInt(1'000'000) * 1000LL;
        event.value = random.nextInt(event.type.maximumValue() + 1);
        events.push_back(event);
    }
    return events;
}

juce::String editEffects(juce::Random& random, model::EffectList effects,
                         model::Automation automation, const juce::File& file,
                         juce::UndoManager* undoManager)
{
    const int count = effects.getNumEffects();
    const int choice = random.nextInt(count == 0 ? 1 : 9);

    if (choice == 0)
    {
        effects.add(effectTypes[static_cast<std::size_t>(random.nextInt(4))],
                    random.nextInt(count + 1), {}, undoManager);
        return "add an effect";
    }

    auto effect = effects.getEffect(random.nextInt(count));
    switch (choice)
    {
    case 1:
        effect.setParameter(parameterIds[static_cast<std::size_t>(random.nextInt(3))],
                            random.nextDouble(), undoManager);
        return "set a parameter";
    case 2:
        effect.setBypassed(!effect.isBypassed(), undoManager);
        return "bypass";
    case 3:
        effects.move(effects.indexOf(effect.getId()), random.nextInt(count), undoManager);
        return "move an effect";
    case 4:
        automation.removeLanesOfEffect(effect.getId(), undoManager);
        effects.remove(effects.indexOf(effect.getId()), undoManager);
        return "remove an effect";
    case 5:
    {
        model::PluginReference plugin;
        plugin.format = random.nextBool() ? "VST3" : "AudioUnit";
        plugin.uniqueId = random.nextInt();
        plugin.name = "Plugin " + juce::String(random.nextInt(5));
        plugin.identifier = "/Library/Audio/Plug-Ins/" + plugin.name;
        effect.setPlugin(plugin, undoManager);
        return "set a plugin";
    }
    case 6:
    {
        juce::MemoryBlock state(static_cast<std::size_t>(1 + random.nextInt(200)));
        random.fillBitsRandomly(state.getData(), state.getSize());
        effect.setExtraState(state, undoManager);
        return "set a plugin's state";
    }
    case 7:
        effect.setFile("sample", file, undoManager);
        return "set a sample file";
    default:
    {
        const auto target = model::AutomationTarget::effectParameter(
            effect.getId(), parameterIds[static_cast<std::size_t>(random.nextInt(3))]);
        auto lane = automation.getOrAddLane(target, undoManager);
        lane.addPoint({random.nextInt(100) * 28'224'000LL, random.nextDouble(), 0.0},
                      undoManager);
        return "automate a parameter";
    }
    }
}

juce::String editAutomation(juce::Random& random, model::Automation automation,
                            juce::UndoManager* undoManager)
{
    const int lanes = automation.getNumLanes();
    const int choice = random.nextInt(lanes == 0 ? 2 : 7);

    if (choice == 0)
    {
        auto lane = automation.getOrAddLane(random.nextBool() ? model::AutomationTarget::volume()
                                                              : model::AutomationTarget::pan(),
                                            undoManager);
        lane.addPoint({random.nextInt(100) * 28'224'000LL, random.nextDouble(), 0.0},
                      undoManager);
        return "add an automation point";
    }
    if (choice == 1)
    {
        automation.setMode(automation.getMode() == model::AutomationMode::read
                               ? model::AutomationMode::off
                               : model::AutomationMode::read);
        return "switch automation";
    }

    auto lane = automation.getLane(random.nextInt(lanes));
    const int points = lane.getNumPoints();
    switch (choice)
    {
    case 2:
    case 3:
        lane.addPoint({random.nextInt(100) * 28'224'000LL, random.nextDouble(),
                       random.nextDouble() * 2.0 - 1.0},
                      undoManager);
        return "add an automation point";
    case 4:
        if (points > 0)
            lane.setPoint(random.nextInt(points),
                          {random.nextInt(100) * 28'224'000LL, random.nextDouble(), 0.0},
                          undoManager);
        return "move an automation point";
    case 5:
        if (points > 0)
            lane.removePoint(random.nextInt(points), undoManager);
        return "remove an automation point";
    default:
        automation.removeLane(random.nextInt(lanes), undoManager);
        return "remove a lane";
    }
}

} // namespace

RandomProjectEditor::RandomProjectEditor(juce::int64 seed, juce::File audioFolder)
    : random(seed), folder(std::move(audioFolder))
{
    if (folder == juce::File())
        folder = juce::File::getSpecialLocation(juce::File::tempDirectory)
                     .getChildFile("daw-random-project");
}

bool RandomProjectEditor::allowed(const model::EditScope& scope) const
{
    return !mayEdit || mayEdit(scope);
}

juce::File RandomProjectEditor::randomFile()
{
    return folder.getChildFile("Audio")
        .getChildFile(juce::String(fileNames[static_cast<std::size_t>(random.nextInt(4))])
                      + " " + juce::String(random.nextInt(3)) + ".wav");
}

juce::String RandomProjectEditor::edit(model::Project& project, juce::UndoManager* undoManager)
{
    if (undoManager != nullptr && !mayEdit && undoOneIn > 0 && random.nextInt(undoOneIn) == 0)
    {
        if (random.nextBool() && undoManager->canUndo())
        {
            undoManager->undo();
            return "undo";
        }
        if (undoManager->canRedo())
        {
            undoManager->redo();
            return "redo";
        }
    }

    if (undoManager != nullptr)
        undoManager->beginNewTransaction();

    const int area = random.nextInt(100);
    if (area < 15 || project.getNumTracks() == 0)
        return editProject(project, undoManager);
    if (area < 75)
        return editTrack(project, random.nextInt(project.getNumTracks()), undoManager);
    if (area < 88 && project.getNumBuses() > 0)
        return editBus(project, random.nextInt(project.getNumBuses()), undoManager);
    return editMaster(project, undoManager);
}

juce::String RandomProjectEditor::editProject(model::Project& project,
                                              juce::UndoManager* undoManager)
{
    const int choice = randomInt(12);

    if (choice >= 6 && choice <= 8)
    {
        // Personal settings: anyone, any time.
        switch (choice)
        {
        case 6:
            project.setSnapEnabled(!project.isSnapEnabled());
            return "switch snap";
        case 7:
            project.setMetronomeEnabled(!project.isMetronomeEnabled());
            return "switch the metronome";
        default:
            project.setGridDivision(random.nextBool() ? model::GridDivision::bar
                                                      : model::GridDivision::sixteenth);
            return "change the grid";
        }
    }

    if (!allowed(model::EditScope::project()))
        return "nothing";

    switch (choice)
    {
    case 0:
    case 1:
        project.addTrack("Track " + juce::String(random.nextInt(100)),
                         random.nextInt(3) == 0 ? model::TrackKind::midi
                                                : model::TrackKind::audio,
                         undoManager);
        return "add a track";
    case 2:
        if (project.getNumTracks() > 0)
        {
            const int index = random.nextInt(project.getNumTracks());
            if (!allowed(model::EditScope::track(project.getTrack(index).getId())))
                return "nothing";
            project.removeTrack(index, undoManager);
            return "remove a track";
        }
        return "nothing";
    case 3:
        project.setTempo(60.0 + random.nextInt(120) + random.nextDouble(), undoManager);
        return "change the tempo";
    case 4:
        project.setLoopRange(random.nextInt(100) * 28'224'000LL,
                             random.nextInt(100) * 28'224'000LL, undoManager);
        return "change the loop";
    case 5:
        project.setLoopEnabled(!project.isLoopEnabled());
        return "switch the loop";
    case 9:
        project.addBus("Bus " + juce::String(random.nextInt(100)), undoManager);
        return "add a bus";
    case 10:
    {
        if (project.getNumBuses() == 0)
            return "nothing";
        const int index = random.nextInt(project.getNumBuses());
        const auto busId = project.getBus(index).getId();
        if (!allowed(model::EditScope::bus(busId)))
            return "nothing";
        for (int track = 0; track < project.getNumTracks(); ++track)
            if (project.getTrack(track).indexOfSendTo(busId) >= 0
                && !allowed(model::EditScope::track(project.getTrack(track).getId())))
                return "nothing";
        project.removeBus(index, undoManager);
        return "remove a bus";
    }
    default:
        project.setTimeSignature({3 + random.nextInt(3), random.nextBool() ? 4 : 8},
                                 undoManager);
        return "change the metre";
    }
}

juce::String RandomProjectEditor::editTrack(model::Project& project, int trackIndex,
                                            juce::UndoManager* undoManager)
{
    auto track = project.getTrack(trackIndex);
    const int choice = randomInt(20);

    if (choice < 3)
    {
        switch (choice)
        {
        case 0:
            track.setArmed(!track.isArmed());
            return "arm";
        case 1:
            track.setSoloed(!track.isSoloed(), undoManager);
            return "solo";
        default:
            track.setInput({random.nextInt(8), random.nextBool()}, undoManager);
            return "change the input";
        }
    }

    if (!allowed(model::EditScope::track(track.getId())))
        return "nothing";

    const int clips = track.getNumClips();
    switch (choice)
    {
    case 3:
        track.setName("Track " + juce::String(random.nextInt(100)), undoManager);
        return "rename a track";
    case 4:
        track.setVolume(randomUnit() * 2.0, undoManager);
        return "change a volume";
    case 5:
        track.setPan(randomUnit() * 2.0 - 1.0, undoManager);
        return "change a pan";
    case 6:
        track.setMuted(!track.isMuted(), undoManager);
        return "mute";
    case 7:
    case 8:
    {
        const model::ClipBounds bounds{random.nextInt(200) * 14'112'000LL,
                                       random.nextInt(10) * 1'000'000LL,
                                       (1 + random.nextInt(100)) * 7'056'000LL};
        if (track.isMidi())
            track.addMidiClip("Clip", bounds, randomNotes(random), randomControllers(random),
                              undoManager);
        else
            track.addClip(randomFile(), bounds, undoManager);
        return "add a clip";
    }
    case 9:
    case 10:
    {
        if (clips == 0)
            return "nothing";
        auto clip = track.getClip(random.nextInt(clips));
        if (clip.isMidi() && random.nextBool())
        {
            model::MidiClip midi(clip.getState());
            if (random.nextBool())
                midi.setNotes(randomNotes(random), undoManager);
            else
                midi.setControllerEvents(randomControllers(random), undoManager);
            return "edit notes";
        }
        auto bounds = clip.getBounds();
        bounds.position = random.nextInt(200) * 14'112'000LL;
        if (random.nextBool())
            bounds.length = (1 + random.nextInt(100)) * 7'056'000LL;
        clip.setBounds(bounds, undoManager);
        return "move or trim a clip";
    }
    case 11:
    {
        if (clips == 0)
            return "nothing";
        const auto clip = track.getClip(random.nextInt(clips));
        const int to = random.nextInt(project.getNumTracks());
        const auto other = project.getTrack(to);
        if (other.isMidi() != track.isMidi() || !allowed(model::EditScope::track(other.getId())))
            return "nothing";
        project.moveClip(clip.getId(), to, random.nextInt(200) * 14'112'000LL, undoManager);
        return "move a clip to a track";
    }
    case 12:
        if (clips == 0)
            return "nothing";
        track.removeClip(random.nextInt(clips), undoManager);
        return "remove a clip";
    case 13:
        if (clips == 0)
            return "nothing";
        track.addCopyOf(track.getClip(random.nextInt(clips)), random.nextInt(200) * 14'112'000LL,
                        undoManager);
        return "duplicate a clip";
    case 14:
    case 15:
        return editEffects(random, track.getEffects(), track.getAutomation(), randomFile(),
                           undoManager);
    case 16:
        if (track.isMidi())
        {
            if (random.nextInt(3) == 0)
                track.removeInstrument(undoManager);
            else
                track.setInstrument(random.nextBool() ? "synth" : "sampler",
                                    {{"cutoff", randomUnit()}}, undoManager);
            return "change the instrument";
        }
        return editAutomation(random, track.getAutomation(), undoManager);
    case 17:
    {
        if (project.getNumBuses() == 0)
            return "nothing";
        const auto busId = project.getBus(random.nextInt(project.getNumBuses())).getId();
        if (const int send = track.indexOfSendTo(busId); send >= 0)
        {
            if (random.nextBool())
            {
                track.getAutomation().removeLaneOfSend(track.getSend(send).getId(), undoManager);
                track.removeSend(send, undoManager);
                return "remove a send";
            }
            auto existing = track.getSend(send);
            existing.setLevel(randomUnit() * 2.0, undoManager);
            existing.setPreFader(random.nextBool(), undoManager);
            return "change a send";
        }
        track.addSend(busId, undoManager);
        return "add a send";
    }
    default:
        return editAutomation(random, track.getAutomation(), undoManager);
    }
}

juce::String RandomProjectEditor::editBus(model::Project& project, int busIndex,
                                          juce::UndoManager* undoManager)
{
    auto bus = project.getBus(busIndex);
    if (!allowed(model::EditScope::bus(bus.getId())))
        return "nothing";

    switch (randomInt(6))
    {
    case 0:
        bus.setName("Bus " + juce::String(random.nextInt(100)), undoManager);
        return "rename a bus";
    case 1:
        bus.setVolume(randomUnit() * 2.0, undoManager);
        return "change a bus volume";
    case 2:
        bus.setMuted(!bus.isMuted(), undoManager);
        return "mute a bus";
    case 3:
        return editAutomation(random, bus.getAutomation(), undoManager);
    default:
        return editEffects(random, bus.getEffects(), bus.getAutomation(), randomFile(),
                           undoManager);
    }
}

juce::String RandomProjectEditor::editMaster(model::Project& project,
                                             juce::UndoManager* undoManager)
{
    if (!allowed(model::EditScope::master()))
        return "nothing";

    switch (randomInt(4))
    {
    case 0:
        project.setMasterVolume(randomUnit() * 2.0, undoManager);
        return "change the master volume";
    case 1:
        return editAutomation(random, project.getMasterAutomation(), undoManager);
    default:
        return editEffects(random, project.getMasterEffects(), project.getMasterAutomation(),
                           randomFile(), undoManager);
    }
}

bool isExactlyTheSame(const juce::ValueTree& a, const juce::ValueTree& b)
{
    if (!a.isValid() || !b.isValid())
        return a.isValid() == b.isValid();
    if (a.getType() != b.getType() || a.getNumProperties() != b.getNumProperties()
        || a.getNumChildren() != b.getNumChildren())
        return false;

    for (int index = 0; index < a.getNumProperties(); ++index)
    {
        const auto name = a.getPropertyName(index);
        if (name != b.getPropertyName(index) || !model::isSameValue(a[name], b[name]))
            return false;
    }

    for (int index = 0; index < a.getNumChildren(); ++index)
        if (!isExactlyTheSame(a.getChild(index), b.getChild(index)))
            return false;

    return true;
}

} // namespace daw::test
