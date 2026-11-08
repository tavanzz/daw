#pragma once

#include <juce_core/juce_core.h>

namespace daw::services
{

/**
    Installs a downloaded, verified update in place of the running app, in two halves:
    prepare() while the app runs (on a worker thread: it may take seconds), then
    finishAfterQuit() right before the app quits, which leaves a small helper waiting for
    the app's process to end, putting the new version in place and starting it.

    - macOS: the .dmg is mounted, the app inside must be signed by the running app's team
      (when the running app is signed at all), copied next to the installed one; the
      helper swaps the two bundles and opens the new one.
    - Windows: the installer runs silently, for the same users and into the same folder,
      and starts the app again itself.
    - Linux: the new AppImage is copied next to the running one ($APPIMAGE); the helper
      renames it over the old one and starts it.

    When the app cannot replace itself (a development build, a folder the user cannot
    write, an app started from the .dmg or the Downloads folder), getInstallation() says
    why, and the UI sends the user to the download instead.
*/
class UpdateInstaller
{
public:
    enum class Kind
    {
        macBundle,
        windowsInstall,
        appImage,
        notInstalled ///< a development build, or a layout the installer does not know
    };

    struct Installation
    {
        Kind kind = Kind::notInstalled;
        juce::File location;      ///< the .app bundle, the install folder or the AppImage
        bool allUsers = false;    ///< Windows: installed for everyone (Program Files)
        juce::String whyNot;      ///< empty when an update can be installed in place
    };

    /** Where the running app is installed, and whether it can be updated in place. */
    [[nodiscard]] static Installation describeRunningApp();

    /** Where an app at `location` (the executable's bundle, folder or AppImage) can be
        updated; `appImage` is the value of $APPIMAGE (Linux), if any. Testable. */
    [[nodiscard]] static Installation describe(const juce::File& executable, const juce::String& appImage);

    explicit UpdateInstaller(Installation installation);

    /** Gets the update ready to be put in place. Blocks; empty on success, else why not.
        `download`: the verified file (.dmg, installer, AppImage). */
    [[nodiscard]] juce::String prepare(const juce::File& download);

    /** Starts what finishes the update once process `processId` has ended (the running app,
        which must then quit), and relaunches the new version with `relaunchArguments`.
        False if it could not be started. */
    bool finishAfterQuit(int processId, const juce::StringArray& relaunchArguments);

    /** The helper's script (macOS and Linux): waits for the process, swaps `staged` in for
        `installed` (keeping nothing of the old one), opens it with the arguments. */
    [[nodiscard]] static juce::String finishingScript(int processId, const juce::File& installed,
                                                      const juce::File& staged, const juce::String& launchCommand);

    /** The installer's command line on Windows (Inno Setup options). */
    [[nodiscard]] static juce::String windowsInstallerArguments(const Installation& installation);

    /** The team that signed a macOS bundle ("" if unsigned or ad-hoc). */
    [[nodiscard]] static juce::String signingTeam(const juce::File& bundle);

private:
    juce::String prepareMac(const juce::File& dmg);
    juce::String prepareAppImage(const juce::File& appImage);

    Installation installation;
    juce::File staged;     ///< macOS, Linux: the new version next to the installed one
    juce::File installer;  ///< Windows: the downloaded installer
};

} // namespace daw::services
