#include "daw/model/ChangeRecorder.h"
#include "daw/model/MidiClip.h"
#include "daw/model/Project.h"
#include "daw/model/ProjectChanges.h"
#include "daw/model/ProjectFile.h"
#include "daw/model/SharedProject.h"

#include "../support/RandomProjectEdits.h"

#include <catch2/catch_test_macros.hpp>

#include <algorithm>

using namespace daw;

namespace
{

const juce::File copyFolder = juce::File::getSpecialLocation(juce::File::tempDirectory)
                                  .getChildFile("daw-change-tests")
                                  .getChildFile("Copy");

model::Project projectWithSomeOfEverything(juce::UndoManager* undoManager = nullptr)
{
    model::Project project;
    auto audio = project.addTrack("Audio", model::TrackKind::audio, undoManager);
    audio.addClip(copyFolder.getChildFile("Audio/1/take.wav"), {0, 0, 1000}, undoManager);
    audio.getEffects().add("equaliser", 0, {}, undoManager);
    auto midi = project.addTrack("Keys", model::TrackKind::midi, undoManager);
    midi.setInstrument("synth", {}, undoManager);
    midi.addMidiClip("Chords", {0, 0, 5000}, {{1, 0, 100, 60, 100}}, {}, undoManager);
    const auto bus = project.addBus("Reverb", undoManager);
    audio.addSend(bus.getId(), undoManager);
    project.getMasterEffects().add("limiter", 0, {}, undoManager);
    return project;
}

} // namespace

TEST_CASE("Recorded changes replayed on a copy give the same project", "[model][coop]")
{
    for (const juce::int64 seed : {1, 2, 3})
    {
        model::Project project = projectWithSomeOfEverything();
        auto copy = project.getState().createCopy();
        model::ChangeRecorder recorder(project.getState());
        juce::UndoManager undoManager;
        test::RandomProjectEditor editor(seed, copyFolder);

        for (int edit = 0; edit < 1500; ++edit)
        {
            const auto what = editor.edit(project, &undoManager);

            if (edit % 7 == 0 || edit == 1499)
            {
                const auto changes = recorder.takeChanges();
                for (const auto& change : changes)
                {
                    const auto check = model::checkChange(copy, change);
                    INFO("edit " << edit << ": " << what << ": " << check.getErrorMessage());
                    REQUIRE(check.wasOk());
                    REQUIRE(model::applyChanges(copy, {change}).wasOk());
                }
                INFO("edit " << edit << ": " << what);
                REQUIRE(model::isSameTree(copy, project.getState()));
                REQUIRE(test::isExactlyTheSame(recorder.getShadow(), project.getState()));
            }
        }

        // Shared, both are the same project, to the byte of its file.
        const auto json = [](const juce::ValueTree& tree)
        {
            return model::toJson(
                model::Project(model::localCopy(model::sharedCopy(tree, copyFolder), copyFolder)),
                copyFolder);
        };
        CHECK(json(copy) == json(project.getState()));
    }
}

TEST_CASE("Inverted changes bring a project back", "[model][coop]")
{
    model::Project project = projectWithSomeOfEverything();
    model::ChangeRecorder recorder(project.getState());
    juce::UndoManager undoManager;
    test::RandomProjectEditor editor(42, copyFolder);

    for (int round = 0; round < 40; ++round)
    {
        const auto before = project.getState().createCopy();
        for (int edit = 0; edit < 25; ++edit)
            [[maybe_unused]] const auto what = editor.edit(project, &undoManager);

        const auto changes = recorder.takeChanges();
        {
            const model::ChangeRecorder::Pause pause(recorder);
            REQUIRE(model::applyChanges(project.getState(), model::inverted(changes)).wasOk());
        }
        REQUIRE(model::isSameTree(project.getState(), before));
        CHECK_FALSE(recorder.hasChanges());

        // And again forwards.
        {
            const model::ChangeRecorder::Pause pause(recorder);
            REQUIRE(model::applyChanges(project.getState(), changes).wasOk());
        }
        undoManager.clearUndoHistory();
    }
}

