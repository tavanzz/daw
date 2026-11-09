#pragma once

#include <juce_core/juce_core.h>

#include <functional>

namespace daw::services
{
class CrashReporting;
class UpdateChecker;
class UserSettings;
} // namespace daw::services

namespace daw::ui::desktop
{

/**
    What the app provides around the project (services/) for the Help menu, the About
    window, the update panel and the first launch. Absent parts switch their commands
    off (the UI tests run without them).
*/
struct AppServices
{
    juce::File logFolder;      ///< Help > Open Log Folder
    juce::File licencesFolder; ///< the licences shipped with the app (About)
    juce::String signature;    ///< how this copy is signed, for the About window

    services::UserSettings* settings = nullptr;          ///< update and crash-report choices
    services::UpdateChecker* updates = nullptr;          ///< Help > Check for Updates...
    services::CrashReporting* crashReports = nullptr;    ///< Help > Send Crash Reports

    /** Why this copy cannot replace itself (then the update panel offers the download). */
    juce::String updateInstallProblem;

    /** Installs the downloaded update: offers to save, quits and starts the new version;
        calls back with a reason if it cannot. */
    std::function<void(std::function<void(const juce::String&)> onProblem)> installUpdate;
};

} // namespace daw::ui::desktop
