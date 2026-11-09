#include "daw/services/BuildInfo.h"

#include "daw/services/BuildCommit.h"

namespace daw::services
{

namespace
{

constexpr const char* platformName()
{
#if JUCE_MAC
    return "macOS";
#elif JUCE_WINDOWS
    return "Windows";
#elif JUCE_LINUX
    return "Linux";
#else
    return "unknown";
#endif
}

constexpr const char* architectureName()
{
#if defined(__aarch64__) || defined(__arm64__) || defined(_M_ARM64)
    return "arm64";
#elif defined(__x86_64__) || defined(_M_X64)
    return "x86_64";
#else
    return "unknown";
#endif
}

BuildInfo makeBuildInfo()
{
    BuildInfo info;
    info.productName = DAW_PRODUCT_NAME;
    info.logoText = DAW_LOGO_TEXT;
    info.previousProductName = DAW_PREVIOUS_PRODUCT_NAME;
    info.version = DAW_VERSION;
    info.commit = DAW_BUILD_COMMIT;
    info.modified = DAW_BUILD_COMMIT_MODIFIED;
    info.commitDate = DAW_BUILD_COMMIT_DATE;
    info.buildType = DAW_BUILD_TYPE;
    info.platform = platformName();
    info.architecture = architectureName();
    info.bundleId = DAW_BUNDLE_ID;
    info.updateId = DAW_UPDATE_ID;
    info.repositoryUrl = DAW_REPOSITORY_URL;
    info.crashReportsDsn = DAW_SENTRY_DSN;
    return info;
}

} // namespace

juce::String BuildInfo::describe() const
{
    return productName + " " + version + " (" + commit + (modified ? "+" : "") + ", " + buildType +
           ", " + platform + " " + architecture + ")";
}

juce::String BuildInfo::commitUrl() const
{
    if (commit == "unknown" || repositoryUrl.isEmpty())
        return repositoryUrl;
    return repositoryUrl + "/commit/" + commit;
}

const BuildInfo& buildInfo()
{
    static const BuildInfo info = makeBuildInfo();
    return info;
}

} // namespace daw::services