TEST_CASE("Changes that do not fit are refused and change nothing", "[model][coop]")
{
    model::Project project = projectWithSomeOfEverything();
    auto other = project.getState().createCopy();
    model::ChangeRecorder recorder(project.getState());

    auto track = project.getTrack(0);
    track.setVolume(0.5);
    track.setName("Renamed");
    const auto changes = recorder.takeChanges();
    REQUIRE(changes.size() == 2);

    // The other copy's track was renamed differently in the meantime.
    model::Track(other.getChild(0)).setName("Elsewhere");
    const auto before = other.createCopy();

    const auto result = model::applyChanges(other, changes);
    CHECK(result.failed());
    CHECK(result.getErrorMessage().contains("change 2 of 2"));
    CHECK(test::isExactlyTheSame(other, before)); // the volume went back too

    SECTION("A child that differs is not removed")
    {
        project.removeTrack(0);
        const auto removal = recorder.takeChanges();
        REQUIRE(removal.size() == 1);
        CHECK(model::applyChanges(before, removal).failed());
    }

    SECTION("A missing node is reported")
    {
        model::ProjectChange change;
        model::PathStep step;
        step.type = juce::Identifier("Track");
        step.key = model::PathStep::Key::id;
        step.id = 999;
        change.path = {step};
        change.property = juce::Identifier("name");
        change.after = juce::var("x");
        CHECK(model::applyChanges(other, {change}).failed());
    }
}

TEST_CASE("Values are compared exactly", "[model][coop]")
{
    CHECK(model::isSameValue(1, 1));
    CHECK_FALSE(model::isSameValue(1, 1.0));
    CHECK_FALSE(model::isSameValue(1, juce::int64{1}));
    CHECK_FALSE(model::isSameValue(0.0, -0.0));
    CHECK_FALSE(model::isSameValue(true, 1));
    CHECK_FALSE(model::isSameValue("a", "A"));
    CHECK(model::isSameValue(juce::var(), juce::var()));
    CHECK_FALSE(model::isSameValue(juce::var(), ""));
    juce::MemoryBlock one("abc", 3);
    juce::MemoryBlock two("abd", 3);
    CHECK(model::isSameValue(one, one));
    CHECK_FALSE(model::isSameValue(one, two));
}

TEST_CASE("Paths find every node of a project", "[model][coop]")
{
    model::Project project = projectWithSomeOfEverything();
    test::RandomProjectEditor editor(7, copyFolder);
    juce::UndoManager undoManager;
    for (int edit = 0; edit < 300; ++edit)
        [[maybe_unused]] const auto what = editor.edit(project, &undoManager);

    std::function<void(const juce::ValueTree&)> check = [&](const juce::ValueTree& node)
    {
        CHECK(model::findNode(project.getState(), model::pathOf(node)) == node);
        for (const auto& child : node)
            check(child);
    };
    check(project.getState());
}

TEST_CASE("Personal settings stay out of what is shared", "[model][coop]")
{
    model::Project project = projectWithSomeOfEverything();
    model::ChangeRecorder recorder(project.getState());
    auto track = project.getTrack(0);

    track.setArmed(true);
    track.setSoloed(true);
    track.setInput({3, true});
    track.setMonitoring(model::MonitoringMode::on);
    project.setSnapEnabled(false);
    project.setMetronomeEnabled(true);
    project.setGridDivision(model::GridDivision::bar);
    CHECK_FALSE(recorder.hasChanges());

    const auto shared = model::sharedCopy(project.getState(), copyFolder);
    CHECK_FALSE(shared.hasProperty(juce::Identifier("snapEnabled")));
    CHECK_FALSE(shared.getChild(0).hasProperty(juce::Identifier("armed")));
    CHECK_FALSE(shared.getChild(0).hasProperty(juce::Identifier("solo")));
    CHECK(shared.getChild(0).hasProperty(juce::Identifier("volume")));

    const model::Project local(model::localCopy(shared, copyFolder));
    CHECK_FALSE(local.getTrack(0).isArmed());
    CHECK_FALSE(local.getTrack(0).isSoloed());
    CHECK(local.getTrack(0).getInput() == model::TrackInput{});
    CHECK(local.getTrack(0).getMonitoring() == model::MonitoringMode::automatic);
    CHECK(local.getTrack(1).getInput().isNone());
    CHECK(local.isSnapEnabled());
    CHECK_FALSE(local.isMetronomeEnabled());
    CHECK(model::isSameTree(local.getState(), project.getState()));

    // A track added with personal settings of its own travels without them.
    auto added = project.addTrack("New");
    added.setArmed(true);
    const auto changes = recorder.takeChanges();
    REQUIRE(changes.size() == 1);
    const auto sharedAdd = model::sharedChange(changes[0], copyFolder);
    REQUIRE(sharedAdd.has_value());
    CHECK_FALSE(sharedAdd->child.hasProperty(juce::Identifier("armed")));

    model::ProjectChange arming;
    arming.path = model::pathOf(added.getState());
    arming.property = juce::Identifier("armed");
    arming.after = true;
    CHECK_FALSE(model::sharedChange(arming, copyFolder).has_value());
}

