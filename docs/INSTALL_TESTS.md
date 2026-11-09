# Installation tests

What to try by hand once a release candidate is published, as friends will: installing
from nothing, updating from one version to the next, and Windows. The release workflow
already checks signatures, installation, a project and updates on every platform
(docs/RELEASING.md); these checks are about what only a person sees: the system's
dialogs, the first launch, the update panel.

They use two pre-releases built with the update key (docs/RELEASING.md, step 1), from
https://github.com/tavanzz/daw/releases: `v0.8.0-rc.4`, the last one called DAW, and
`v0.8.0-rc.5`, the first one called MICINSTATION, which an older copy looking for
pre-releases is offered. (`v0.8.0-rc.1` was the pipeline's first trial, signed with a
throwaway key: it cannot update itself. `v0.8.0-rc.2` and `rc.3` work as rc.4, but
may ask for the microphone at launch.)

Before starting on a Mac where DAW ran before, keep your settings and logs aside, so that
you see what a new user sees:

```bash
cd ~/Library/Application\ Support && mv DAW DAW.before-tests; [ -d MICINSTATION ] && mv MICINSTATION MICINSTATION.before-tests; cd ~/Library/Logs && mv DAW DAW.before-tests; true
```

(Afterwards they go back: see the end of the Mac checks. Your plugin list is in them.)

## Mac, from nothing (rc.4, still called DAW)

1. Download `DAW-0.8.0-rc.4-macOS.dmg` with Safari. Open it: a window shows DAW and a
   link to Applications. Drag DAW onto Applications, eject the disk image.
2. Open DAW from Applications (or Launchpad).
   - With the Apple account set up: it opens after macOS's usual "downloaded from the
     internet" question, which names you as the developer.
   - Without it (the release notes say "not signed with an Apple Developer ID"): macOS
     refuses. Open System Settings > Privacy & Security, scroll to "DAW was blocked",
     choose Open Anyway and confirm. Only the first time.
   - macOS does not ask for the microphone now, even with your Komplete Audio 6 (rc.2 and
     rc.3 did): only when a track needs an input (check 14).
3. The welcome window, "1 of 3": your interface is preselected; press Test, you hear a
   tone in it. Next: click each theme, the whole app changes at once; keep one. Next:
   "Crash reports": Start is greyed until you choose Send or Don't send. Choose one,
   Start. The window closes; nothing else is asked.
4. Quit and open DAW again: no welcome window.
5. Help > About DAW: version 0.8.0-rc.4, a commit, "macOS, arm64" (or x86_64 on an Intel
   Mac), format 6, protocol 1, settings 1, and whether it is signed. The Licences menu
   lists DAW, Third-party components, the fonts, JUCE's VST3 SDK, Monocypher,
   sentry-native, mpack, jsmn; each shows its text. The three links open the repository,
   the releases and the privacy page.
6. A project with plugins: File > Plugin Manager... shows the plugins found (the first
   scan takes a few minutes, in the background; the same count as in Phase 7, 688 on this
   Mac, 6 not loaded). Make a MIDI track with Serum 2 and an audio track with Soundtoys
   EchoBoy, play them, open their editors: they work as in the development build. Save
   the project (it is a `.dawproject`).
7. Help > Open Log Folder: a Finder window with one log per launch. The newest starts
   with the version and the system and names your device; your home folder is written
   "~". Help > Privacy... opens the privacy page.
8. Help > Send Crash Reports shows your answer from step 3; click it to change it.

## Mac, the update that renames the app (rc.4 to rc.5)

9. In DAW (rc.4): Help > Include Pre-releases (ticked), then Help > Check for Updates...:
   the update panel says "DAW 0.8.0-rc.5 is available" (it is still the old app talking;
   you have 0.8.0-rc.4) with the notes of rc.5, and three buttons.
10. Make an unsaved change (add a track), then Update Now: a progress bar, then DAW asks
    whether to save; save the project. DAW quits and, a few seconds later, MICINSTATION
    starts by itself (it opens twice in a row, quickly: the first time it renames itself).
