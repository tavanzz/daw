#include "daw/model/FormatVersion.h"
#include "daw/model/ProjectFile.h"

#include "support/TestAudioFiles.h"

#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_string.hpp>

#include <array>

using Catch::Matchers::ContainsSubstring;
using daw::model::FormatMigration;
using daw::model::loadProject;
using daw::model::Project;
using daw::model::saveProject;
using daw::test::TemporaryDirectory;

namespace
{

Project makeSampleProject(const juce::File& audioFile)
{
    Project project;
    project.setLoopEnabled(true);
    project.addTrack("Track 1").addClip(audioFile);
    return project;
}

juce::String errorFor(const juce::String& json)
{
    return daw::model::fromJson(json, juce::File::getCurrentWorkingDirectory()).error;
}

} // namespace

TEST_CASE("The file format is exactly the documented JSON", "[model][file]")
{
    // A golden file: if this changes, the format changed. Bump currentFormatVersion
    // and add a migration, then update this text.
    const auto expected = R"({
  "format": "daw-project",
  "formatVersion": 1,
  "project": {
    "loopEnabled": true,
    "tracks": [
      {
        "name": "Track 1",
        "clips": [
          {
            "audioFile": "audio/drums.wav"
          }
        ]
      }
    ]
  }
}
)";

    TemporaryDirectory directory;
    const auto file = directory.file("song.dawproject");
    REQUIRE(saveProject(makeSampleProject(directory.file("audio/drums.wav")), file).wasOk());

    juce::MemoryBlock bytes;
    REQUIRE(file.loadFileAsData(bytes));
    CHECK(bytes.toString().toStdString() == expected);
}

TEST_CASE("A saved project loads back identical", "[model][file]")
{
    TemporaryDirectory directory;
    Project project;
    project.setLoopEnabled(true);
    auto drums = project.addTrack("Drums");
    drums.addClip(directory.file("audio/kick.wav"));
    drums.addClip(directory.file("audio/snare.aiff"));
    project.addTrack("Empty");
    project.addTrack(juce::CharPointer_UTF8("Voce \xc3\xa8 bella")).addClip(directory.file("v.wav"));

    const auto file = directory.file(juce::String("song") + daw::model::projectFileExtension);
    REQUIRE(saveProject(project, file).wasOk());

    const auto loaded = loadProject(file);

    CAPTURE(loaded.error);
    REQUIRE(loaded.succeeded());
    CHECK(loaded.fileFormatVersion == daw::model::currentFormatVersion);
    CHECK(loaded.project->isEquivalentTo(project));
}

TEST_CASE("Audio inside the project folder is stored relative to it", "[model][file]")
{
    TemporaryDirectory directory;
    const auto inside = directory.file("audio/drums.wav");
    const auto outside = juce::File::getSpecialLocation(juce::File::tempDirectory)
                             .getChildFile("elsewhere")
                             .getChildFile("bass.wav");

    Project project;
    auto track = project.addTrack("Track 1");
    track.addClip(inside);
    track.addClip(outside);

    const auto json = daw::model::toJson(project, directory.file(""));

    CHECK_THAT(json.toStdString(), ContainsSubstring("\"audioFile\": \"audio/drums.wav\""));
    CHECK_THAT(json.toStdString(), ContainsSubstring(outside.getFullPathName().replace("\\", "\\\\").toStdString()));
}

TEST_CASE("A project folder can be moved with its audio", "[model][file]")
{
    TemporaryDirectory first;
    TemporaryDirectory second;
    const auto file = first.file("song.dawproject");
    REQUIRE(saveProject(makeSampleProject(first.file("audio/drums.wav")), file).wasOk());

    const auto movedFile = second.file("song.dawproject");
    REQUIRE(file.copyFileTo(movedFile));

    const auto loaded = loadProject(movedFile);
    REQUIRE(loaded.succeeded());
    CHECK(loaded.project->getTrack(0).getClip(0).getAudioFile() == second.file("audio/drums.wav"));
}

TEST_CASE("Saving replaces an existing project file", "[model][file]")
{
    TemporaryDirectory directory;
    const auto file = directory.file("song.dawproject");
    REQUIRE(file.replaceWithText("old contents"));

    REQUIRE(saveProject(makeSampleProject(directory.file("a.wav")), file).wasOk());
    CHECK(loadProject(file).succeeded());
}

TEST_CASE("Saving into a missing folder fails with a message", "[model][file]")
{
    TemporaryDirectory directory;
    const auto result = saveProject(Project(), directory.file("missing/song.dawproject"));

    CHECK(result.failed());
    CHECK_THAT(result.getErrorMessage().toStdString(),
               ContainsSubstring("Could not save \"song.dawproject\""));
}

