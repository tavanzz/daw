#include "daw/services/Http.h"

#include "daw/services/BuildInfo.h"

namespace daw::services::http
{

namespace
{

juce::URL::InputStreamOptions optionsFor(int timeoutMilliseconds, int* status, juce::String headers = {})
{
    return juce::URL::InputStreamOptions(juce::URL::ParameterHandling::inAddress)
        .withConnectionTimeoutMs(timeoutMilliseconds)
        .withNumRedirectsToFollow(8)
        .withStatusCode(status)
        .withExtraHeaders("User-Agent: " + userAgent() + (headers.isNotEmpty() ? "\r\n" + headers : juce::String()));
}

juce::String describe(int status)
{
    return status == 0 ? juce::String("no answer (no network?)")
                       : "the server answered " + juce::String(status);
}

} // namespace

juce::String userAgent()
{
    const auto& build = buildInfo();
    return build.productName + "/" + build.version;
}

Response getText(const juce::URL& url, int timeoutMilliseconds)
{
    Response response;
    auto stream = url.createInputStream(optionsFor(timeoutMilliseconds, &response.status));
    const bool answered = stream != nullptr && (url.isLocalFile() || (response.status >= 200 && response.status < 300));
    if (!answered)
    {
        response.error = url.isLocalFile() ? "cannot read " + url.getLocalFile().getFullPathName()
                                           : describe(response.status);
        return response;
    }
    response.body = stream->readEntireStreamAsString();
    response.ok = true;
    return response;
}

Response post(const juce::URL& url, const juce::MemoryBlock& body, const juce::String& headers,
              int timeoutMilliseconds)
{
    Response response;
    const auto withBody = url.withPOSTData(body);
    auto options = juce::URL::InputStreamOptions(juce::URL::ParameterHandling::inPostData)
                       .withConnectionTimeoutMs(timeoutMilliseconds)
                       .withNumRedirectsToFollow(0)
                       .withStatusCode(&response.status)
                       .withExtraHeaders("User-Agent: " + userAgent() + "\r\n" + headers);
    auto stream = withBody.createInputStream(options);
    if (stream == nullptr || response.status < 200 || response.status >= 300)
    {
        response.error = describe(response.status);
        return response;
    }
    response.body = stream->readEntireStreamAsString();
    response.ok = true;
    return response;
}

juce::String download(const juce::URL& url, const juce::File& target,
                      const std::function<bool(juce::int64, juce::int64)>& onProgress,
                      const std::atomic<bool>& cancel, int timeoutMilliseconds)
{
    int status = 0;
    auto stream = url.createInputStream(optionsFor(timeoutMilliseconds, &status));
    if (stream == nullptr || (!url.isLocalFile() && (status < 200 || status >= 300)))
        return "the download failed: " + (url.isLocalFile() ? "cannot read the file" : describe(status));

    target.deleteFile();
    target.getParentDirectory().createDirectory();
    juce::String problem;
    {
        juce::FileOutputStream out(target);
        if (out.failedToOpen())
            return "cannot write " + target.getFullPathName();

        const auto total = stream->getTotalLength();
        juce::HeapBlock<char> buffer(65536);
        juce::int64 done = 0;
        for (;;)
        {
            if (cancel.load() || (onProgress && !onProgress(done, total)))
            {
                problem = "cancelled";
                break;
            }
            const auto read = stream->read(buffer.get(), 65536);
            if (read < 0)
            {
                problem = "the download broke off";
                break;
            }
            if (read == 0)
            {
                if (!stream->isExhausted() || (total >= 0 && done != total))
                    problem = "the download broke off";
                break;
            }
            if (!out.write(buffer.get(), static_cast<size_t>(read)))
            {
                problem = "cannot write " + target.getFullPathName() + " (disk full?)";
                break;
            }
            done += read;
        }
        out.flush();
        if (problem.isEmpty() && out.getStatus().failed())
            problem = "cannot write " + target.getFullPathName();
    }

    if (problem.isNotEmpty())
        target.deleteFile();
    return problem;
}

} // namespace daw::services::http
