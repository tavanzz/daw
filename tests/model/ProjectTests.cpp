#include "daw/model/Project.h"

#include <catch2/catch_test_macros.hpp>

#include <functional>
#include <random>
#include <set>
#include <vector>

using daw::model::ClipBounds;
using daw::model::GridDivision;
using daw::model::ObjectId;
using daw::model::Project;
using daw::model::Ticks;
using daw::model::Track;

namespace
{

juce::File audioFile(const juce::String& name)
{
    return juce::File::getSpecialLocation(juce::File::tempDirectory).getChildFile(name);
}

// Stricter than ValueTree::isEquivalentTo, which treats doubles within an epsilon
// as equal: same types, same properties with bit-identical values, same children
// in the same order.
bool identical(const juce::ValueTree& a, const juce::ValueTree& b)
{
    if (a.getType() != b.getType() || a.getNumProperties() != b.getNumProperties() ||
        a.getNumChildren() != b.getNumChildren())
        return false;

    for (int i = 0; i < a.getNumProperties(); ++i)
    {
        const auto name = a.getPropertyName(i);
        if (!b.hasProperty(name))
            return false;

        const auto& x = a[name];
        const auto& y = b[name];
        if (x.isDouble() && y.isDouble())
        {
            if (!juce::exactlyEqual(static_cast<double>(x), static_cast<double>(y)))
                return false;
        }
        else if (!x.equalsWithSameType(y))
        {
            return false;
        }
    }

    for (int i = 0; i < a.getNumChildren(); ++i)
        if (!identical(a.getChild(i), b.getChild(i)))
            return false;

    return true;
}

std::set<ObjectId> allIds(const Project& project)
{
    std::set<ObjectId> ids;
    for (int t = 0; t < project.getNumTracks(); ++t)
    {
        const auto track = project.getTrack(t);
        ids.insert(track.getId());
        for (int c = 0; c < track.getNumClips(); ++c)
            ids.insert(track.getClip(c).getId());
    }
    return ids;
}

int countObjects(const Project& project)
{
    int count = project.getNumTracks();
    for (int t = 0; t < project.getNumTracks(); ++t)
        count += project.getTrack(t).getNumClips();
    return count;
}

} // namespace

TEST_CASE("A new project has the documented defaults", "[model]")
{
    const Project project;

    CHECK(project.getNumTracks() == 0);
    CHECK(project.getTempoMap() == daw::model::TempoMap(120.0, {4, 4}));
    CHECK_FALSE(project.isLoopEnabled());
    CHECK(project.getLoopStart() == 0);
    CHECK(project.getLoopEnd() == 0);
    CHECK(project.isSnapEnabled());
    CHECK(project.getGridDivision() == GridDivision::automatic);
    CHECK(project.getMasterVolume() == 1.0);
    CHECK(project.getEnd() == 0);
}

TEST_CASE("Tracks and clips can be added, changed and removed", "[model]")
{
    Project project;

    auto track = project.addTrack("Drums");
    track.addClip(audioFile("a.wav"), {100, 20, 300});
    project.addTrack("Bass");

    REQUIRE(project.getNumTracks() == 2);
    CHECK(project.getTrack(0).getName() == "Drums");
    CHECK(project.getTrack(1).getName() == "Bass");
    REQUIRE(project.getTrack(0).getNumClips() == 1);

    const auto clip = project.getTrack(0).getClip(0);
    CHECK(clip.getAudioFile() == audioFile("a.wav"));
    CHECK(clip.getBounds() == ClipBounds{100, 20, 300});
    CHECK(clip.getEnd() == 400);

    // A new track is neutral in the mixer.
    const auto bass = project.getTrack(1);
    CHECK(bass.getVolume() == 1.0);
    CHECK(bass.getPan() == 0.0);
    CHECK_FALSE(bass.isMuted());
    CHECK_FALSE(bass.isSoloed());

    // Handles refer to the project's state, not to copies of it.
    project.getTrack(1).setName("Bass guitar");
    CHECK(project.getTrack(1).getName() == "Bass guitar");

    project.removeTrack(0);
    CHECK(project.getNumTracks() == 1);
    CHECK(project.getTrack(0).getName() == "Bass guitar");
}

