# Installation tests

What to try by hand once a release candidate is published, as friends will: installing
from nothing, updating from one version to the next, and Windows. The release workflow
already checks signatures, installation, a project and updates on every platform
(docs/RELEASING.md); these checks are about what only a person sees: the system's
dialogs, the first launch, the update panel.

They use two pre-releases built with the update key (docs/RELEASING.md, step 1), from
https://github.com/tavanzz/daw/releases: `v0.8.0-rc.3`, and `v0.8.0-rc.4`, the newest, which
an older copy looking for pre-releases is offered. (`v0.8.0-rc.1` was the pipeline's first
trial, signed with a throwaway key: it cannot update itself. `v0.8.0-rc.2` works as rc.3.)
Before starting on a Mac where DAW ran before, keep your settings aside, so that you see
what a new user sees:

```bash
mv ~/Library/Application\ Support/DAW ~/Library/Application\ Support/DAW.before-tests
```

(Afterwards: delete the new folder and move this one back. Your plugin list is in it.)

## Mac, from nothing

1. Download `DAW-0.8.0-rc.3-macOS.dmg` with Safari. Open it: a window shows DAW and a
   link to Applications. Drag DAW onto Applications, eject the disk image.
2. Open DAW from Applications (or Launchpad).
   - With the Apple account set up: it opens after macOS's usual "downloaded from the
     internet" question, which names you as the developer.
   - Without it (the release notes say "not signed with an Apple Developer ID"): macOS
     refuses. Open System Settings > Privacy & Security, scroll to "DAW was blocked",
     choose Open Anyway and confirm. Only the first time.
   - In rc.3, if your audio interface has inputs, macOS may ask now whether DAW may use
     the microphone: allow it (recording needs it). From rc.4 on it asks only when a track
     needs an input (checks 21 to 23).
3. The welcome window, "1 of 3": your interface is preselected; press Test, you hear a
   tone in it. Next: click each theme, the whole app changes at once; keep one. Next:
   "Crash reports": Start is greyed until you choose Send or Don't send. Choose one,
   Start. The window closes; nothing else is asked.
4. Quit and open DAW again: no welcome window.
5. Help > About DAW: version 0.8.0-rc.3, a commit, "macOS, arm64" (or x86_64 on an Intel
   Mac), format 6, protocol 1, settings 1, and whether it is signed. The Licences menu
   lists DAW, Third-party components, the fonts, JUCE's VST3 SDK, Monocypher,
   sentry-native, mpack, jsmn; each shows its text. The three links open the repository,
   the releases and the privacy page.
6. A project with plugins: File > Plugin Manager... shows the plugins found (the first
   scan takes a few minutes, in the background; the same count as in Phase 7, 688 on this
   Mac, 6 not loaded). Make a MIDI track with Serum 2 and an audio track with Soundtoys
   EchoBoy, play them, open their editors: they work as in the development build.
   (PACE-protected plugins need the app's `allow-unsigned-executable-memory`
   entitlement: a failure here would show as crashed plugins in the Plugin Manager.)
7. Help > Open Log Folder: a Finder window with one log per launch. The newest starts
   with the version and the system and names your device; your home folder is written
   "~". Help > Privacy... opens the privacy page.
8. Help > Send Crash Reports shows your answer from step 3; click it to change it.

## Mac, an update from one version to the next

9. In rc.3: Help > Include Pre-releases (ticked), then Help > Check for Updates...: the
   update panel says "DAW 0.8.0-rc.4 is available" (you have 0.8.0-rc.3) with the notes
   of rc.4, and three buttons.
10. Make an unsaved change (add a track), then Update Now: a progress bar, then DAW asks
    whether to save; save the project. DAW quits and, a few seconds later, starts again
    by itself.
11. Help > About: 0.8.0-rc.4. Your theme, device, plugin list and the project's recent
    file are as before; no welcome window; the project opens as saved.
12. Quit, open again: at launch, after five seconds, nothing is offered (you are up to
    date). Help > Check for Updates...: "DAW 0.8.0-rc.4 is up to date."
13. Optional: with "Include Pre-releases" off and Check for Updates at Launch off, launch:
    no network request is made (the log shows no "Updates:" line).

Afterwards, put your settings back:

```bash
rm -rf ~/Library/Application\ Support/DAW && mv ~/Library/Application\ Support/DAW.before-tests ~/Library/Application\ Support/DAW
```

## What to ask a friend on Windows

Send them the link to the release (rc.3, then rc.4) and this list.

14. Download `DAW-0.8.0-rc.3-Windows-x64-Setup.exe` and run it. If Windows says
    "Windows protected your PC": More info > Run anyway. (If Windows says the app was
    blocked by Smart App Control, it cannot be installed while that is on: tell me.)
15. The installer asks "Install for me only" or "for all users": choose "for me only".
    Italian or English as Windows is set. Finish with "Launch DAW" ticked.
16. The welcome window: choose the audio output (Windows Audio, their speakers or
    interface), Test, a theme, an answer about crash reports, Start.
17. Start menu > DAW opens it again with no welcome window. Double-click a `.dawproject`
    file (send them one): it opens in DAW.
18. In DAW: Help > Include Pre-releases, Help > Check for Updates...: rc.4 is offered.
    Update Now: DAW closes, the installer runs by itself (no questions, for "only for me"),
    DAW starts again: Help > About says 0.8.0-rc.4.
19. Settings > Apps > Installed apps > DAW > Uninstall: it goes; the Start menu entry too.
20. If anything goes wrong: Help > Open Log Folder (or `%APPDATA%\DAW\Logs`), and send the
    newest log, which shows the version and nothing personal but file names; and Help >
    About's version line.

Linux friends: the AppImage, `chmod +x`, run; the same welcome window and updates (the
AppImage replaces itself).

## Mac, the microphone (rc.4)

Up to rc.3 macOS asked for the microphone at the first launch whenever the audio interface
also has inputs (your Komplete Audio 6). From rc.4 on it must ask only when a track needs an
input.

21. In rc.4 (after checks 9 to 11, or installed from `DAW-0.8.0-rc.4-macOS.dmg` as in
    step 1): quit DAW, then make macOS forget its answer, so that it can ask again:

    ```bash
    tccutil reset Microphone io.github.tavanzz.daw
    ```

22. Open DAW with the interface as its output and play a project: you hear it, and macOS
    asks nothing (no microphone question, no orange dot in the menu bar).
23. Arm an audio track (or switch its monitoring to On): now macOS asks whether DAW may use
    the microphone. Allow: the track's input meter moves when you play into the
    interface. (Don't Allow instead: arming refuses with a message saying how to allow it in
    System Settings.)
