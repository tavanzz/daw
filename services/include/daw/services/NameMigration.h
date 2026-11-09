#pragma once

#include <juce_core/juce_core.h>

namespace daw::services
{

/** Takes over the user's data kept under the app's previous name (BuildInfo::
    previousProductName: "DAW", up to v0.8.0-rc.4), once, before the settings and the log
    open. The settings are copied, so a copy of the old app still finds its own; the
    session logs and crash reports are moved, so the history stays in one place and no
    report is handled twice. */
class NameMigration
{
public:
    /** Where one name keeps its data: the settings folder (UserSettings) and the log
        folder (AppLog), which may be inside it. A run with settings of its own
        (--settings-dir) gives the same folders for both names. */
    struct Folders
    {
        juce::String name;
        juce::File settings;
        juce::File logs;
    };

    struct Result
    {
        bool tookOver = false; ///< the previous name's settings were found and copied
        int logsMoved = 0;
        int crashReportsMoved = 0;
        juce::StringArray problems; ///< what could not be copied or moved

        /** A line for the log, empty when nothing was taken over. */
        [[nodiscard]] juce::String describe(const Folders& from) const;
    };

    /** Runs only when the current name has no settings file yet and the previous one has.
        Copies everything in the previous settings folder but its logs (the settings file
        renamed after the current name; nothing already there is overwritten), and moves
        the previous name's session logs (renamed after the current name, so rotation
        counts them) and its crash reports into the current log folder. */
    [[nodiscard]] static Result run(const Folders& previous, const Folders& current);
};

} // namespace daw::services