TEST_CASE("Tracks and clips get ids no other object of the project has", "[model]")
{
    Project project;
    auto first = project.addTrack("One");
    first.addClip(audioFile("a.wav"));
    first.addClip(audioFile("b.wav"));
    auto second = project.addTrack("Two");
    second.addClip(audioFile("c.wav"));

    const auto ids = allIds(project);
    CHECK(static_cast<int>(ids.size()) == countObjects(project));
    CHECK(ids.count(0) == 0);

    CHECK(project.indexOfTrack(second.getId()) == 1);
    CHECK(project.indexOfTrack(12345) == -1);

    const auto location = project.findClip(first.getClip(1).getId());
    REQUIRE(location.has_value());
    CHECK(location->trackIndex == 0);
    CHECK(location->clipIndex == 1);
}

TEST_CASE("Mixer settings and clip bounds are kept in range", "[model]")
{
    Project project;
    auto track = project.addTrack("T");

    track.setVolume(5.0);
    CHECK(track.getVolume() == Track::maximumVolume);
    track.setVolume(-1.0);
    CHECK(track.getVolume() == 0.0);
    track.setVolume(std::numeric_limits<double>::quiet_NaN());
    CHECK(track.getVolume() == 1.0);

    track.setPan(-3.0);
    CHECK(track.getPan() == -1.0);
    track.setPan(2.0);
    CHECK(track.getPan() == 1.0);

    project.setMasterVolume(10.0);
    CHECK(project.getMasterVolume() == Track::maximumVolume);

    auto clip = track.addClip(audioFile("a.wav"), {-5, -7, 0});
    CHECK(clip.getBounds() == ClipBounds{0, 0, daw::model::Clip::minimumLength});
}

TEST_CASE("Mute and solo decide which tracks are heard", "[model]")
{
    Project project;
    for (int i = 0; i < 4; ++i)
        project.addTrack("T" + juce::String(i));

    const auto audible = [&project] {
        std::vector<bool> result;
        for (int i = 0; i < project.getNumTracks(); ++i)
            result.push_back(project.isTrackAudible(i));
        return result;
    };

    CHECK(audible() == std::vector<bool>{true, true, true, true});

    project.getTrack(1).setMuted(true);
    CHECK(audible() == std::vector<bool>{true, false, true, true});

    // Solo: only soloed tracks are heard.
    project.getTrack(2).setSoloed(true);
    CHECK(project.isAnyTrackSoloed());
    CHECK(audible() == std::vector<bool>{false, false, true, false});

    project.getTrack(3).setSoloed(true);
    CHECK(audible() == std::vector<bool>{false, false, true, true});

    // Mute wins over solo.
    project.getTrack(1).setSoloed(true);
    CHECK(audible() == std::vector<bool>{false, false, true, true});
    project.getTrack(3).setMuted(true);
    CHECK(audible() == std::vector<bool>{false, false, true, false});

    // With every solo released, all unmuted tracks are heard again.
    for (int i = 0; i < 4; ++i)
        project.getTrack(i).setSoloed(false);
    CHECK_FALSE(project.isAnyTrackSoloed());
    CHECK(audible() == std::vector<bool>{true, false, true, false});
}

TEST_CASE("Moving a clip puts it on top of the destination track and keeps its id", "[model]")
{
    Project project;
    auto first = project.addTrack("One");
    const auto a = first.addClip(audioFile("a.wav"), {0, 0, 100}).getId();
    const auto b = first.addClip(audioFile("b.wav"), {50, 0, 100}).getId();
    auto second = project.addTrack("Two");
    const auto c = second.addClip(audioFile("c.wav"), {0, 0, 100}).getId();

    // Within the same track: a goes above b.
    project.moveClip(a, 0, 10);
    CHECK(first.getClip(0).getId() == b);
    CHECK(first.getClip(1).getId() == a);
    CHECK(first.getClip(1).getPosition() == 10);

    // To another track: on top of c.
    project.moveClip(b, 1, 500);
    CHECK(first.getNumClips() == 1);
    REQUIRE(second.getNumClips() == 2);
    CHECK(second.getClip(0).getId() == c);
    CHECK(second.getClip(1).getId() == b);
    CHECK(second.getClip(1).getBounds() == ClipBounds{500, 0, 100});

    CHECK(project.getEnd() == 600);

    project.removeClip(b);
    CHECK(second.getNumClips() == 1);
    CHECK_FALSE(project.findClip(b).has_value());
}

