#include "daw/services/UpdateInstaller.h"

#include "support/TestAudioFiles.h"

#include <catch2/catch_test_macros.hpp>

#if JUCE_LINUX
 #include <unistd.h>
#endif

using daw::services::UpdateInstaller;
using Kind = UpdateInstaller::Kind;

namespace
{

#if !JUCE_WINDOWS
// The id of a process that has ended (the helper script waits for the app's).
int finishedProcessId()
{
    juce::ChildProcess process;
    REQUIRE(process.start(juce::StringArray{"/bin/sh", "-c", "echo $$"}));
    const auto id = process.readAllProcessOutput().trim().getIntValue();
    REQUIRE(process.waitForProcessToFinish(10000));
    return id;
}

void runScript(const juce::String& script)
{
    juce::ChildProcess shell;
    const auto file = juce::File::createTempFile(".sh");
    REQUIRE(file.replaceWithText(script, false, false, "\n"));
    REQUIRE(shell.start(juce::StringArray{"/bin/sh", file.getFullPathName()}));
    const auto output = shell.readAllProcessOutput();
    REQUIRE(shell.waitForProcessToFinish(20000));
    INFO(script);
    INFO(output);
    CHECK(shell.getExitCode() == 0);
    file.deleteFile();
}

// A stand-in for an application bundle holding one file.
juce::File makeBundle(const juce::File& bundle, const juce::String& content)
{
    const auto executable = bundle.getChildFile("Contents/MacOS/DAW");
    REQUIRE(executable.getParentDirectory().createDirectory());
    REQUIRE(executable.replaceWithText(content));
    return executable;
}
#endif

} // namespace

TEST_CASE("The installer's options update the same installation", "[services][updates]")
{
    UpdateInstaller::Installation perUser;
    perUser.kind = Kind::windowsInstall;
    perUser.location = juce::File::getSpecialLocation(juce::File::tempDirectory).getChildFile("Programs").getChildFile("DAW");
    const auto arguments = UpdateInstaller::windowsInstallerArguments(perUser);
    CHECK(arguments.contains("/SILENT"));
    CHECK(arguments.contains("/CURRENTUSER"));
    CHECK(arguments.contains("/DIR=\"" + perUser.location.getFullPathName() + "\""));

    const auto script = UpdateInstaller::windowsFinishingScript(1234, juce::File::getSpecialLocation(
                                                                          juce::File::tempDirectory)
                                                                          .getChildFile("Setup.exe"),
                                                                perUser, {"--settings-dir=C:\\x", "--smoke-test=new"});
    CHECK(script.contains("PID eq 1234"));
    CHECK(script.contains("Setup.exe\" /SILENT"));
    CHECK(script.contains("\"--settings-dir=C:\\x\" \"--smoke-test=new\""));
    CHECK(script.contains("start \"\" \"" + perUser.location.getFullPathName()));

    auto allUsers = perUser;
    allUsers.allUsers = true;
    CHECK(UpdateInstaller::windowsInstallerArguments(allUsers).contains("/ALLUSERS"));
}

