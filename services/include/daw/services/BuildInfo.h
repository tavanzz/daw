#pragma once

#include <juce_core/juce_core.h>

namespace daw::services
{

/** What this build of the app is: shown in the About window, written at the top of every
    log and into crash reports, compared by the updater. */
struct BuildInfo
{
    juce::String productName;   ///< e.g. "DAW"
    juce::String version;       ///< semantic version, with the tag's suffix: "0.8.0-rc.1"
    juce::String commit;        ///< 12 hex digits, or "unknown" (built without git)
    bool modified = false;      ///< the working tree had uncommitted changes
    juce::String commitDate;    ///< ISO 8601, empty without git
    juce::String buildType;     ///< "Debug" or "Release"
    juce::String platform;      ///< "macOS", "Windows" or "Linux"
    juce::String architecture;  ///< "arm64" or "x86_64": the slice running now
    juce::String bundleId;      ///< e.g. "io.github.tavanzz.daw"
    juce::String repositoryUrl; ///< the GitHub repository, for links and updates
    juce::String crashReportsDsn; ///< where crash reports may go (Sentry); empty: nowhere

    /** One line, e.g. "DAW 0.8.0 (3f2a9c1b7e04, Release, macOS arm64)". */
    [[nodiscard]] juce::String describe() const;

    /** The repository's page for this commit, or for the repository without one. */
    [[nodiscard]] juce::String commitUrl() const;
};

/** This build's information. */
[[nodiscard]] const BuildInfo& buildInfo();

} // namespace daw::services