TEST_CASE("A clip of unknown length gets one when its file has been read", "[model]")
{
    Project project;
    auto clip = project.addTrack("T").addClip(audioFile("a.wav"), {1000, 300, std::nullopt});

    CHECK_FALSE(clip.getLength().has_value());
    CHECK_FALSE(clip.getEnd().has_value());
    CHECK(project.getEnd() == 1000); // counts up to its start

    clip.resolveLength(5000);
    CHECK(clip.getLength() == 4700); // the file after the offset

    clip.resolveLength(9999); // already known: unchanged
    CHECK(clip.getLength() == 4700);
}

TEST_CASE("The loop region is ordered and never negative", "[model]")
{
    Project project;

    project.setLoopRange(900, 300);
    CHECK(project.getLoopStart() == 300);
    CHECK(project.getLoopEnd() == 900);

    project.setLoopRange(-50, 100);
    CHECK(project.getLoopStart() == 0);
    CHECK(project.getLoopEnd() == 100);
}

TEST_CASE("Tempo, metre and grid settings are stored", "[model]")
{
    Project project;
    project.setTempo(133.5);
    project.setTimeSignature({7, 8});
    project.setGridDivision(GridDivision::sixteenth);
    project.setSnapEnabled(false);

    CHECK(project.getTempoMap() == daw::model::TempoMap(133.5, {7, 8}));
    CHECK(project.getGridDivision() == GridDivision::sixteenth);
    CHECK_FALSE(project.isSnapEnabled());

    project.setTempo(5000.0);
    CHECK(project.getTempoMap().getTempo() == daw::model::TempoMap::maximumTempo);

    for (const auto division : {GridDivision::automatic, GridDivision::bar, GridDivision::half,
                                GridDivision::quarter, GridDivision::eighth,
                                GridDivision::sixteenth, GridDivision::thirtySecond})
        CHECK(daw::model::gridDivisionFromString(daw::model::toString(division)) == division);

    CHECK_FALSE(daw::model::gridDivisionFromString("1/3").has_value());
}

TEST_CASE("Loop on/off, snap and grid are not part of the undo history", "[model][undo]")
{
    Project project;
    juce::UndoManager undoManager;

    undoManager.beginNewTransaction();
    project.setLoopEnabled(true);
    project.setSnapEnabled(false);
    project.setGridDivision(GridDivision::bar);

    CHECK_FALSE(undoManager.canUndo());
}

TEST_CASE("Every editing operation can be undone and redone", "[model][undo]")
{
    Project project;
    juce::UndoManager undoManager;
    auto track = project.addTrack("T");
    const auto clipId = track.addClip(audioFile("a.wav"), {0, 0, 1000}).getId();
    project.addTrack("U");

    const std::vector<std::pair<const char*, std::function<void()>>> edits{
        {"rename", [&] { project.getTrack(0).setName("Renamed", &undoManager); }},
        {"volume", [&] { project.getTrack(0).setVolume(0.5, &undoManager); }},
        {"pan", [&] { project.getTrack(0).setPan(-0.25, &undoManager); }},
        {"mute", [&] { project.getTrack(0).setMuted(true, &undoManager); }},
        {"solo", [&] { project.getTrack(0).setSoloed(true, &undoManager); }},
        {"add clip",
         [&] { project.getTrack(1).addClip(audioFile("b.wav"), {5, 0, 10}, &undoManager); }},
        {"move clip", [&] { project.moveClip(clipId, 1, 777, &undoManager); }},
        {"trim clip", [&] {
             const auto location = project.findClip(clipId);
             project.getTrack(location->trackIndex)
                 .getClip(location->clipIndex)
                 .setBounds({100, 100, 400}, &undoManager);
         }},
        {"remove clip", [&] { project.removeClip(clipId, &undoManager); }},
        {"add track", [&] { project.addTrack("New", &undoManager); }},
        {"remove track", [&] { project.removeTrack(0, &undoManager); }},
        {"tempo", [&] { project.setTempo(90.0, &undoManager); }},
        {"metre", [&] { project.setTimeSignature({3, 4}, &undoManager); }},
        {"loop region", [&] { project.setLoopRange(10, 20, &undoManager); }},
        {"master", [&] { project.setMasterVolume(0.25, &undoManager); }},
    };

    for (const auto& [name, edit] : edits)
    {
        INFO(name);
        const auto before = project.getState().createCopy();

        undoManager.beginNewTransaction();
        edit();
        const auto after = project.getState().createCopy();
        REQUIRE_FALSE(identical(before, after));

        REQUIRE(undoManager.undo());
        CHECK(identical(project.getState(), before));
        REQUIRE(undoManager.redo());
        CHECK(identical(project.getState(), after));
    }
}

