#include "daw/model/FormatVersion.h"
#include "daw/model/ProjectFile.h"

#include "support/TestAudioFiles.h"

#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_string.hpp>

#include <array>
#include <set>

using Catch::Matchers::ContainsSubstring;
using daw::model::ClipBounds;
using daw::model::FormatMigration;
using daw::model::GridDivision;
using daw::model::loadProject;
using daw::model::Project;
using daw::model::saveProject;
using daw::model::Ticks;
using daw::model::ticksPerSecond;
using daw::test::TemporaryDirectory;

namespace
{

Project makeSampleProject(const juce::File& audioFile)
{
    Project project;
    project.setTempo(128.0);
    project.setTimeSignature({6, 8});
    project.setLoopEnabled(true);
    project.setLoopRange(ticksPerSecond, ticksPerSecond * 3);
    project.setGridDivision(GridDivision::sixteenth);
    project.setMasterVolume(0.5);

    auto track = project.addTrack("Track 1");
    track.setVolume(0.25);
    track.setPan(-0.5);
    track.setMuted(true);
    track.addClip(audioFile, {ticksPerSecond / 2, 0, ticksPerSecond * 5});
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
  "formatVersion": 2,
  "project": {
    "tempo": 128.0,
    "timeSignature": {
      "numerator": 6,
      "denominator": 8
    },
    "loop": {
      "enabled": true,
      "start": 282240000,
      "end": 846720000
    },
    "snap": {
      "enabled": true,
      "grid": "1/16"
    },
    "master": {
      "volume": 0.5
    },
    "tracks": [
      {
        "id": 1,
        "name": "Track 1",
        "volume": 0.25,
        "pan": -0.5,
        "mute": true,
        "solo": false,
        "clips": [
          {
            "id": 2,
            "audioFile": "audio/drums.wav",
            "position": 141120000,
            "offset": 0,
            "length": 1411200000
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
    Project project = makeSampleProject(directory.file("audio/drums.wav"));
    auto drums = project.addTrack("Drums");
    drums.setSoloed(true);
    drums.setVolume(0.7079457843841379); // -3 dB
    drums.addClip(directory.file("audio/kick.wav"), {0, 1234, 5678});
    drums.addClip(directory.file("audio/snare.aiff"), {Ticks{1} << 40, 0, std::nullopt});
    project.addTrack("Empty");
    project.addTrack(juce::CharPointer_UTF8("Voce \xc3\xa8 bella")).addClip(directory.file("v.wav"));
    project.setSnapEnabled(false);

    const auto file = directory.file(juce::String("song") + daw::model::projectFileExtension);
    REQUIRE(saveProject(project, file).wasOk());

    const auto loaded = loadProject(file);

    CAPTURE(loaded.error);
    REQUIRE(loaded.succeeded());
    CHECK(loaded.fileFormatVersion == daw::model::currentFormatVersion);
    CHECK(loaded.project->isEquivalentTo(project));
    CHECK(daw::model::toJson(*loaded.project, directory.file("")) ==
          daw::model::toJson(project, directory.file("")));
    CHECK(loaded.project->getTrack(1).getId() == drums.getId());
    CHECK_FALSE(loaded.project->getTrack(1).getClip(1).getLength().has_value());
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
        R"({"format": "daw-project", "formatVersion": 2, "futureThing": [1, 2],
            "project": {"loop": {"enabled": true, "shuffle": 3}, "colour": "red",
                        "tracks": [{"name": "A", "clips": [], "volume": 0.5, "hue": 7}]}})",
        juce::File());

    CAPTURE(loaded.error);
    REQUIRE(loaded.succeeded());
    CHECK(loaded.project->isLoopEnabled());
    CHECK(loaded.project->getTrack(0).getName() == "A");
    CHECK(loaded.project->getTrack(0).getVolume() == 0.5);
}

namespace
{
// A pretend history of the format: version 1 called the loop flag "repeat", version 2
// moved it into a "loop" object, version 3 added a track list where version 2 had a
// single track.
juce::var renameRepeatToLoop(juce::var json)
{
    auto* project = json["project"].getDynamicObject();
    auto loop = juce::var(new juce::DynamicObject());
    loop.getDynamicObject()->setProperty("enabled", project->getProperty("repeat"));
    project->setProperty("loop", loop);
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
    const std::array<FormatMigration, 2> history{renameRepeatToLoop, wrapSingleTrackInList};
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
                                "project": {"loop": {"enabled": false},
                                            "track": {"name": "Mid"}}})";
    const auto fromTwo = daw::model::detail::fromJson(versionTwo, juce::File(), history);
    REQUIRE(fromTwo.succeeded());
    CHECK(fromTwo.project->getTrack(0).getName() == "Mid");

    // And version 4 is newer than this pretend build understands.
    const auto versionFour = R"({"format": "daw-project", "formatVersion": 4, "project": {}})";
    CHECK_FALSE(daw::model::detail::fromJson(versionFour, juce::File(), history).succeeded());
}

TEST_CASE("A version 1 project opens after the migration to version 2", "[model][file]")
{
    // Written by Phase 1 (v0.1.0): the golden text of format version 1.
    const auto versionOne = R"({
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
    const auto file = directory.file("phase1.dawproject");
    REQUIRE(file.replaceWithText(versionOne));

    const auto loaded = loadProject(file);
    CAPTURE(loaded.error);
    REQUIRE(loaded.succeeded());
    CHECK(loaded.fileFormatVersion == 1);

    const auto& project = *loaded.project;
    CHECK(project.isLoopEnabled());
    CHECK(project.getLoopStart() == 0); // an empty region: the whole project, as in Phase 1
    CHECK(project.getLoopEnd() == 0);
    CHECK(project.getTempoMap() == daw::model::TempoMap(120.0, {4, 4}));
    CHECK(project.isSnapEnabled());
    CHECK(project.getGridDivision() == GridDivision::automatic);
    CHECK(project.getMasterVolume() == 1.0);

    REQUIRE(project.getNumTracks() == 1);
    const auto track = project.getTrack(0);
    CHECK(track.getName() == "Track 1");
    CHECK(track.getVolume() == 1.0);
    CHECK(track.getPan() == 0.0);
    CHECK_FALSE(track.isMuted());
    CHECK_FALSE(track.isSoloed());
    CHECK(track.getId() > 0);

    REQUIRE(track.getNumClips() == 1);
    const auto clip = track.getClip(0);
    CHECK(clip.getAudioFile() == directory.file("audio/drums.wav"));
    CHECK(clip.getBounds() == ClipBounds{0, 0, std::nullopt}); // the whole file, from 0
    CHECK(clip.getId() > 0);
    CHECK(clip.getId() != track.getId());

    // Saving writes the current version, which loads back the same.
    REQUIRE(saveProject(project, file).wasOk());
    const auto reloaded = loadProject(file);
    REQUIRE(reloaded.succeeded());
    CHECK(reloaded.fileFormatVersion == daw::model::currentFormatVersion);
    CHECK(reloaded.project->isEquivalentTo(project));
}

TEST_CASE("Missing, invalid and repeated ids are replaced by new ones", "[model][file]")
{
    const auto loaded = daw::model::fromJson(
        R"({"format": "daw-project", "formatVersion": 2, "project": {"tracks": [
              {"id": 5, "clips": [{"id": 5, "audioFile": "a.wav"}, {"audioFile": "b.wav"}]},
              {"id": -3, "clips": [{"id": 9, "audioFile": "c.wav"}]}]}})",
        juce::File());

    CAPTURE(loaded.error);
    REQUIRE(loaded.succeeded());
    const auto& project = *loaded.project;

    CHECK(project.getTrack(0).getId() == 5);
    CHECK(project.getTrack(1).getClip(0).getId() == 9);

    std::set<daw::model::ObjectId> ids{project.getTrack(0).getId(), project.getTrack(1).getId(),
                                       project.getTrack(0).getClip(0).getId(),
                                       project.getTrack(0).getClip(1).getId(),
                                       project.getTrack(1).getClip(0).getId()};
    CHECK(ids.size() == 5);
    CHECK(ids.count(0) == 0);
}

