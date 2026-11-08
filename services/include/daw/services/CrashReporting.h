#pragma once

#include <juce_core/juce_core.h>

#include <atomic>
#include <memory>

namespace daw::services
{

/**
    Crash reports, by sentry-native with its in-process backend: when the app crashes,
    the crashing thread's stack (addresses and the modules they fall in, no memory) is
    written to the crash database; at the next start it is turned into a report.

    Every report is kept locally (the log folder's "Crashes", the last 20), scrubbed. It
    is sent to Sentry only if the user allowed it and the build has a DSN; otherwise it
    never leaves the computer. Scrubbing (scrubEvent()) keeps the app's version, the
    system, the signal, the stack and the modules, the plugin that was running, and
    removes the user, the computer's name, breadcrumbs and anything else; the home
    folder is written "~" everywhere. Sentry keeps no IP address (a project setting).

    Install it after the crash marker (app/CrashMarker): its handler then runs first and
    hands the signal on to the marker's.

    Message thread for the calls; reports are sent on a thread of its own.
*/
class CrashReporting
{
public:
    struct Options
    {
        juce::File databaseFolder;   ///< sentry's working folder
        juce::File reportsFolder;    ///< local copies, readable
        juce::String dsn;            ///< empty: reports are never sent
        juce::String release;        ///< "io.github.tavanzz.daw@0.8.0"
        juce::String environment;    ///< "release", "pre-release" or "development"
        juce::File home = juce::File::getSpecialLocation(juce::File::userHomeDirectory);
        juce::StringArray crashedPlugins; ///< what the crash marker saw at the last crash
        bool sendAllowed = false;
        bool sendInBackground = true; ///< false: reports wait in the outbox (tests)
    };

    explicit CrashReporting(Options options);
    ~CrashReporting();

    CrashReporting(const CrashReporting&) = delete;
    CrashReporting& operator=(const CrashReporting&) = delete;

    /** True if this build can send reports at all (it has a DSN). */
    [[nodiscard]] bool canSend() const noexcept { return options.dsn.isNotEmpty(); }

    /** The user's consent. Refusing deletes the reports waiting to be sent. */
    void setSendAllowed(bool allowed);

    /** Reports that came out of the last crash, as kept locally (the newest first). */
    [[nodiscard]] juce::Array<juce::File> getLocalReports() const;

    /** The event of a crash report, scrubbed: what may be stored or sent. */
    [[nodiscard]] static juce::var scrubEvent(const juce::var& event, const juce::File& home,
                                              const juce::StringArray& crashedPlugins);

    /** A Sentry envelope's text with its event scrubbed (other items left out). */
    [[nodiscard]] static juce::String scrubEnvelope(const juce::String& envelope, const juce::File& home,
                                                    const juce::StringArray& crashedPlugins);

    /** Sends what waits in the outbox (if allowed). Blocks: the reporting thread uses it. */
    void sendPending();

    /** Called by sentry for every envelope it would send. */
    void receiveEnvelope(const juce::String& envelope);

private:
    class Sender;

    [[nodiscard]] juce::File getOutbox() const;

    const Options options;
    std::atomic<bool> sendAllowed{false};
    std::unique_ptr<Sender> sender;
    bool started = false;
};

} // namespace daw::services