TEST_CASE("Undoing and redoing 200 random edits restores every state exactly", "[model][undo]")
{
    Project project;
    juce::UndoManager undoManager(1'000'000, 1000);
    std::mt19937 random(2026);

    const auto pick = [&random](int count) {
        return std::uniform_int_distribution<int>(0, count - 1)(random);
    };
    const auto anyTicks = [&random] {
        return std::uniform_int_distribution<Ticks>(0, Ticks{1} << 40)(random);
    };
    const auto anyClip = [&]() -> std::optional<ObjectId> {
        std::vector<ObjectId> clips;
        for (int t = 0; t < project.getNumTracks(); ++t)
            for (int c = 0; c < project.getTrack(t).getNumClips(); ++c)
                clips.push_back(project.getTrack(t).getClip(c).getId());
        if (clips.empty())
            return std::nullopt;
        return clips[static_cast<std::size_t>(pick(static_cast<int>(clips.size())))];
    };

    std::vector<juce::ValueTree> states{project.getState().createCopy()};

    while (states.size() < 201)
    {
        undoManager.beginNewTransaction();
        const int tracks = project.getNumTracks();

        switch (tracks == 0 ? 0 : pick(12))
        {
        case 0:
            project.addTrack("Track " + juce::String(pick(100)), &undoManager);
            break;
        case 1:
            project.removeTrack(pick(tracks), &undoManager);
            break;
        case 2:
            project.getTrack(pick(tracks)).setVolume(pick(1000) / 500.0, &undoManager);
            break;
        case 3:
            project.getTrack(pick(tracks)).setPan(pick(201) / 100.0 - 1.0, &undoManager);
            break;
        case 4:
            project.getTrack(pick(tracks)).setMuted(pick(2) == 1, &undoManager);
            project.getTrack(pick(tracks)).setSoloed(pick(2) == 1, &undoManager);
            break;
        case 5:
        case 6:
            project.getTrack(pick(tracks))
                .addClip(audioFile("f" + juce::String(pick(5)) + ".wav"),
                         {anyTicks(), anyTicks(), anyTicks()}, &undoManager);
            break;
        case 7:
            if (const auto clip = anyClip())
                project.moveClip(*clip, pick(tracks), anyTicks(), &undoManager);
            break;
        case 8:
            if (const auto clip = anyClip())
            {
                const auto location = project.findClip(*clip);
                project.getTrack(location->trackIndex)
                    .getClip(location->clipIndex)
                    .setBounds({anyTicks(), anyTicks(), anyTicks()}, &undoManager);
            }
            break;
        case 9:
            if (const auto clip = anyClip())
                project.removeClip(*clip, &undoManager);
            break;
        case 10:
            project.setTempo(20.0 + pick(9000) / 10.0, &undoManager);
            project.setTimeSignature({1 + pick(12), 1 << pick(6)}, &undoManager);
            break;
        default:
            project.setLoopRange(anyTicks(), anyTicks(), &undoManager);
            project.setMasterVolume(pick(1000) / 500.0, &undoManager);
            break;
        }

        // Edits that changed nothing leave no undo step.
        if (undoManager.getUndoDescriptions().size() == static_cast<int>(states.size()))
            states.push_back(project.getState().createCopy());

        // Ids stay unique all the way.
        REQUIRE(static_cast<int>(allIds(project).size()) == countObjects(project));
    }

    for (auto index = states.size() - 1; index > 0; --index)
    {
        REQUIRE(undoManager.undo());
        REQUIRE(identical(project.getState(), states[index - 1]));
    }
    CHECK_FALSE(undoManager.canUndo());

    for (std::size_t index = 1; index < states.size(); ++index)
    {
        REQUIRE(undoManager.redo());
        REQUIRE(identical(project.getState(), states[index]));
    }
    CHECK_FALSE(undoManager.canRedo());
}

TEST_CASE("Equivalent projects compare equal, different ones do not", "[model]")
{
    Project a;
    Project b;
    a.addTrack("One");
    b.addTrack("One");
    CHECK(a.isEquivalentTo(b));

    b.setLoopEnabled(true);
    CHECK_FALSE(a.isEquivalentTo(b));
}
