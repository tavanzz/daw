#include "daw/services/NameMigration.h"
#include "daw/services/UserSettings.h"

#include "support/TestAudioFiles.h"

#include <catch2/catch_test_macros.hpp>

using daw::services::NameMigration;

namespace
{

void write(const juce::File& file, const juce::String& text)
{
    REQUIRE(file.getParentDirectory().createDirectory().wasOk());
    REQUIRE(file.replaceWithText(text));
}

// What v0.8.0-rc.4 leaves: its settings, the plugin catalogue, the crash marker, a report
// waiting to be sent, and in the log folder two session logs and a kept crash report.
void leaveOldApp(const NameMigration::Folders& old)
{
    write(old.settings.getChildFile("DAW.settings"), "<PROPERTIES><VALUE name=\"settingsVersion\" val=\"1\"/></PROPERTIES>");
    write(old.settings.getChildFile("Plugins.json"), "{\"plugins\": []}");
    write(old.settings.getChildFile("CrashedPlugins.txt"), "Gain (VST3)\n");
    write(old.settings.getChildFile("Crash Reporting").getChildFile("outbox").getChildFile("1.envelope"), "report");
    write(old.logs.getChildFile("DAW 2026-11-08 10-00-00.000.log"), "first session");
    write(old.logs.getChildFile("DAW 2026-11-09 10-00-00.000.log"), "second session");
    write(old.logs.getChildFile("Crashes").getChildFile("2026-11-08.json"), "{}");
}

} // namespace

TEST_CASE("The app takes over the settings and logs of its previous name", "[services]")
{
    daw::test::TemporaryDirectory directory;
    // As on macOS: the logs in a folder of their own.
    const NameMigration::Folders old{"DAW", directory.file("Application Support/DAW"), directory.file("Logs/DAW")};
    const NameMigration::Folders now{"MICINSTATION", directory.file("Application Support/MICINSTATION"),
                                     directory.file("Logs/MICINSTATION")};
    leaveOldApp(old);
    write(old.logs.getChildFile("notes.txt"), "not a log of the app");

    const auto result = NameMigration::run(old, now);

    CHECK(result.tookOver);
    CHECK(result.logsMoved == 2);
    CHECK(result.crashReportsMoved == 1);
    CHECK(result.problems.isEmpty());
    CHECK(result.describe(old).contains("2 session logs"));

    // The settings are copied, under the new name; the old app still has its own.
    CHECK(now.settings.getChildFile("MICINSTATION.settings").loadFileAsString().contains("settingsVersion"));
    CHECK(old.settings.getChildFile("DAW.settings").existsAsFile());
    CHECK(now.settings.getChildFile("Plugins.json").loadFileAsString() == "{\"plugins\": []}");
    CHECK(old.settings.getChildFile("Plugins.json").existsAsFile());
    CHECK(now.settings.getChildFile("CrashedPlugins.txt").existsAsFile());

    // Reports waiting to be sent and the logs move, the logs renamed.
    CHECK(now.settings.getChildFile("Crash Reporting/outbox/1.envelope").existsAsFile());
    CHECK_FALSE(old.settings.getChildFile("Crash Reporting").exists());
    CHECK(now.logs.getChildFile("MICINSTATION 2026-11-08 10-00-00.000.log").loadFileAsString() == "first session");
    CHECK(now.logs.getChildFile("MICINSTATION 2026-11-09 10-00-00.000.log").existsAsFile());
    CHECK(now.logs.getChildFile("Crashes/2026-11-08.json").existsAsFile());
    CHECK_FALSE(old.logs.getChildFile("DAW 2026-11-08 10-00-00.000.log").exists());
    CHECK_FALSE(old.logs.getChildFile("Crashes").exists());

    // What is not the app's stays where it was.
    CHECK(old.logs.getChildFile("notes.txt").existsAsFile());
    CHECK_FALSE(now.logs.getChildFile("notes.txt").exists());

    // The settings read as any others.
    const daw::services::UserSettings settings(now.settings, now.name);
    CHECK(settings.getVersionFound() == 1);
}