11. Applications holds MICINSTATION and no DAW; the menu bar says MICINSTATION; the logo
    reads MIC IN STATION, each theme its own way (View > Theme > Show Previews shows all
    three). Help > About MICINSTATION: 0.8.0-rc.5. Your theme, device, plugin list and
    crash-report answer are as before; no welcome window.
12. Help > Open Log Folder opens `~/Library/Logs/MICINSTATION`: the logs of DAW's sessions
    are there too, now named "MICINSTATION ...". The newest says "Took over the settings
    of DAW (the app's previous name)".
13. Projects: File > Open Recent (or File > Open) opens the project of step 10, a
    `.dawproject`: it plays as before. Change something and Save: it stays
    `.dawproject`, nothing else appears next to it. File > Save As... proposes `.micin`;
    save a copy. In the Finder, double-click the `.micin` copy: MICINSTATION opens it.
    Double-click the `.dawproject`: MICINSTATION opens it too, unless an app for Bitwig's
    DAWproject format (Bitwig Studio, Studio One, Cubase) claims it: then right-click >
    Open With > MICINSTATION.
14. The microphone: macOS knows MICINSTATION as a new app. Play the project with the
    interface as the output: macOS asks nothing, no orange dot in the menu bar. Arm an
    audio track (or switch its monitoring to On): now macOS asks whether MICINSTATION may
    use the microphone. Allow: the track's input meter moves when you play into the
    interface. (Don't Allow instead: arming refuses with a message saying how to allow it
    in System Settings.)
15. Quit, open again: at launch, after five seconds, nothing is offered (you are up to
    date). Help > Check for Updates...: "MICINSTATION 0.8.0-rc.5 is up to date."
16. Optional: with "Include Pre-releases" off and Check for Updates at Launch off, launch:
    no network request is made (the log shows no "Updates:" line).

Afterwards, put your settings and logs back. Your own settings go back under the old
name, so that MICINSTATION takes them over the next time it starts, as it would have
after a real update:

```bash
cd ~/Library/Application\ Support && rm -rf DAW MICINSTATION && mv DAW.before-tests DAW; [ -d MICINSTATION.before-tests ] && mv MICINSTATION.before-tests MICINSTATION; cd ~/Library/Logs && rm -rf MICINSTATION && mv DAW.before-tests DAW; true
```

## What to ask a friend on Windows

Send them the link to the releases (rc.4, then rc.5) and this list.

17. Download `DAW-0.8.0-rc.4-Windows-x64-Setup.exe` and run it. If Windows says
    "Windows protected your PC": More info > Run anyway. (If Windows says the app was
    blocked by Smart App Control, it cannot be installed while that is on: tell me.)
18. The installer asks "Install for me only" or "for all users": choose "for me only".
    Italian or English as Windows is set. Finish with "Launch DAW" ticked.
19. The welcome window: choose the audio output (Windows Audio, their speakers or
    interface), Test, a theme, an answer about crash reports, Start.
20. In DAW: Help > Include Pre-releases, Help > Check for Updates...: rc.5 is offered.
    Update Now: DAW closes, the installer runs by itself (no questions, for "only for me"),
    and MICINSTATION starts: Help > About says 0.8.0-rc.5, the theme and device are as
    chosen in step 19, no welcome window.
21. The Start menu has MICINSTATION and no DAW; Settings > Apps > Installed apps has one
    entry, MICINSTATION. Double-click a `.micin` and a `.dawproject` file (send them one of
    each): MICINSTATION opens both.
22. Settings > Apps > Installed apps > MICINSTATION > Uninstall: it goes; the Start menu
    entry too.
23. If anything goes wrong: Help > Open Log Folder (or `%APPDATA%\MICINSTATION\Logs`, or
    `%APPDATA%\DAW\Logs` before the update), and send the newest log, which shows the
    version and nothing personal but file names; and Help > About's version line.

A friend who installs rc.5 from nothing (`MICINSTATION-0.8.0-rc.5-Windows-x64-Setup.exe`)
sees the same, with MICINSTATION from the start.

Linux friends: the AppImage, `chmod +x`, run; the same welcome window and updates (the
AppImage replaces itself, keeping its file name; the settings move from `~/.config/DAW`
to `~/.config/MICINSTATION`).