TEST_CASE("Version 2 fields of the wrong type make the file damaged", "[model][file]")
{
    const auto errorForProject = [](const juce::String& projectJson) {
        return errorFor(R"({"format": "daw-project", "formatVersion": 2, "project": )" +
                        projectJson + "}")
            .toStdString();
    };

    CHECK_THAT(errorForProject(R"({"tempo": "fast"})"), ContainsSubstring("tempo"));
    CHECK_THAT(errorForProject(R"({"timeSignature": {"numerator": 4, "denominator": 3}})"),
               ContainsSubstring("time signature"));
    CHECK_THAT(errorForProject(R"({"timeSignature": 7})"), ContainsSubstring("time signature"));
    CHECK_THAT(errorForProject(R"({"loop": {"enabled": 1}})"), ContainsSubstring("loop"));
    CHECK_THAT(errorForProject(R"({"loop": {"start": 1.5}})"), ContainsSubstring("loop region"));
    CHECK_THAT(errorForProject(R"({"snap": {"enabled": "yes"}})"), ContainsSubstring("snap"));
    CHECK_THAT(errorForProject(R"({"master": {"volume": true}})"),
               ContainsSubstring("master volume"));
    CHECK_THAT(errorForProject(R"({"tracks": [{"pan": "left"}]})"),
               ContainsSubstring("volume or pan"));
    CHECK_THAT(errorForProject(R"({"tracks": [{"solo": 0}]})"), ContainsSubstring("mute or solo"));
    CHECK_THAT(
        errorForProject(R"({"tracks": [{"clips": [{"audioFile": "a.wav", "length": 2.5}]}]})"),
        ContainsSubstring("whole ticks"));

    // An unknown grid name is not worth refusing a project for.
    const auto loaded = daw::model::fromJson(
        R"({"format": "daw-project", "formatVersion": 2, "project": {"snap": {"grid": "1/7"}}})",
        juce::File());
    REQUIRE(loaded.succeeded());
    CHECK(loaded.project->getGridDivision() == GridDivision::automatic);
}
