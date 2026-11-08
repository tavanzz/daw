#include "daw/services/UpdateChecker.h"

#include "daw/services/Http.h"

#include <functional>

namespace daw::services
{

// One job at a time on a thread of its own.
class UpdateChecker::Worker final : public juce::Thread
{
public:
    Worker() : juce::Thread("Update check") {}
    ~Worker() override { stopThread(30000); }

    bool start(std::function<void()> job)
    {
        // A job that has just set its last status may still be leaving its thread.
        if (isThreadRunning() && !waitForThreadToExit(2000))
            return false;
        task = std::move(job);
        return startThread();
    }

    void run() override
    {
        if (task)
            task();
    }

private:
    std::function<void()> task;
};

UpdateChecker::UpdateChecker(Options checkerOptions)
    : options(std::move(checkerOptions))
    , worker(std::make_unique<Worker>())
{
}

UpdateChecker::~UpdateChecker()
{
    cancelRequested = true;
    worker.reset();
}

UpdateChecker::Status UpdateChecker::getStatus() const
{
    const juce::ScopedLock sl(lock);
    return status;
}

void UpdateChecker::setStatus(Status newStatus)
{
    {
        const juce::ScopedLock sl(lock);
        status = std::move(newStatus);
    }
    ++revision;
}

juce::String UpdateChecker::getReleasesPage() const
{
    return options.repositoryUrl + "/releases/latest";
}

void UpdateChecker::check(const UpdatePreferences& preferences, bool manual)
{
    const auto current = getStatus().state;
    if (current == State::checking || current == State::downloading)
        return;
    cancelRequested = false;
    Status checking;
    checking.state = State::checking;
    checking.manual = manual;
    setStatus(checking);
    if (!worker->start([this, preferences, manual] { runCheck(preferences, manual); }))
        setStatus({});
}

void UpdateChecker::download()
{
    const auto current = getStatus();
    if (current.state != State::available || !current.installable)
        return;
    cancelRequested = false;
    auto downloading = current;
    downloading.state = State::downloading;
    downloading.progress = 0.0;
    setStatus(downloading);
    if (!worker->start([this] { runDownload(); }))
        setStatus(current);
}

void UpdateChecker::cancel()
{
    cancelRequested = true;
}

std::optional<juce::String> UpdateChecker::findPreReleaseFeed(juce::String& problem) const
{
    // The newest release (by version) that has an update.json.
    const auto apiUrl = options.releasesApiOverride.isNotEmpty()
                            ? options.releasesApiOverride
                            : options.repositoryUrl.replace("https://github.com/", "https://api.github.com/repos/") +
                                  "/releases?per_page=20";
    const auto answer = http::getText(juce::URL(apiUrl));
    if (!answer.ok)
    {
        problem = "could not ask GitHub for the releases: " + answer.error;
        return std::nullopt;
    }

    std::optional<SemanticVersion> best;
    juce::String feed;
    const auto parsed = juce::JSON::parse(answer.body); // kept: getArray() points into it
    if (const auto* releases = parsed.getArray())
        for (const auto& release : *releases)
        {
            if (static_cast<bool>(release["draft"]))
                continue;
            const auto version = SemanticVersion::parse(release["tag_name"].toString());
            if (!version || (best && *version <= *best))
                continue;
            if (const auto* assets = release["assets"].getArray())
                for (const auto& asset : *assets)
                    if (asset["name"].toString() == "update.json")
                    {
                        best = version;
                        feed = asset["browser_download_url"].toString();
                    }
        }

    if (feed.isEmpty())
    {
        problem = "no release with update information was found";
        return std::nullopt;
    }
    return feed;
}

void UpdateChecker::runCheck(UpdatePreferences preferences, bool manual)
{
    juce::String problem;
    std::optional<juce::String> feed;
    if (options.feedOverride.isNotEmpty())
        feed = options.feedOverride;
    else if (preferences.channel == UpdateChannel::preRelease)
        feed = findPreReleaseFeed(problem);
    else
        feed = options.repositoryUrl + "/releases/latest/download/update.json";

    Status result;
    result.manual = manual;
    if (!feed)
    {
        result.state = State::failed;
        result.problem = problem;
        setStatus(result);
        return;
    }

    const auto answer = http::getText(juce::URL(*feed));
    if (cancelRequested.load())
    {
        setStatus({});
        return;
    }
    if (!answer.ok)
    {
        result.state = State::failed;
        result.problem = answer.status == 404 ? juce::String("no release with update information yet")
                                              : "could not reach GitHub: " + answer.error;
        setStatus(result);
        return;
    }
    finishCheck(manifest::read(answer.body, options.publicKey), preferences, manual);
}

void UpdateChecker::finishCheck(manifest::ReadResult read, const UpdatePreferences& preferences, bool manual)
{
    Status result;
    result.manual = manual;
    if (!read.release)
    {
        result.state = State::failed;
        result.problem = read.problem;
        setStatus(result);
        return;
    }

    const auto& release = *read.release;
    const auto current = SemanticVersion::parse(options.currentVersion);
    if (release.bundleId != options.bundleId)
    {
        result.state = State::failed;
        result.problem = "the update information is for another app (" + release.bundleId + ")";
    }
    else if (!current || release.version <= *current ||
             (!manual && release.version.toString() == preferences.skippedVersion))
    {
        // Not newer (never a downgrade), or a version the user chose to skip.
        result.state = State::upToDate;
    }
    else
    {
        result.state = State::available;
        result.release = release;
        result.installable = release.assetFor(options.platform) != nullptr;
    }
    setStatus(result);
}

void UpdateChecker::runDownload()
{
    auto result = getStatus();
    const auto* asset = result.release ? result.release->assetFor(options.platform) : nullptr;
    if (asset == nullptr)
        return;

    const juce::URL url(asset->url);
    auto fileName = url.getFileName();
    if (fileName.isEmpty() || !juce::File::createLegalFileName(fileName).equalsIgnoreCase(fileName))
        fileName = "update";
    const auto target = options.downloadFolder.getChildFile(fileName);
    const auto partial = target.withFileExtension(target.getFileExtension() + ".part");

    const auto total = asset->size;
    const auto problem = http::download(
        url, partial,
        [this, total](juce::int64 done, juce::int64) {
            auto progressing = getStatus();
            progressing.progress = juce::jlimit(0.0, 1.0, static_cast<double>(done) / static_cast<double>(total));
            setStatus(progressing);
            return !cancelRequested.load();
        },
        cancelRequested);

    if (problem == "cancelled")
    {
        result.state = State::available;
        setStatus(result);
        return;
    }
    if (problem.isNotEmpty())
    {
        result.state = State::failed;
        result.problem = problem;
        setStatus(result);
        return;
    }

    // Only what the signed manifest describes counts.
    if (!manifest::fileMatches(partial, *asset))
    {
        partial.deleteFile();
        result.state = State::failed;
        result.problem = "the downloaded file is not the one the update describes (size or SHA-256 differ)";
        setStatus(result);
        return;
    }

    target.deleteFile();
    if (!partial.moveFileTo(target))
    {
        result.state = State::failed;
        result.problem = "cannot write " + target.getFullPathName();
        setStatus(result);
        return;
    }

    result.state = State::ready;
    result.progress = 1.0;
    result.downloadedFile = target;
    setStatus(result);
}

} // namespace daw::services
