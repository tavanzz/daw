// daw_crash_probe: a program that crashes on purpose, so that tests can read the crash
// report the next run makes (tests/services/CrashReportingTests.cpp).
//
//   daw_crash_probe crash <folder>     sets up crash reports in <folder>, then crashes
//   daw_crash_probe collect <folder>   starts again: the report of the crash is made

#include "daw/services/CrashReporting.h"

#include <juce_core/juce_core.h>

#include <cstdio>

namespace
{

daw::services::CrashReporting::Options optionsFor(const juce::File& folder)
{
    daw::services::CrashReporting::Options options;
    options.databaseFolder = folder.getChildFile("database");
    options.reportsFolder = folder.getChildFile("reports");
    // A DSN, so that reports are queued for sending; the probe never sends them.
    options.dsn = "https://0123456789abcdef0123456789abcdef@o1.ingest.de.sentry.io/42";
    options.release = "io.github.tavanzz.daw@0.0.0-probe";
    options.environment = "development";
    options.crashedPlugins = {"Probe Plugin (VST3)"};
    options.sendAllowed = true;
    options.sendInBackground = false;
    return options;
}

// A null pointer the compiler cannot see through.
volatile int* volatile target = nullptr;

} // namespace

int main(int argc, char* argv[])
{
    if (argc != 3)
        return 2;
    const juce::String command(argv[1]);
    const auto folder = juce::File::getCurrentWorkingDirectory().getChildFile(juce::String(argv[2]));

    daw::services::CrashReporting reporting(optionsFor(folder));
    if (command == "crash")
    {
        // A project name in memory, which must never reach a report.
        volatile const char* secret = "My Secret Song.dawproject";
        std::printf("%c", secret[0] == 'M' ? '.' : '!');
        std::fflush(stdout);
        *target = 1;
        return 3; // not reached
    }
    std::printf("%d\n", reporting.getLocalReports().size());
    return 0;
}