TEST_CASE("Where the app runs decides whether it can update itself", "[services][updates]")
{
    daw::test::TemporaryDirectory directory;

#if JUCE_MAC
    const auto executable = makeBundle(directory.file("Applications/DAW.app"), "x");
    const auto installed = UpdateInstaller::describe(executable, {});
    CHECK(installed.kind == Kind::macBundle);
    CHECK(installed.location == directory.file("Applications/DAW.app"));
    CHECK(installed.whyNot.isEmpty());

    CHECK(UpdateInstaller::describe(juce::File("/Volumes/DAW 0.8.0/DAW.app/Contents/MacOS/DAW"), {})
              .whyNot.contains("disk image"));
    CHECK(UpdateInstaller::describe(
              juce::File("/private/var/folders/x/AppTranslocation/1234/d/DAW.app/Contents/MacOS/DAW"), {})
              .whyNot.contains("Applications"));
    CHECK(UpdateInstaller::describe(directory.file("build/daw_app"), {}).kind == Kind::notInstalled);
#elif JUCE_WINDOWS
    const auto folder = directory.file("Programs/DAW");
    REQUIRE(folder.createDirectory());
    CHECK(UpdateInstaller::describe(folder.getChildFile("DAW.exe"), {}).kind == Kind::notInstalled);
    REQUIRE(folder.getChildFile("unins000.exe").replaceWithText("uninstaller"));
    const auto installed = UpdateInstaller::describe(folder.getChildFile("DAW.exe"), {});
    CHECK(installed.kind == Kind::windowsInstall);
    CHECK(installed.location == folder);
    CHECK_FALSE(installed.allUsers);
#else
    const auto image = directory.file("Apps/DAW-x86_64.AppImage");
    REQUIRE(image.getParentDirectory().createDirectory());
    REQUIRE(image.replaceWithText("old"));
    const auto installed = UpdateInstaller::describe(juce::File("/tmp/.mount_DAW/usr/bin/DAW"), image.getFullPathName());
    CHECK(installed.kind == Kind::appImage);
    CHECK(installed.location == image);
    CHECK(installed.whyNot.isEmpty());
    CHECK(UpdateInstaller::describe(juce::File("/opt/daw/DAW"), {}).kind == Kind::notInstalled);
#endif
}

#if !JUCE_WINDOWS
TEST_CASE("The finishing script waits, swaps in the new version and starts it", "[services][updates]")
{
    daw::test::TemporaryDirectory directory;
    const auto installed = directory.file("Apps/DAW 'quoted'.app");
    const auto staged = directory.file("Apps/.DAW 'quoted'.app.update");
    makeBundle(installed, "old");
    makeBundle(staged, "new");
    const auto started = directory.file("started");

    runScript(UpdateInstaller::finishingScript(finishedProcessId(), installed, staged,
                                               "touch '" + started.getFullPathName() + "'"));

    CHECK(installed.getChildFile("Contents/MacOS/DAW").loadFileAsString() == "new");
    CHECK_FALSE(staged.exists());
    CHECK(started.existsAsFile());
    CHECK(directory.file("Apps").findChildFiles(juce::File::findFilesAndDirectories, false, "*.old").isEmpty());
}
#endif

#if JUCE_MAC
TEST_CASE("A disk image's app is checked and copied next to the installed one", "[services][updates]")
{
    daw::test::TemporaryDirectory directory;
    const auto source = directory.file("image");
    makeBundle(source.getChildFile("DAW.app"), "new");
    const auto dmg = directory.file("DAW-new.dmg");
    juce::ChildProcess hdiutil;
    REQUIRE(hdiutil.start(juce::StringArray{"/usr/bin/hdiutil", "create", "-quiet", "-fs", "HFS+", "-format", "UDZO",
                                            "-srcfolder", source.getFullPathName(), dmg.getFullPathName()}));
    REQUIRE(hdiutil.waitForProcessToFinish(60000));
    REQUIRE(dmg.existsAsFile());

    const auto executable = makeBundle(directory.file("Applications/DAW.app"), "old");
    // An unsigned stand-in: no team to compare, as with unsigned builds.
    CHECK(UpdateInstaller::signingTeam(directory.file("Applications/DAW.app")).isEmpty());

    UpdateInstaller installer(UpdateInstaller::describe(executable, {}));
    CHECK(installer.prepare(dmg).isEmpty());
    const auto staged = directory.file("Applications/.DAW.app.update");
    CHECK(staged.getChildFile("Contents/MacOS/DAW").loadFileAsString() == "new");

    CHECK(installer.prepare(directory.file("missing.dmg")).isNotEmpty());
    REQUIRE(directory.file("not-a-dmg.dmg").replaceWithText("nonsense"));
    CHECK(installer.prepare(directory.file("not-a-dmg.dmg")).contains("cannot be opened"));
}
#endif

