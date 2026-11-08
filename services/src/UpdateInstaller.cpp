#include "daw/services/UpdateInstaller.h"

#if JUCE_WINDOWS
 #include <windows.h>
#else
 #include <unistd.h>
#endif

namespace daw::services
{

namespace
{

// Runs a program and waits; returns its output (stdout and stderr), or nothing on failure.
struct Run
{
    bool ok = false;
    juce::String output;
};

Run run(const juce::StringArray& command, int timeoutMilliseconds = 120000)
{
    juce::ChildProcess process;
    if (!process.start(command, juce::ChildProcess::wantStdOut | juce::ChildProcess::wantStdErr))
        return {};
    Run result;
    result.output = process.readAllProcessOutput();
    if (!process.waitForProcessToFinish(timeoutMilliseconds))
    {
        process.kill();
        return result;
    }
    result.ok = process.getExitCode() == 0;
    return result;
}

// A word for /bin/sh: in single quotes, with single quotes escaped.
juce::String shellQuoted(const juce::String& text)
{
    return "'" + text.replace("'", "'\\''") + "'";
}

juce::File stagedNextTo(const juce::File& installed)
{
    return installed.getSiblingFile("." + installed.getFileName() + ".update");
}

} // namespace

UpdateInstaller::Installation UpdateInstaller::describeRunningApp()
{
    return describe(juce::File::getSpecialLocation(juce::File::currentExecutableFile),
                    juce::SystemStats::getEnvironmentVariable("APPIMAGE", {}));
}

UpdateInstaller::Installation UpdateInstaller::describe(const juce::File& executable, const juce::String& appImage)
{
    Installation installation;

#if JUCE_MAC
    juce::ignoreUnused(appImage);
    // .../DAW.app/Contents/MacOS/DAW
    const auto bundle = executable.getParentDirectory().getParentDirectory().getParentDirectory();
    if (!bundle.hasFileExtension(".app"))
    {
        installation.whyNot = "this copy of the app is not an application bundle";
        return installation;
    }
    installation.kind = Kind::macBundle;
    installation.location = bundle;
    const auto path = bundle.getFullPathName();
    if (path.contains("/AppTranslocation/"))
        installation.whyNot = "macOS runs this copy from a temporary place: move the app to the Applications "
                              "folder, open it from there, then update";
    else if (path.startsWith("/Volumes/"))
        installation.whyNot = "the app runs from the disk image: drag it to the Applications folder first";
    else if (!bundle.getParentDirectory().hasWriteAccess())
        installation.whyNot = "the app cannot replace itself in " + bundle.getParentDirectory().getFullPathName() +
                              ": install the new version from the disk image";
#elif JUCE_WINDOWS
    juce::ignoreUnused(appImage);
    const auto folder = executable.getParentDirectory();
    if (!folder.getChildFile("unins000.exe").existsAsFile())
    {
        installation.whyNot = "this copy of the app was not installed with the installer";
        return installation;
    }
    installation.kind = Kind::windowsInstall;
    installation.location = folder;
    const auto programFiles = juce::File::getSpecialLocation(juce::File::globalApplicationsDirectory);
    installation.allUsers = folder.isAChildOf(programFiles) ||
                            folder.isAChildOf(juce::File::getSpecialLocation(juce::File::globalApplicationsDirectoryX86));
#else
    juce::ignoreUnused(executable);
    const juce::File image = juce::File::isAbsolutePath(appImage) ? juce::File(appImage) : juce::File();
    if (!image.existsAsFile())
    {
        installation.whyNot = "this copy of the app is not an AppImage";
        return installation;
    }
    installation.kind = Kind::appImage;
    installation.location = image;
    if (!image.getParentDirectory().hasWriteAccess())
        installation.whyNot = "the app cannot replace itself in " + image.getParentDirectory().getFullPathName();
#endif
    return installation;
}

UpdateInstaller::UpdateInstaller(Installation installationToUpdate)
    : installation(std::move(installationToUpdate))
{
}

juce::String UpdateInstaller::prepare(const juce::File& download)
{
    if (installation.whyNot.isNotEmpty())
        return installation.whyNot;
    if (!download.existsAsFile())
        return "the downloaded update is missing";

    switch (installation.kind)
    {
    case Kind::macBundle:
        return prepareMac(download);
    case Kind::appImage:
        return prepareAppImage(download);
    case Kind::windowsInstall:
        installer = download;
        return {};
    case Kind::notInstalled:
    default:
        return "this copy of the app cannot be updated in place";
    }
}

int UpdateInstaller::currentProcessId()
{
#if JUCE_WINDOWS
    return static_cast<int>(GetCurrentProcessId());
#else
    return static_cast<int>(getpid());
#endif
}

juce::String UpdateInstaller::signingTeam(const juce::File& bundle)
{
    const auto result = run({"/usr/bin/codesign", "-dv", "--verbose=2", bundle.getFullPathName()});
    for (const auto& line : juce::StringArray::fromLines(result.output))
        if (line.startsWith("TeamIdentifier="))
        {
            const auto team = line.fromFirstOccurrenceOf("=", false, false).trim();
            return team == "not set" ? juce::String() : team;
        }
    return {};
}

juce::String UpdateInstaller::prepareMac(const juce::File& dmg)
{
    const auto mountPoint = juce::File::createTempFile("mount");
    mountPoint.createDirectory();
    if (!run({"/usr/bin/hdiutil", "attach", "-nobrowse", "-readonly", "-noautoopen", "-mountpoint",
              mountPoint.getFullPathName(), dmg.getFullPathName()})
             .ok)
    {
        mountPoint.deleteRecursively();
        return "the disk image of the update cannot be opened";
    }

    juce::String problem;
    const auto apps = mountPoint.findChildFiles(juce::File::findDirectories, false, "*.app");
    if (apps.isEmpty())
        problem = "the disk image of the update holds no app";

    if (problem.isEmpty())
    {
        // A signed app accepts only an update signed by the same team.
        const auto& newApp = apps.getFirst();
        const auto team = signingTeam(installation.location);
        if (team.isNotEmpty() &&
            (!run({"/usr/bin/codesign", "--verify", "--deep", "--strict", newApp.getFullPathName()}).ok ||
             signingTeam(newApp) != team))
            problem = "the update is not signed by the developer of this app";
    }

    if (problem.isEmpty())
    {
        staged = stagedNextTo(installation.location);
        staged.deleteRecursively();
        if (!run({"/usr/bin/ditto", apps.getFirst().getFullPathName(), staged.getFullPathName()}).ok)
            problem = "the update cannot be copied next to the app";
        else
            run({"/usr/bin/xattr", "-dr", "com.apple.quarantine", staged.getFullPathName()});
    }

    run({"/usr/bin/hdiutil", "detach", "-force", mountPoint.getFullPathName()});
    mountPoint.deleteRecursively();
    if (problem.isNotEmpty())
        staged.deleteRecursively();
    return problem;
}

juce::String UpdateInstaller::prepareAppImage(const juce::File& appImage)
{
    staged = stagedNextTo(installation.location);
    staged.deleteFile();
    if (!appImage.copyFileTo(staged) || !staged.setExecutePermission(true))
    {
        staged.deleteFile();
        return "the update cannot be copied next to the app";
    }
    return {};
}

juce::String UpdateInstaller::finishingScript(int processId, const juce::File& installed, const juce::File& staged,
                                              const juce::String& launchCommand)
{
    const auto target = shellQuoted(installed.getFullPathName());
    const auto update = shellQuoted(staged.getFullPathName());
    const auto old = shellQuoted(installed.getFullPathName() + ".old");
    juce::String script;
    script << "#!/bin/sh\n"
           << "# Finishes an update once the app has quit (services/UpdateInstaller.cpp).\n"
           << "while kill -0 " << processId << " 2>/dev/null; do sleep 0.2; done\n"
           << "rm -rf " << old << "\n"
           << "if mv " << target << " " << old << "; then\n"
           << "  if mv " << update << " " << target << "; then rm -rf " << old << "; else mv " << old << " "
           << target << "; fi\n"
           << "fi\n"
           << launchCommand << "\n";
    return script;
}

juce::String UpdateInstaller::windowsInstallerArguments(const Installation& target)
{
    return juce::String("/SILENT /SUPPRESSMSGBOXES /NORESTART ") + (target.allUsers ? "/ALLUSERS" : "/CURRENTUSER") +
           " /DIR=\"" + target.location.getFullPathName() + "\"";
}

juce::String UpdateInstaller::windowsFinishingScript(int processId, const juce::File& setup,
                                                     const Installation& target, const juce::StringArray& arguments)
{
    // Arguments are quoted for the command line; they never hold double quotes.
    juce::StringArray quoted;
    for (const auto& argument : arguments)
        quoted.add("\"" + argument.removeCharacters("\"") + "\"");
    const auto pid = juce::String(processId);
    juce::String script;
    script << "@echo off\r\n"
           << "rem Finishes an update once the app has quit (services/UpdateInstaller.cpp).\r\n"
           << ":wait\r\n"
           << "tasklist /FI \"PID eq " << pid << "\" /NH 2>NUL | find \" " << pid << " \" >NUL\r\n"
           << "if not errorlevel 1 (\r\n  ping -n 2 127.0.0.1 >NUL\r\n  goto wait\r\n)\r\n"
           << "\"" << setup.getFullPathName() << "\" " << windowsInstallerArguments(target) << "\r\n"
           << "start \"\" \"" << target.location.getChildFile(juce::File::getSpecialLocation(
                                                           juce::File::currentExecutableFile).getFileName())
                                        .getFullPathName()
           << "\" " << quoted.joinIntoString(" ") << "\r\n";
    return script;
}

bool UpdateInstaller::finishAfterQuit(int processId, const juce::StringArray& relaunchArguments)
{
    if (installation.kind == Kind::windowsInstall)
    {
        if (!installer.existsAsFile())
            return false;
        const auto script = juce::File::createTempFile(".cmd");
        if (!script.replaceWithText(windowsFinishingScript(processId, installer, installation, relaunchArguments)))
            return false;
        juce::ChildProcess helper;
        return helper.start(juce::StringArray{"cmd.exe", "/c", script.getFullPathName()}, 0);
    }

    if (!staged.exists())
        return false;

    juce::String launch;
    juce::StringArray arguments;
    for (const auto& argument : relaunchArguments)
        arguments.add(shellQuoted(argument));
    if (installation.kind == Kind::macBundle)
        launch = "/usr/bin/open -n " + shellQuoted(installation.location.getFullPathName()) +
                 (arguments.isEmpty() ? juce::String() : " --args " + arguments.joinIntoString(" "));
    else
        launch = shellQuoted(installation.location.getFullPathName()) + " " + arguments.joinIntoString(" ") +
                 " >/dev/null 2>&1 &";

    const auto script = juce::File::createTempFile(".sh");
    if (!script.replaceWithText(finishingScript(processId, installation.location, staged, launch), false, false, "\n"))
        return false;

    // Its own process, which outlives the app.
    juce::ChildProcess helper;
    return helper.start(juce::StringArray{"/bin/sh", script.getFullPathName()}, 0);
}

} // namespace daw::services
