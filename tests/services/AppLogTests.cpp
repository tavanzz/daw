#include "daw/services/AppLog.h"

#include "support/TestAudioFiles.h"

#include <catch2/catch_test_macros.hpp>

using daw::services::AppLog;

namespace
{

AppLog::Options optionsFor(const juce::File& folder)
{
    AppLog::Options options;
    options.folder = folder;
    options.home = folder.getChildFile("home").getChildFile("someone");
    return options;
}

juce::Array<juce::File> logsIn(const juce::File& folder)
{
    return folder.findChildFiles(juce::File::findFiles, false, "DAW *.log");
}

} // namespace

TEST_CASE("The home folder is written as a tilde", "[services][log]")
{
    const auto home = juce::File::getSpecialLocation(juce::File::tempDirectory).getChildFile("someone");
    const auto path = home.getChildFile("Music").getChildFile("Song.dawproject").getFullPathName();

    CHECK(AppLog::maskHome("Opened " + path, home) ==
          "Opened ~" + path.substring(home.getFullPathName().length()));
    CHECK(AppLog::maskHome(home.getFullPathName() + " and " + home.getFullPathName(), home) == "~ and ~");
    CHECK(AppLog::maskHome("nothing here", home) == "nothing here");
    if (!juce::File::areFileNamesCaseSensitive())
        CHECK(AppLog::maskHome(path.toUpperCase(), home).startsWith("~"));
}

TEST_CASE("Each session writes a log of its own, and the next one knows how it ended",
          "[services][log]")
{
    daw::test::TemporaryDirectory directory;
    const auto folder = directory.file("Logs");
    const auto options = optionsFor(folder);
    const auto project = options.home.getChildFile("Music/Song.dawproject").getFullPathName();

    juce::File first;
    {
        AppLog log(options);
        CHECK(log.previousSessionEndedNormally()); // there was none
        first = log.getFile();
        log.logMessage("Opened " + project);
        // No end-of-session line: as if the app crashed.
    }
    const auto text = first.loadFileAsString();
    CHECK(text.contains("Opened ~"));
    CHECK_FALSE(text.contains(options.home.getFullPathName()));

    {
        AppLog log(options);
        CHECK_FALSE(log.previousSessionEndedNormally());
        CHECK(log.getFile() != first);
        log.logMessage(AppLog::endOfSessionLine);
    }

    AppLog log(options);
    CHECK(log.previousSessionEndedNormally());
    CHECK(logsIn(folder).size() == 3);
}

TEST_CASE("Old sessions are removed and a session's log has a size limit", "[services][log]")
{
    daw::test::TemporaryDirectory directory;
    const auto folder = directory.file("Logs");
    REQUIRE(folder.createDirectory());
    for (int index = 0; index < 25; ++index)
        REQUIRE(folder.getChildFile("DAW 2020-01-01 00-00-" + juce::String(index).paddedLeft('0', 2) + ".000.log")
                    .replaceWithText("old\n"));
    REQUIRE(folder.getChildFile("Other file.txt").replaceWithText("kept"));

    auto options = optionsFor(folder);
    options.sessionsKept = 5;
    options.maxBytesPerSession = 300;
    AppLog log(options);

    const auto logs = logsIn(folder);
    CHECK(logs.size() == 5);
    CHECK(logs.contains(log.getFile()));
    CHECK_FALSE(folder.getChildFile("DAW 2020-01-01 00-00-00.000.log").exists());
    CHECK(folder.getChildFile("DAW 2020-01-01 00-00-24.000.log").exists());
    CHECK(folder.getChildFile("Other file.txt").exists());

    for (int index = 0; index < 50; ++index)
        log.logMessage("Message number " + juce::String(index));
    log.logMessage(AppLog::endOfSessionLine);

    const auto text = log.getFile().loadFileAsString();
    CHECK(text.contains("Message number 0"));
    CHECK_FALSE(text.contains("Message number 49"));
    CHECK(text.contains("The log is full"));
    CHECK(text.trimEnd().endsWith(AppLog::endOfSessionLine));
    CHECK(log.getFile().getSize() < 600);
}
