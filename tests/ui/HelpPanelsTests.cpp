#include "daw/model/FormatVersion.h"
#include "daw/services/BuildInfo.h"
#include "daw/services/Ed25519.h"
#include "daw/services/UpdateChecker.h"
#include "daw/services/UserSettings.h"
#include "daw/ui/desktop/AboutPanel.h"
#include "daw/ui/desktop/ThemeManager.h"
#include "daw/ui/desktop/UpdatePanel.h"

#include "support/TestAudioFiles.h"

#include <catch2/catch_test_macros.hpp>

using namespace daw;
using services::UpdateChecker;
using State = UpdateChecker::State;

namespace
{

void snapshot(juce::Component& component, const juce::String& name)
{
    const auto folder = juce::SystemStats::getEnvironmentVariable("DAW_UI_SNAPSHOTS", {});
    if (folder.isEmpty())
        return;
    const auto image = component.createComponentSnapshot(component.getLocalBounds(), true, 2.0f);
    const juce::File file = juce::File(folder).getChildFile(name + ".png");
    file.deleteFile();
    juce::FileOutputStream stream(file);
    juce::PNGImageFormat().writeImageToStream(image, stream);
}

// A signed release of a newer version in a folder, and a checker reading it.
struct LocalRelease
{
    daw::test::TemporaryDirectory directory;
    services::ed25519::KeyPair keys;
    juce::File feed;

    explicit LocalRelease(const juce::String& version)
    {
        juce::MemoryBlock seed(services::ed25519::seedSize);
        seed.fillWith(9);
        keys = services::ed25519::keyPairFromSeed(seed);

        const auto asset = directory.file("DAW-installer.bin");
        REQUIRE(asset.replaceWithText("new version"));
        const auto payload = R"({"bundleId": ")" + services::buildInfo().bundleId + R"(", "version": ")" + version +
                             R"(", "notes": "- Faster mixer\n- Fewer bugs", "assets": {")" +
                             services::manifest::platformKey() + R"(": {"url": ")" + juce::URL(asset).toString(false) +
                             R"(", "size": 11, "sha256": ")" + services::sha256Of(asset) + R"("}}})";
        feed = directory.file("update.json");
        REQUIRE(feed.replaceWithText(services::manifest::sign(payload, keys.secretKey), false, false, "\n"));
    }

    std::unique_ptr<UpdateChecker> checker() const
    {
        UpdateChecker::Options options;
        options.currentVersion = "0.0.1";
        options.bundleId = services::buildInfo().bundleId;
        options.publicKey = keys.publicKey;
        options.repositoryUrl = "https://github.com/tavanzz/daw";
        options.feedOverride = juce::URL(feed).toString(false);
        options.downloadFolder = directory.file("Downloads");
        return std::make_unique<UpdateChecker>(options);
    }
};

UpdateChecker::Status waitFor(UpdateChecker& checker, State state)
{
    for (int attempt = 0; attempt < 1000 && checker.getStatus().state != state; ++attempt)
        juce::Thread::sleep(10);
    return checker.getStatus();
}

} // namespace

TEST_CASE("The About panel names the build and shows the shipped licences in every theme", "[ui]")
{
    daw::test::TemporaryDirectory directory;
    const auto licences = directory.file("Licences");
    REQUIRE(licences.getChildFile("JUCE").createDirectory());
    REQUIRE(licences.getChildFile("LICENSE.txt").replaceWithText("Our licence"));
    REQUIRE(licences.getChildFile("THIRD_PARTY.md").replaceWithText("# Third-party components"));
    REQUIRE(licences.getChildFile("JUCE/LICENSE.md").replaceWithText("JUCE's licence"));
    REQUIRE(licences.getChildFile("ChakraPetch").createDirectory());
    REQUIRE(licences.getChildFile("ChakraPetch/OFL.txt").replaceWithText("SIL Open Font License"));

    ui::desktop::ThemeManager themes({});
    for (const auto& theme : themes.getLibrary().getThemes())
    {
        themes.setTheme(theme.id);
        ui::desktop::AboutPanel panel("Not signed", licences);
        panel.setBounds(panel.getLocalBounds());

        const auto& build = services::buildInfo();
        CHECK(panel.getDetails().contains(build.commit));
        CHECK(panel.getDetails().contains("format " + juce::String(model::currentFormatVersion)));
        CHECK(panel.getDetails().contains("Not signed"));
        CHECK(panel.getLicenceNames() ==
              juce::StringArray{build.productName, "Third-party components", "ChakraPetch (font)", "JUCE"});
        CHECK(panel.getShownLicence() == "Our licence");
        panel.showLicence(3);
        CHECK(panel.getShownLicence() == "JUCE's licence");

        const auto image = panel.createComponentSnapshot(panel.getLocalBounds());
        CHECK(image.isValid());
        snapshot(panel, "about-" + theme.id);
    }
}

TEST_CASE("The update panel offers a found release; skipping it is remembered", "[ui]")
{
    ui::desktop::ThemeManager themes({});
    LocalRelease release("9.0.0");
    auto checker = release.checker();
    daw::test::TemporaryDirectory settingsFolder;
    services::UserSettings settings(settingsFolder.file("Settings"), "DAW");

    ui::desktop::AppServices services;
    services.settings = &settings;
    services.updates = checker.get();
    int installs = 0;
    services.installUpdate = [&installs](std::function<void(const juce::String&)>) { ++installs; };
    bool closed = false;

    checker->check(settings.getUpdatePreferences(), true);
    ui::desktop::UpdatePanel panel(services, [&closed] { closed = true; });
    REQUIRE(waitFor(*checker, State::available).state == State::available);
    panel.refresh();

    CHECK(panel.getHeadline().contains("9.0.0 is available"));
    CHECK(panel.getButtonTexts() == juce::StringArray{"Update Now", "Skip This Version", "Later"});
    snapshot(panel, "update-available");

    panel.press("Skip This Version");
    CHECK(closed);
    CHECK(settings.getUpdatePreferences().skippedVersion == "9.0.0");

    // "Update Now": downloads and verifies, then installs without another click.
    panel.press("Update Now");
    REQUIRE(waitFor(*checker, State::ready).state == State::ready);
    panel.refresh();
    CHECK(panel.getButtonTexts().isEmpty());
}

TEST_CASE("A copy that cannot update itself offers the download page instead", "[ui]")
{
    LocalRelease release("9.0.0");
    auto checker = release.checker();

    ui::desktop::AppServices services;
    services.updates = checker.get();
    services.updateInstallProblem = "the app runs from the disk image";
    services.installUpdate = [](std::function<void(const juce::String&)>) {};

    checker->check({}, true);
    ui::desktop::UpdatePanel panel(services, [] {});
    REQUIRE(waitFor(*checker, State::available).state == State::available);
    panel.refresh();
    CHECK(panel.getButtonTexts()[0] == "Open Download Page");

    // Nothing newer: up to date; nothing reachable: the reason and a second try.
    UpdateChecker::Options options;
    options.currentVersion = "0.0.1";
    options.bundleId = services::buildInfo().bundleId;
    options.feedOverride = juce::URL(release.directory.file("missing.json")).toString(false);
    UpdateChecker failing(options);
    services.updates = &failing;
    failing.check({}, true);
    ui::desktop::UpdatePanel failed(services, [] {});
    waitFor(failing, State::failed);
    failed.refresh();
    CHECK(failed.getHeadline() == "Could not check for updates");
    CHECK(failed.getButtonTexts().contains("Try Again"));
}