TEST_CASE("Files that are not readable projects are refused with a reason", "[model][file]")
{
    CHECK_THAT(errorFor("").toStdString(), ContainsSubstring("not valid JSON"));
    CHECK_THAT(errorFor("{ broken").toStdString(), ContainsSubstring("not valid JSON"));
    CHECK_THAT(errorFor("[1, 2]").toStdString(), ContainsSubstring("not a project file"));
    CHECK_THAT(errorFor(R"({"formatVersion": 1})").toStdString(),
               ContainsSubstring("not a project file"));
    CHECK_THAT(errorFor(R"({"format": "something-else", "formatVersion": 1})").toStdString(),
               ContainsSubstring("not a project file"));
    CHECK_THAT(errorFor(R"({"format": "daw-project"})").toStdString(),
               ContainsSubstring("no valid format version"));
    CHECK_THAT(errorFor(R"({"format": "daw-project", "formatVersion": 0})").toStdString(),
               ContainsSubstring("no valid format version"));
    CHECK_THAT(errorFor(R"({"format": "daw-project", "formatVersion": 1.5})").toStdString(),
               ContainsSubstring("no valid format version"));
    CHECK_THAT(errorFor(R"({"format": "daw-project", "formatVersion": 1})").toStdString(),
               ContainsSubstring("damaged (no project section)"));
    CHECK_THAT(
        errorFor(R"({"format": "daw-project", "formatVersion": 1, "project": {"tracks": 3}})")
            .toStdString(),
        ContainsSubstring("tracks is not a list"));
    CHECK_THAT(errorFor(R"({"format": "daw-project", "formatVersion": 1,
                            "project": {"tracks": [{"clips": [{"audioFile": 7}]}]}})")
                   .toStdString(),
               ContainsSubstring("a clip has no audio file"));
}

TEST_CASE("Projects from a newer version are refused, not half-read", "[model][file]")
{
    const auto newer = daw::model::currentFormatVersion + 1;
    const auto loaded = daw::model::fromJson(
        R"({"format": "daw-project", "formatVersion": )" + juce::String(newer) +
            R"(, "project": {"tracks": []}})",
        juce::File());

    CHECK_FALSE(loaded.succeeded());
    CHECK(loaded.fileFormatVersion == newer);
    CHECK_THAT(loaded.error.toStdString(), ContainsSubstring("saved by a newer version"));
    CHECK_THAT(loaded.error.toStdString(), ContainsSubstring("Update the app"));
}

TEST_CASE("Opening a missing project file names it", "[model][file]")
{
    TemporaryDirectory directory;
    const auto loaded = loadProject(directory.file("gone.dawproject"));

    CHECK_FALSE(loaded.succeeded());
    CHECK_THAT(loaded.error.toStdString(),
               ContainsSubstring("Could not open \"gone.dawproject\": the file does not exist"));
}

TEST_CASE("Unknown fields are ignored", "[model][file]")
{
    const auto loaded = daw::model::fromJson(
        R"({"format": "daw-project", "formatVersion": 1, "futureThing": [1, 2],
            "project": {"loopEnabled": true, "colour": "red",
                        "tracks": [{"name": "A", "clips": [], "volume": 0.5}]}})",
        juce::File());

    REQUIRE(loaded.succeeded());
    CHECK(loaded.project->isLoopEnabled());
    CHECK(loaded.project->getTrack(0).getName() == "A");
}

namespace
{
// A pretend history of the format: version 1 called the loop flag "repeat", version 2
// renamed it, version 3 added a track list where version 2 had a single track.
juce::var renameRepeatToLoopEnabled(juce::var json)
{
    auto* project = json["project"].getDynamicObject();
    project->setProperty("loopEnabled", project->getProperty("repeat"));
    project->removeProperty("repeat");
    return json;
}

juce::var wrapSingleTrackInList(juce::var json)
{
    auto* project = json["project"].getDynamicObject();
    juce::Array<juce::var> tracks;
    tracks.add(project->getProperty("track"));
    project->setProperty("tracks", tracks);
    project->removeProperty("track");
    return json;
}
} // namespace

TEST_CASE("Older files are upgraded through every migration in order", "[model][file]")
{
    const std::array<FormatMigration, 2> history{renameRepeatToLoopEnabled, wrapSingleTrackInList};
    const auto versionOne = R"({"format": "daw-project", "formatVersion": 1,
                                "project": {"repeat": true, "track": {"name": "Old"}}})";

    const auto loaded = daw::model::detail::fromJson(versionOne, juce::File(), history);

    CAPTURE(loaded.error);
    REQUIRE(loaded.succeeded());
    CHECK(loaded.fileFormatVersion == 1);
    CHECK(loaded.project->isLoopEnabled());
    REQUIRE(loaded.project->getNumTracks() == 1);
    CHECK(loaded.project->getTrack(0).getName() == "Old");

    // A version 2 file only goes through the second migration.
    const auto versionTwo = R"({"format": "daw-project", "formatVersion": 2,
                                "project": {"loopEnabled": false, "track": {"name": "Mid"}}})";
    const auto fromTwo = daw::model::detail::fromJson(versionTwo, juce::File(), history);
    REQUIRE(fromTwo.succeeded());
    CHECK(fromTwo.project->getTrack(0).getName() == "Mid");

    // And version 4 is newer than this pretend build understands.
    const auto versionFour = R"({"format": "daw-project", "formatVersion": 4, "project": {}})";
    CHECK_FALSE(daw::model::detail::fromJson(versionFour, juce::File(), history).succeeded());
}
