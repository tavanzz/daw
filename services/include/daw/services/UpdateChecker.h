#pragma once

#include "daw/services/UpdateManifest.h"
#include "daw/services/UserSettings.h"

#include <juce_core/juce_core.h>

#include <atomic>
#include <optional>

namespace daw::services
{

/**
    Looks for a newer release and downloads it, on a thread of its own; every UI reads
    its state (getStatus(), polled by a UI timer) and starts its steps.

    Stable channel: the latest release's update.json (GitHub's
    releases/latest/download/, no API). Pre-release channel: GitHub's releases API, the
    newest release with an update.json. A manifest is used only with a valid signature by
    the build's key, for this app, for a version newer than the running one, and (when
    the check was not asked for by the user) not skipped. A download counts only with the
    manifest's size and SHA-256.

    Message thread for the calls; the work happens on the checker's thread.
*/
class UpdateChecker
{
public:
    enum class State
    {
        idle,        ///< nothing checked yet
        checking,
        upToDate,    ///< nothing newer (or only a skipped version)
        available,   ///< a newer release, not downloaded
        downloading,
        ready,       ///< downloaded and verified: getDownloadedFile()
        failed       ///< getStatus().problem says why
    };

    struct Options
    {
        juce::String currentVersion;      ///< the running app's (BuildInfo, or pretended)
        juce::String bundleId;            ///< manifests for another app are refused
        juce::MemoryBlock publicKey;      ///< empty: nothing is installed
        juce::String repositoryUrl;       ///< "https://github.com/owner/repo"
        juce::String feedOverride;        ///< a manifest URL used instead of both channels
        juce::String releasesApiOverride; ///< tests: the API's answer for the pre-release channel
        juce::File downloadFolder;        ///< where downloads go
        juce::String platform = manifest::platformKey();
    };

    struct Status
    {
        State state = State::idle;
        std::optional<UpdateRelease> release; ///< available, downloading, ready
        bool installable = false;             ///< the release has a file for this platform
        double progress = 0.0;                ///< downloading: 0..1 (or -1 when unknown)
        juce::String problem;                 ///< failed
        juce::File downloadedFile;            ///< ready
        bool manual = false;                  ///< the user asked for this check
    };

    explicit UpdateChecker(Options options);
    ~UpdateChecker();

    UpdateChecker(const UpdateChecker&) = delete;
    UpdateChecker& operator=(const UpdateChecker&) = delete;

    /** Starts a check (ignored while one runs). `manual`: asked for by the user, so a skipped
        version is offered again and failures are worth showing. */
    void check(const UpdatePreferences& preferences, bool manual);

    /** Downloads the available release's file for this platform. */
    void download();

    /** Stops a check or a download. */
    void cancel();

    [[nodiscard]] Status getStatus() const;

    /** The releases page, for builds or platforms that cannot install the release. */
    [[nodiscard]] juce::String getReleasesPage() const;

    /** Bumped whenever the status changes, so a UI repaints only then. */
    [[nodiscard]] int getRevision() const noexcept { return revision.load(); }

private:
    class Worker;

    void runCheck(UpdatePreferences preferences, bool manual);
    void runDownload();
    [[nodiscard]] std::optional<juce::String> findPreReleaseFeed(juce::String& problem) const;
    void setStatus(Status status);
    void finishCheck(manifest::ReadResult read, const UpdatePreferences& preferences, bool manual);

    const Options options;
    mutable juce::CriticalSection lock;
    Status status;
    std::atomic<int> revision{0};
    std::atomic<bool> cancelRequested{false};
    std::unique_ptr<Worker> worker;
};

} // namespace daw::services
