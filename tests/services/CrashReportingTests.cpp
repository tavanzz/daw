#include "daw/services/CrashReporting.h"

#include "support/TestAudioFiles.h"

#include <catch2/catch_test_macros.hpp>

using daw::services::CrashReporting;

namespace
{

// An event as sentry-native writes it for a crash, with what must not leave.
juce::var crashEvent(const juce::File& home)
{
    const auto plugin = home.getChildFile("Library/Audio/Plug-Ins/VST3/Secret.vst3").getFullPathName();
    return juce::JSON::parse(R"({
        "event_id": "0123456789abcdef0123456789abcdef",
        "timestamp": "2026-11-08T12:00:00Z",
        "platform": "native",
        "level": "fatal",
        "release": "io.github.tavanzz.daw@0.8.0",
        "server_name": "Someones-MacBook",
        "user": {"id": "42", "ip_address": "{{auto}}", "username": "someone"},
        "breadcrumbs": [{"message": "opened My Song.dawproject"}],
        "extra": {"project": "My Song"},
        "message": {"formatted": "My Song"},
        "exception": {"values": [{"type": "SIGSEGV", "mechanism": {"type": "signalhandler"},
                                  "stacktrace": {"frames": [{"instruction_addr": "0x1000"}]}}]},
        "debug_meta": {"images": [{"type": "macho", "code_file": ")" +
                             plugin.replace("\\", "\\\\") + R"(", "image_addr": "0x1000"}]},
        "contexts": {"os": {"name": "macOS", "version": "15.6.1", "build": "24G90"},
                     "device": {"arch": "arm64", "name": "Someones-MacBook", "model": "Mac15,7"},
                     "app": {"app_name": "DAW"}},
        "tags": {"secret": "yes"}
    })");
}

juce::String runProbe(const juce::String& command, const juce::File& folder, int& exitCode)
{
    juce::ChildProcess probe;
    REQUIRE(probe.start(juce::StringArray{DAW_CRASH_PROBE, command, folder.getFullPathName()}));
    const auto output = probe.readAllProcessOutput();
    REQUIRE(probe.waitForProcessToFinish(60000));
    exitCode = static_cast<int>(probe.getExitCode());
    return output;
}

} // namespace

TEST_CASE("A crash event keeps the crash and loses everything personal", "[services][crash]")
{
    const auto home = juce::File::getSpecialLocation(juce::File::tempDirectory).getChildFile("someone");
    const auto scrubbed = CrashReporting::scrubEvent(crashEvent(home), home, {"Secret Synth (VST3)"});
    const auto text = juce::JSON::toString(scrubbed);

    CHECK(scrubbed["exception"]["values"][0]["type"].toString() == "SIGSEGV");
    CHECK(scrubbed["release"].toString() == "io.github.tavanzz.daw@0.8.0");
    CHECK(scrubbed["contexts"]["os"]["version"].toString() == "15.6.1");
    CHECK(scrubbed["contexts"]["device"]["arch"].toString() == "arm64");
    CHECK(scrubbed["tags"]["plugin"].toString() == "Secret Synth (VST3)");
    CHECK(scrubbed["debug_meta"]["images"][0]["code_file"].toString().startsWith("~"));

    for (const auto* gone : {"server_name", "user", "breadcrumbs", "extra", "message"})
        CHECK_FALSE(scrubbed.hasProperty(gone));
    CHECK_FALSE(text.contains("Someones-MacBook"));
    CHECK_FALSE(text.contains("My Song"));
    CHECK_FALSE(text.contains("Mac15,7"));
    CHECK_FALSE(text.contains("someone/"));
    CHECK_FALSE(text.contains("\"secret\""));
    CHECK_FALSE(text.contains(home.getFullPathName()));
}

