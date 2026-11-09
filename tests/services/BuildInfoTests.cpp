#include "daw/services/BuildInfo.h"

#include <catch2/catch_test_macros.hpp>

TEST_CASE("The build information names this build", "[services]")
{
    const auto& info = daw::services::buildInfo();

    CHECK(info.productName.isNotEmpty());
    CHECK(info.version.matchesWildcard("*.*.*", false));
    CHECK(info.bundleId.contains("."));
    CHECK(info.repositoryUrl.startsWith("https://github.com/"));
    CHECK((info.platform == "macOS" || info.platform == "Windows" || info.platform == "Linux"));
    CHECK((info.architecture == "arm64" || info.architecture == "x86_64"));
    CHECK((info.commit == "unknown" || info.commit.length() == 12));
    CHECK(info.commit.containsOnly("0123456789abcdefunknow"));

    const auto line = info.describe();
    CHECK(line.startsWith(info.productName + " " + info.version + " ("));
    CHECK(line.contains(info.platform + " " + info.architecture));
}

TEST_CASE("The commit link points at the repository", "[services]")
{
    daw::services::BuildInfo info;
    info.repositoryUrl = "https://github.com/someone/daw";

    info.commit = "0123456789ab";
    CHECK(info.commitUrl() == "https://github.com/someone/daw/commit/0123456789ab");

    info.commit = "unknown";
    CHECK(info.commitUrl() == "https://github.com/someone/daw");
}

TEST_CASE("Updates keep naming the app by its first bundle id", "[services]")
{
    const auto& info = daw::services::buildInfo();

    // Copies of the app from before it was renamed accept only manifests naming this.
    CHECK(info.updateId == "io.github.tavanzz.daw");
    CHECK(info.previousProductName == "DAW");
    CHECK(info.bundleId != info.updateId);

    // The logo shows the name itself, in words.
    CHECK(info.logoText.removeCharacters(" ") == info.productName);
    CHECK(info.logoText.contains(" "));
}