TEST_CASE("File paths are relative to the copy when shared", "[model][coop]")
{
    model::Project project;
    auto track = project.addTrack("Audio");
    track.addClip(copyFolder.getChildFile("Audio/2/take 1.wav"), {0, 0, 10});
    auto sampler = track.getEffects().add("sampler", 0);
    sampler.setFile("sample", copyFolder.getChildFile("Audio/2/hit.wav"));

    const auto shared = model::sharedCopy(project.getState(), copyFolder);
    const model::Project sharedProject(shared.createCopy());
    CHECK(shared.getChild(0).getChild(0)[juce::Identifier("audioFile")].toString()
          == "Audio/2/take 1.wav");

    const auto elsewhere = copyFolder.getSiblingFile("Elsewhere");
    const model::Project local(model::localCopy(shared, elsewhere));
    CHECK(local.getTrack(0).getClip(0).getAudioFile()
          == elsewhere.getChildFile("Audio/2/take 1.wav"));
    CHECK(local.getTrack(0).getEffects().getEffect(0).getFile("sample")
          == elsewhere.getChildFile("Audio/2/hit.wav"));

    model::ChangeRecorder recorder(project.getState());
    track.getClip(0).setAudioFile(copyFolder.getChildFile("Audio/2/take 2.wav"));
    const auto changes = model::sharedChanges(recorder.takeChanges(), copyFolder);
    REQUIRE(changes.size() == 1);
    CHECK(changes[0].before->toString() == "Audio/2/take 1.wav");
    CHECK(changes[0].after->toString() == "Audio/2/take 2.wav");
    const auto back = model::localChanges(changes, elsewhere);
    CHECK(back[0].after->toString()
          == elsewhere.getChildFile("Audio/2/take 2.wav").getFullPathName());
}

TEST_CASE("Each participant makes ids in a range of their own", "[model][coop]")
{
    model::Project project = projectWithSomeOfEverything();
    auto other = model::Project(project.getState().createCopy());

    model::setIdRange(project.getState(), model::idRangeOfParticipant(3));
    model::setIdRange(other.getState(), model::idRangeOfParticipant(4));
    CHECK(model::getIdRange(project.getState()) == model::idRangeOfParticipant(3));

    const auto first = project.addTrack("A").getId();
    const auto second = project.getTrack(0).addClip(juce::File(), {0, 0, 1}).getId();
    const auto theirs = other.addTrack("B").getId();

    CHECK(first == (juce::int64{3} << 40));
    CHECK(second == first + 1);
    CHECK(theirs == (juce::int64{4} << 40));

    model::setIdRange(project.getState(), std::nullopt);
    CHECK(project.addTrack("C").getId() == second + 1);
    CHECK_FALSE(model::sharedCopy(other.getState(), copyFolder)
                    .hasProperty(juce::Identifier("idRangeFirst")));
}

