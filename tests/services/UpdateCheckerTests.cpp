#include "daw/services/Ed25519.h"
#include "daw/services/UpdateChecker.h"

#include "support/TestAudioFiles.h"

#include <catch2/catch_test_macros.hpp>

#include <algorithm>

using daw::services::UpdateChannel;
using daw::services::UpdateChecker;
using daw::services::UpdatePreferences;
using State = UpdateChecker::State;
namespace ed25519 = daw::services::ed25519;
namespace manifest = daw::services::manifest;

namespace
{

ed25519::KeyPair keysFrom(int seedByte)
{
    juce::MemoryBlock seed(ed25519::seedSize);
    seed.fillWith(static_cast<juce::uint8>(seedByte));
    return ed25519::keyPairFromSeed(seed);
}

// A local "GitHub": release files and signed manifests in a folder, reached by file:// URLs.
struct LocalReleases
{
    daw::test::TemporaryDirectory directory;
    ed25519::KeyPair keys = keysFrom(3);

    juce::File publish(const juce::String& version, const juce::String& content = "new app",
                       const juce::String& bundleId = "io.github.tavanzz.daw", juce::int64 sizeOverride = -1)
    {
        const auto folder = directory.file("v" + version);
        folder.createDirectory();
        const auto asset = folder.getChildFile("DAW-" + version + "-installer.bin");
        asset.replaceWithText(content);

        auto assets = std::make_unique<juce::DynamicObject>();
        auto entry = std::make_unique<juce::DynamicObject>();
        entry->setProperty("url", juce::URL(asset).toString(false));
        entry->setProperty("size", sizeOverride >= 0 ? sizeOverride : asset.getSize());
        entry->setProperty("sha256", daw::services::sha256Of(asset));
        assets->setProperty(manifest::platformKey(), entry.release());

        auto payload = std::make_unique<juce::DynamicObject>();
        payload->setProperty("bundleId", bundleId);
        payload->setProperty("version", version);
        payload->setProperty("notes", "Notes of " + version);
        payload->setProperty("assets", assets.release());

        const auto file = folder.getChildFile("update.json");
        file.replaceWithText(manifest::sign(juce::JSON::toString(juce::var(payload.release())), keys.secretKey), false,
                             false, "\n");
        return file;
    }

    UpdateChecker::Options options(const juce::String& currentVersion, const juce::File& feed) const
    {
        UpdateChecker::Options result;
        result.currentVersion = currentVersion;
        result.bundleId = "io.github.tavanzz.daw";
        result.publicKey = keys.publicKey;
        result.repositoryUrl = "https://github.com/tavanzz/daw";
        result.feedOverride = feed != juce::File() ? juce::URL(feed).toString(false) : juce::String();
        result.downloadFolder = directory.file("Downloads");
        return result;
    }
};

UpdateChecker::Status waitWhile(UpdateChecker& checker, std::initializer_list<State> busy)
{
    for (int attempt = 0; attempt < 1000; ++attempt)
    {
        const auto status = checker.getStatus();
        if (std::find(busy.begin(), busy.end(), status.state) == busy.end())
            return status;
        juce::Thread::sleep(10);
    }
    FAIL("the update checker did not finish");
    return {};
}

UpdateChecker::Status checkNow(UpdateChecker& checker, const UpdatePreferences& preferences = {}, bool manual = false)
{
    checker.check(preferences, manual);
    return waitWhile(checker, {State::checking});
}

} // namespace

TEST_CASE("A newer signed release is offered, downloaded and verified", "[services][updates]")
{
    LocalReleases releases;
    const auto feed = releases.publish("0.9.0");
    UpdateChecker checker(releases.options("0.8.0", feed));

    const auto found = checkNow(checker);
    REQUIRE(found.state == State::available);
    REQUIRE(found.release.has_value());
    CHECK(found.release->version.toString() == "0.9.0");
    CHECK(found.release->notes == "Notes of 0.9.0");
    CHECK(found.installable);

    const auto revision = checker.getRevision();
    checker.download();
    const auto downloaded = waitWhile(checker, {State::available, State::downloading});
    REQUIRE(downloaded.state == State::ready);
    CHECK(checker.getRevision() > revision);
    CHECK(downloaded.downloadedFile.loadFileAsString() == "new app");
    CHECK(downloaded.downloadedFile.getParentDirectory() == releases.directory.file("Downloads"));
}

