#pragma once

#include <juce_core/juce_core.h>

#include <atomic>
#include <functional>

namespace daw::services::http
{

/** What a request brought back. */
struct Response
{
    bool ok = false;     ///< reached and answered 2xx (file:// URLs: readable)
    int status = 0;      ///< HTTP status, 0 if never answered
    juce::String body;   ///< text of the answer
    juce::String error;  ///< why not ok, for the user
};

/** The User-Agent the app sends ("DAW/0.8.0"), which GitHub's API requires. */
[[nodiscard]] juce::String userAgent();

/** Fetches a small text (a manifest, an API answer), following redirects. Blocks: call it
    on a worker thread. file:// URLs read the file (tests, local feeds). */
[[nodiscard]] Response getText(const juce::URL& url, int timeoutMilliseconds = 20000);

/** Posts `body` and returns the answer (crash reports). Blocks: worker thread. */
[[nodiscard]] Response post(const juce::URL& url, const juce::MemoryBlock& body, const juce::String& headers,
                            int timeoutMilliseconds = 20000);

/** Downloads into `target` (replaced). `onProgress(bytes so far, total or -1)` may return
    false to cancel, as may `cancel` becoming true. Blocks: worker thread. Returns an
    empty string on success, else the reason; `target` is deleted on failure. */
[[nodiscard]] juce::String download(const juce::URL& url, const juce::File& target,
                                    const std::function<bool(juce::int64, juce::int64)>& onProgress,
                                    const std::atomic<bool>& cancel, int timeoutMilliseconds = 20000);

} // namespace daw::services::http