TEST_CASE("Changes say which locks they need", "[model][coop]")
{
    model::Project project = projectWithSomeOfEverything();
    model::ChangeRecorder recorder(project.getState());
    const auto audio = project.getTrack(0);
    const auto keys = project.getTrack(1);
    const auto bus = project.getBus(0);

    const auto scopesOfEdit = [&](const std::function<void()>& edit)
    {
        edit();
        return model::scopesOf(recorder.takeChanges());
    };
    using model::EditScope;

    CHECK(scopesOfEdit([&] { project.getTrack(0).setVolume(0.3); })
          == std::vector{EditScope::track(audio.getId())});
    CHECK(scopesOfEdit([&] { project.getTrack(1).addMidiClip("x", {0, 0, 10}); })
          == std::vector{EditScope::track(keys.getId())});
    CHECK(scopesOfEdit([&] { project.setTempo(100.0); }) == std::vector{EditScope::project()});
    CHECK(scopesOfEdit([&] { project.setMasterVolume(0.5); }) == std::vector{EditScope::master()});
    CHECK(scopesOfEdit([&] { project.getMasterEffects().add("delay", 0); })
          == std::vector{EditScope::master()});
    CHECK(scopesOfEdit([&] { project.getBus(0).setVolume(0.5); })
          == std::vector{EditScope::bus(bus.getId())});

    const auto added = scopesOfEdit([&] { project.addTrack("New"); });
    REQUIRE(added.size() == 2);
    CHECK(added[0] == EditScope::project());
    CHECK(added[1].kind == EditScope::Kind::track);

    auto second = project.addTrack("Second");
    second.addClip(juce::File(), {0, 0, 10});
    [[maybe_unused]] const auto ignored = recorder.takeChanges();
    const auto moved = scopesOfEdit(
        [&] { project.moveClip(second.getClip(0).getId(), 0, 100); });
    CHECK(moved == std::vector{EditScope::track(audio.getId()), EditScope::track(second.getId())});

    // Removing the bus also removes the audio track's send to it.
    const auto removed = scopesOfEdit([&] { project.removeBus(0); });
    CHECK(removed
          == std::vector{EditScope::project(), EditScope::track(audio.getId()),
                         EditScope::bus(bus.getId())});
}

TEST_CASE("Malformed changes are refused", "[model][coop]")
{
    model::Project project = projectWithSomeOfEverything();
    const auto& root = project.getState();
    const auto trackPath = model::pathOf(root.getChild(0));

    const auto insertion = [](model::NodePath path, int index, juce::ValueTree child)
    {
        model::ProjectChange change;
        change.kind = model::ProjectChange::Kind::insertChild;
        change.path = std::move(path);
        change.index = index;
        change.child = std::move(child);
        return change;
    };

    juce::ValueTree clip("Clip");
    clip.setProperty("id", juce::int64{5000}, nullptr);
    CHECK(model::checkChange(root, insertion({}, 0, clip)).failed()); // a clip in the project
    CHECK(model::checkChange(root, insertion(trackPath, 1, clip)).wasOk());
    CHECK(model::checkChange(root, insertion(trackPath, 3, clip)).failed()); // after containers

    auto taken = clip.createCopy();
    taken.setProperty("id", root.getChild(0)["id"], nullptr);
    CHECK(model::checkChange(root, insertion(trackPath, 0, taken)).failed()); // id taken

    juce::ValueTree withoutId("Clip");
    CHECK(model::checkChange(root, insertion(trackPath, 0, withoutId)).failed());

    juce::ValueTree effects("Effects");
    CHECK(model::checkChange(root, insertion(trackPath, 2, effects)).failed()); // a second one

    model::ProjectChange removal;
    removal.kind = model::ProjectChange::Kind::removeChild;
    removal.path = {};
    removal.index = project.getNumTracks(); // the Buses container
    CHECK(model::checkChange(root, removal).failed());

    model::ProjectChange idChange;
    idChange.path = trackPath;
    idChange.property = "id";
    idChange.after = juce::int64{77};
    CHECK(model::checkChange(root, idChange).failed());

    model::ProjectChange personal;
    personal.path = trackPath;
    personal.property = "armed";
    personal.after = true;
    CHECK(model::checkChange(root, personal).failed());

    model::ProjectChange move;
    move.kind = model::ProjectChange::Kind::moveChild;
    move.path = {};
    move.index = 0;
    move.newIndex = project.getNumTracks(); // among the containers
    CHECK(model::checkChange(root, move).failed());
    move.newIndex = 1;
    CHECK(model::checkChange(root, move).wasOk());
}