TEST_CASE("Only the scrubbed event of an envelope is kept", "[services][crash]")
{
    const auto home = juce::File::getSpecialLocation(juce::File::tempDirectory).getChildFile("someone");
    const auto event = juce::JSON::toString(crashEvent(home), true);
    const juce::String session = R"({"sid":"x","did":"someone@example.com"})";
    const juce::String attachment = "secret bytes";
    const auto envelope = R"({"event_id":"0123456789abcdef0123456789abcdef","dsn":"https://k@h/1"})"
                          "\n" R"({"type":"session"})" "\n" + session + "\n" +
                          R"({"type":"event","length":)" + juce::String(event.getNumBytesAsUTF8()) + "}\n" + event +
                          "\n" + R"({"type":"attachment","length":12,"filename":"log.txt"})" "\n" + attachment + "\n";

    const auto scrubbed = CrashReporting::scrubEnvelope(envelope, home, {});
    REQUIRE(scrubbed.isNotEmpty());
    const auto lines = juce::StringArray::fromLines(scrubbed.trimEnd());
    REQUIRE(lines.size() == 3);
    CHECK(juce::JSON::parse(lines[0])["event_id"].toString() == "0123456789abcdef0123456789abcdef");
    const auto itemHeader = juce::JSON::parse(lines[1]);
    CHECK(itemHeader["type"].toString() == "event");
    CHECK(static_cast<size_t>(static_cast<int>(itemHeader["length"])) == lines[2].getNumBytesAsUTF8());
    CHECK(juce::JSON::parse(lines[2])["level"].toString() == "fatal");
    CHECK_FALSE(scrubbed.contains("someone@example.com"));
    CHECK_FALSE(scrubbed.contains("secret bytes"));

    CHECK(CrashReporting::scrubEnvelope("not an envelope", home, {}).isEmpty());
    CHECK(CrashReporting::scrubEnvelope(R"({"event_id":"x"})" "\n" R"({"type":"session"})" "\n{}\n", home, {})
              .isEmpty());
}

TEST_CASE("A real crash becomes a scrubbed report at the next start", "[services][crash]")
{
    daw::test::TemporaryDirectory directory;
    const auto folder = directory.file("probe");

    // The crash itself: (JUCE reports no exit code for a process ended by a signal.)
    int exitCode = 0;
    runProbe("crash", folder, exitCode);

    // The next start finds one report, and prints how many on its last line.
    const auto output = runProbe("collect", folder, exitCode);
    CHECK(exitCode == 0);
    auto lines = juce::StringArray::fromLines(output.trim());
    CHECK(lines[lines.size() - 1] == "1");

    const auto reports = folder.getChildFile("reports").findChildFiles(juce::File::findFiles, false, "Crash *.json");
    REQUIRE(reports.size() == 1);
    const auto report = reports[0].loadFileAsString();
    const auto event = juce::JSON::parse(report);
    CHECK(event["level"].toString() == "fatal");
    CHECK(event["release"].toString() == "io.github.tavanzz.daw@0.0.0-probe");
    CHECK(event["tags"]["plugin"].toString() == "Probe Plugin (VST3)");
    CHECK(event["exception"]["values"].size() >= 1);
    CHECK(event["debug_meta"]["images"].size() >= 1);

    // Nothing that names the user, the computer or a project.
    const auto home = juce::File::getSpecialLocation(juce::File::userHomeDirectory).getFullPathName();
    CHECK_FALSE(report.containsIgnoreCase(home));
    if (const auto computer = juce::SystemStats::getComputerName(); computer.length() >= 4)
        CHECK_FALSE(report.containsIgnoreCase(computer));
    CHECK_FALSE(report.contains("My Secret Song"));

    // Queued to be sent (the user allowed it), exactly as kept.
    const auto outbox = folder.getChildFile("database/outbox").findChildFiles(juce::File::findFiles, false, "*.envelope");
    REQUIRE(outbox.size() == 1);
    CHECK(outbox[0].loadFileAsString().contains(event["event_id"].toString()));
}