TEST_CASE("Logs kept with the settings move without being copied", "[services]")
{
    daw::test::TemporaryDirectory directory;
    // As on Windows and Linux: the logs in the settings folder.
    const NameMigration::Folders old{"DAW", directory.file("DAW"), directory.file("DAW/Logs")};
    const NameMigration::Folders now{"MICINSTATION", directory.file("MICINSTATION"), directory.file("MICINSTATION/Logs")};
    leaveOldApp(old);

    const auto result = NameMigration::run(old, now);

    CHECK(result.tookOver);
    CHECK(result.logsMoved == 2);
    CHECK(result.problems.isEmpty());
    CHECK(now.logs.findChildFiles(juce::File::findFiles, false, "*.log").size() == 2);
    CHECK(now.logs.findChildFiles(juce::File::findFiles, false, "DAW *.log").isEmpty());
    CHECK_FALSE(old.logs.exists());
    CHECK(old.settings.getChildFile("DAW.settings").existsAsFile());
}

TEST_CASE("A run with settings of its own renames them in place", "[services]")
{
    daw::test::TemporaryDirectory directory;
    const auto folder = directory.file("run");
    const NameMigration::Folders old{"DAW", folder, folder.getChildFile("Logs")};
    const NameMigration::Folders now{"MICINSTATION", folder, folder.getChildFile("Logs")};
    leaveOldApp(old);

    const auto result = NameMigration::run(old, now);

    CHECK(result.tookOver);
    CHECK(result.logsMoved == 2);
    CHECK(result.crashReportsMoved == 0);
    CHECK(result.problems.isEmpty());
    CHECK(folder.getChildFile("MICINSTATION.settings").existsAsFile());
    CHECK(folder.getChildFile("DAW.settings").existsAsFile());
    CHECK(folder.getChildFile("Logs/MICINSTATION 2026-11-09 10-00-00.000.log").existsAsFile());
    CHECK(folder.getChildFile("Logs/Crashes/2026-11-08.json").existsAsFile());
    CHECK(folder.getChildFile("Crash Reporting/outbox/1.envelope").existsAsFile());
}

TEST_CASE("Taking over happens once and never overwrites", "[services]")
{
    daw::test::TemporaryDirectory directory;
    const NameMigration::Folders old{"DAW", directory.file("DAW"), directory.file("DAW/Logs")};
    const NameMigration::Folders now{"MICINSTATION", directory.file("MICINSTATION"), directory.file("MICINSTATION/Logs")};

    SECTION("Nothing under the previous name")
    {
        const auto result = NameMigration::run(old, now);
        CHECK_FALSE(result.tookOver);
        CHECK(result.describe(old).isEmpty());
        CHECK_FALSE(now.settings.exists());
    }

    SECTION("The current name has its settings already")
    {
        leaveOldApp(old);
        write(now.settings.getChildFile("MICINSTATION.settings"), "<PROPERTIES/>");

        const auto result = NameMigration::run(old, now);
        CHECK_FALSE(result.tookOver);
        CHECK(now.settings.getChildFile("MICINSTATION.settings").loadFileAsString() == "<PROPERTIES/>");
        CHECK_FALSE(now.settings.getChildFile("Plugins.json").exists());
        CHECK(old.logs.getChildFile("DAW 2026-11-08 10-00-00.000.log").existsAsFile());
    }

    SECTION("Files the current name has already are kept")
    {
        leaveOldApp(old);
        write(now.settings.getChildFile("Plugins.json"), "newer catalogue");

        const auto result = NameMigration::run(old, now);
        CHECK(result.tookOver);
        CHECK(now.settings.getChildFile("Plugins.json").loadFileAsString() == "newer catalogue");
        CHECK(NameMigration::run(old, now).tookOver == false);
    }
}