#if JUCE_LINUX
TEST_CASE("A new AppImage is put next to the running one, executable", "[services][updates]")
{
    daw::test::TemporaryDirectory directory;
    const auto image = directory.file("DAW-x86_64.AppImage");
    REQUIRE(image.replaceWithText("old"));
    const auto download = directory.file("download.AppImage");
    REQUIRE(download.replaceWithText("new"));

    UpdateInstaller installer(UpdateInstaller::describe({}, image.getFullPathName()));
    CHECK(installer.prepare(download).isEmpty());
    const auto staged = directory.file(".DAW-x86_64.AppImage.update");
    CHECK(staged.loadFileAsString() == "new");
    CHECK(access(staged.getFullPathName().toRawUTF8(), X_OK) == 0);
}
#endif

TEST_CASE("An app started under its previous name hands over to its new one", "[services][updates]")
{
    using Action = UpdateInstaller::Handover::Action;
    daw::test::TemporaryDirectory directory;
    const auto handover = [](const juce::File& executable)
    { return UpdateInstaller::handoverFromPreviousName(executable, "MICINSTATION", "DAW"); };
    const auto write = [](const juce::File& file)
    {
        REQUIRE(file.getParentDirectory().createDirectory());
        REQUIRE(file.replaceWithText("program"));
        return file;
    };

    SECTION("macOS: the old app's bundle, where an update put the new app, is renamed")
    {
        const auto executable = write(directory.file("Applications/DAW.app/Contents/MacOS/MICINSTATION"));
        const auto found = handover(executable);
        CHECK(found.action == Action::renameBundle);
        CHECK(found.from == directory.file("Applications/DAW.app"));
        CHECK(found.to == directory.file("Applications/MICINSTATION.app"));

        // Not over another copy, not on the disk image, not when it has its name already.
        write(directory.file("Applications/MICINSTATION.app/Contents/MacOS/MICINSTATION"));
        CHECK(handover(executable).action == Action::none);
        CHECK(handover(directory.file("Applications/MICINSTATION.app/Contents/MacOS/MICINSTATION")).action ==
              Action::none);
#if JUCE_MAC // a mounted disk image: a path of macOS's own
        CHECK(handover(juce::File("/Volumes/DAW/DAW.app/Contents/MacOS/MICINSTATION")).action == Action::none);
#endif
        CHECK(handover(directory.file("Applications/Mine.app/Contents/MacOS/MICINSTATION")).action == Action::none);
    }

    SECTION("Windows: the old program name starts the new program")
    {
        const auto old = write(directory.file("Programs/DAW/DAW.exe"));
        CHECK(handover(old).action == Action::none); // the old app itself, alone

        const auto program = write(directory.file("Programs/DAW/MICINSTATION.exe"));
        const auto found = handover(old);
        CHECK(found.action == Action::startProgram);
        CHECK(found.to == program);
        CHECK(handover(program).action == Action::none);
    }

    CHECK(UpdateInstaller::handoverFromPreviousName(directory.file("x/DAW"), "DAW", "DAW").action == Action::none);
}

#if !JUCE_WINDOWS
TEST_CASE("The renaming script renames the bundle once the app has quit, then opens it", "[services][updates]")
{
    daw::test::TemporaryDirectory directory;
    const auto from = directory.file("Apps/DAW.app");
    const auto to = directory.file("Apps/MICINSTATION 'new'.app");
    makeBundle(from, "new app");
    const auto renamed = directory.file("renamed");
    const auto kept = directory.file("kept");
    const auto launch = [](const juce::File& marker) { return "touch '" + marker.getFullPathName() + "'"; };

    runScript(UpdateInstaller::renamingScript(finishedProcessId(), from, to, launch(renamed), launch(kept)));
    CHECK(to.getChildFile("Contents/MacOS/DAW").loadFileAsString() == "new app");
    CHECK_FALSE(from.exists());
    CHECK(renamed.existsAsFile());
    CHECK_FALSE(kept.exists());

    // Something has the new name already: the app keeps its own and starts as it is.
    makeBundle(from, "another");
    runScript(UpdateInstaller::renamingScript(finishedProcessId(), from, to, launch(renamed), launch(kept)));
    CHECK(from.getChildFile("Contents/MacOS/DAW").loadFileAsString() == "another");
    CHECK(kept.existsAsFile());
}
#endif
