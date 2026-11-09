#pragma once

#include <juce_core/juce_core.h>

namespace daw::services
{

/**
    The app's local log: one file per session in the log folder, the last few sessions
    kept, each file limited in size. Installed as JUCE's current logger, so
    juce::Logger::writeToLog() writes here from any thread but the audio thread (which
    never logs: see the audio thread rules).

    The user's home folder is written as "~", so a log can be sent without showing the
    user's name in paths.
*/
class AppLog final : public juce::Logger
{
public:
    struct Options
    {
        juce::File folder;
        int sessionsKept = 20;
        juce::int64 maxBytesPerSession = 4 * 1024 * 1024;
        juce::String applicationName = "MICINSTATION";
        /** The folder written as "~" (the user's home; tests choose their own). */
        juce::File home = juce::File::getSpecialLocation(juce::File::userHomeDirectory);
    };

    /** The log folder for this user: ~/Library/Logs/<name> on macOS, Logs next to the user
        settings elsewhere. */
    [[nodiscard]] static juce::File defaultFolder(const juce::String& applicationName);

    explicit AppLog(Options options);
    ~AppLog() override;

    AppLog(const AppLog&) = delete;
    AppLog& operator=(const AppLog&) = delete;

    [[nodiscard]] juce::File getFolder() const { return options.folder; }
    [[nodiscard]] juce::File getFile() const { return file; }

    /** False if the previous session's log does not end with the line the app writes when
        it quits normally (a crash, a forced quit, a power cut). */
    [[nodiscard]] bool previousSessionEndedNormally() const noexcept { return previousEndedNormally; }

    /** The last line of a normal session; AppController writes it when the app quits. */
    static constexpr const char* endOfSessionLine = "Session ended normally.";

    /** `text` with `home`'s path written as "~". */
    [[nodiscard]] static juce::String maskHome(const juce::String& text, const juce::File& home);

    void logMessage(const juce::String& message) override;

private:
    void removeOldSessions();

    Options options;
    juce::File file;
    std::unique_ptr<juce::FileOutputStream> stream;
    juce::CriticalSection lock;
    juce::int64 bytesWritten = 0;
    bool full = false;
    bool previousEndedNormally = true;
};

} // namespace daw::services
