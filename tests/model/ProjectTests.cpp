#include "daw/model/Project.h"

#include <catch2/catch_test_macros.hpp>

using daw::model::Project;

TEST_CASE("A new project is empty with loop off", "[model]")
{
    const Project project;

    CHECK(project.getNumTracks() == 0);
    CHECK_FALSE(project.isLoopEnabled());
}

TEST_CASE("Tracks and clips can be added, changed and removed", "[model]")
{
    Project project;
    const auto audio = juce::File::getSpecialLocation(juce::File::tempDirectory).getChildFile("a.wav");

    auto track = project.addTrack("Drums");
    track.addClip(audio);
    project.addTrack("Bass");
    project.setLoopEnabled(true);

    REQUIRE(project.getNumTracks() == 2);
    CHECK(project.getTrack(0).getName() == "Drums");
    CHECK(project.getTrack(1).getName() == "Bass");
    REQUIRE(project.getTrack(0).getNumClips() == 1);
    CHECK(project.getTrack(0).getClip(0).getAudioFile() == audio);
    CHECK(project.isLoopEnabled());

    // Handles refer to the project's state, not to copies of it.
    project.getTrack(1).setName("Bass guitar");
    CHECK(project.getTrack(1).getName() == "Bass guitar");

    project.removeTrack(0);
    CHECK(project.getNumTracks() == 1);
    CHECK(project.getTrack(0).getName() == "Bass guitar");
}

TEST_CASE("Project changes can be undone through an UndoManager", "[model]")
{
    Project project;
    juce::UndoManager undoManager;

    undoManager.beginNewTransaction();
    project.addTrack("Vocals", &undoManager);
    undoManager.beginNewTransaction();
    project.setLoopEnabled(true, &undoManager);

    CHECK(undoManager.undo());
    CHECK_FALSE(project.isLoopEnabled());
    CHECK(undoManager.undo());
    CHECK(project.getNumTracks() == 0);
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