TEST_CASE("Nothing older or equal is offered, and skipped versions only when asked for",
          "[services][updates]")
{
    LocalReleases releases;
    const auto feed = releases.publish("0.9.0");

    UpdateChecker same(releases.options("0.9.0", feed));
    CHECK(checkNow(same).state == State::upToDate);

    UpdateChecker newer(releases.options("1.0.0", feed));
    CHECK(checkNow(newer).state == State::upToDate); // never a downgrade

    UpdateChecker candidate(releases.options("0.9.0-rc.2", feed));
    CHECK(checkNow(candidate).state == State::available);

    UpdatePreferences skipping;
    skipping.skippedVersion = "0.9.0";
    UpdateChecker skipped(releases.options("0.8.0", feed));
    CHECK(checkNow(skipped, skipping).state == State::upToDate);
    CHECK(checkNow(skipped, skipping, true).state == State::available);
}

TEST_CASE("Bad signatures, another app, missing feeds and tampered files are refused",
          "[services][updates]")
{
    LocalReleases releases;

    SECTION("a manifest signed with another key")
    {
        const auto feed = releases.publish("0.9.0");
        auto options = releases.options("0.8.0", feed);
        options.publicKey = keysFrom(4).publicKey;
        UpdateChecker checker(options);
        const auto status = checkNow(checker);
        CHECK(status.state == State::failed);
        CHECK(status.problem.contains("signature"));
    }

    SECTION("a build without a key")
    {
        const auto feed = releases.publish("0.9.0");
        auto options = releases.options("0.8.0", feed);
        options.publicKey = {};
        UpdateChecker checker(options);
        CHECK(checkNow(checker).state == State::failed);
    }

    SECTION("a manifest for another app")
    {
        const auto feed = releases.publish("0.9.0", "x", "com.example.other");
        UpdateChecker checker(releases.options("0.8.0", feed));
        const auto status = checkNow(checker);
        CHECK(status.state == State::failed);
        CHECK(status.problem.contains("another app"));
    }

    SECTION("no feed")
    {
        UpdateChecker checker(releases.options("0.8.0", releases.directory.file("missing.json")));
        CHECK(checkNow(checker).state == State::failed);
    }

    SECTION("a file that is not the one the manifest describes")
    {
        const auto feed = releases.publish("0.9.0", "new app");
        // Replaced after signing, same size.
        REQUIRE(releases.directory.file("v0.9.0/DAW-0.9.0-installer.bin").replaceWithText("bad app"));
        UpdateChecker checker(releases.options("0.8.0", feed));
        REQUIRE(checkNow(checker).state == State::available);
        checker.download();
        const auto status = waitWhile(checker, {State::available, State::downloading});
        CHECK(status.state == State::failed);
        CHECK(status.problem.contains("SHA-256"));
        CHECK(releases.directory.file("Downloads").findChildFiles(juce::File::findFiles, false).isEmpty());
    }
}

TEST_CASE("The pre-release channel takes the newest release that has update information",
          "[services][updates]")
{
    LocalReleases releases;
    const auto stable = releases.publish("0.9.0");
    const auto candidate = releases.publish("1.0.0-rc.1");

    // GitHub's answer, newest first: a draft, a release without update.json, then ours.
    const auto releaseEntry = [](const juce::String& tag, const juce::File& feed, bool draft) {
        return R"({"tag_name": ")" + tag + R"(", "draft": )" + (draft ? "true" : "false") +
               R"(, "assets": [{"name": "update.json", "browser_download_url": ")" +
               juce::URL(feed).toString(false) + R"("}]})";
    };
    const auto api = releases.directory.file("releases.json");
    REQUIRE(api.replaceWithText("[" + releaseEntry("v2.0.0", stable, true) + R"(, {"tag_name": "v1.5.0", "assets": []}, )" +
                                releaseEntry("v0.9.0", stable, false) + ", " +
                                releaseEntry("v1.0.0-rc.1", candidate, false) + "]"));

    auto options = releases.options("0.8.0", {});
    options.releasesApiOverride = juce::URL(api).toString(false);
    UpdateChecker checker(options);

    UpdatePreferences preRelease;
    preRelease.channel = UpdateChannel::preRelease;
    const auto status = checkNow(checker, preRelease);
    INFO(status.problem);
    REQUIRE(status.state == State::available);
    CHECK(status.release->version.toString() == "1.0.0-rc.1");
}
