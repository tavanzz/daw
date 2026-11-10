# Privacy

What MICINSTATION sends over the network, what it keeps on your computer, and how to change it.
MICINSTATION has no accounts, no analytics and no advertising. Your projects, recordings and
settings never leave your computer, unless you host or join a co-op session (below).

## What MICINSTATION sends

### Update checks (on by default; Help > Check for Updates at Launch)

At every launch, and when you choose Help > Check for Updates..., MICINSTATION asks GitHub, where
its releases are published, whether a newer version exists. It reads a small file from
`https://github.com/tavanzz/daw/releases` (with "Include Pre-releases" on, GitHub's
releases API at `api.github.com`). The request says which version of MICINSTATION is asking
("MICINSTATION/0.8.0") and, like any web request, reaches GitHub from your IP address. Nothing
else is sent. If you choose Update Now, MICINSTATION downloads the new installer from the same
place. GitHub's own privacy statement applies to those requests:
https://docs.github.com/site-policy/privacy-policies/github-general-privacy-statement

### Crash reports (only if you allow it)

The first time MICINSTATION starts it asks whether it may send crash reports. Until you answer
yes, nothing about a crash leaves your computer. You can change your answer at any time
with Help > Send Crash Reports.

When MICINSTATION crashes, the next start turns what was recorded at the crash into a report. If
you allowed it, that report is sent to Sentry (sentry.io, operated by Functional
Software, Inc.), in its European Union region (Frankfurt, Germany), where it is kept for
up to 30 days and seen only by MICINSTATION's developer. A report contains only:

- the version of MICINSTATION and the commit it was built from;
- the operating system's name and version and the processor's architecture (e.g. arm64);
- what kind of crash it was (for example "segmentation fault");
- where in the program it happened: the addresses of the functions that were running and
  the names of the program files they belong to (MICINSTATION itself, the system's libraries, a
  plugin), with your home folder written as `~` in their paths;
- the name and format of the plugin that was running when it happened, if one was.

A report never contains: your name, your user name, your computer's name, your email,
an identifier for you or your computer, the contents of memory, your projects, their
names or file names, recordings, audio, your settings or your log. MICINSTATION asks Sentry not
to store IP addresses.

### Co-op sessions (only when you host or join one)

A co-op session shares a project with the people in it, through a co-op server that one
of you runs (`docs/SERVER.md`): MICINSTATION connects to it only while you are in a
session, over TLS 1.3, and checks that it is the server the link names. What goes there:

- the project as everyone sees it: tracks, clips, notes, mixer, effects, plugin states
  and automation, and every change made to it, with the name of whoever made it; not
  your personal settings (armed tracks, inputs, monitoring, solo, snap and grid,
  metronome, count-in, punch, recording format);
- the audio files the project uses (imports, recordings, samples), from your copy's
  folder, so that the others can download them;
- the name you chose for the session, where you are looking (your cursor, selection,
  playhead and the track you look at) and when you play for everyone;
- the version of MICINSTATION and, like any connection, your IP address.

The server passes all of it on to the session's other participants. It keeps the
project, its files and the participants' names until the host closes the session (or for
30 days after the last time anyone joined it), and it logs what happens with random ids
and numbers only: no names, no contents, no addresses. Whoever runs the server can read
what it keeps; choose a server you trust. MICINSTATION's developer runs none and sees
nothing of it.

Your copy of a co-op project is an ordinary folder on your computer
(`Documents/MICINSTATION/Co-op/`); its `.coop` folder holds what lets you rejoin the
session (the server, your number in the session and its secret token), readable by you
only.

## What MICINSTATION keeps on your computer

- **Settings**: `~/Library/Application Support/MICINSTATION` (macOS), `%APPDATA%\MICINSTATION`
  (Windows), `~/.config/MICINSTATION` (Linux): your audio device, theme, plugin list and choices such as the
  ones above.
- **Logs**: `~/Library/Logs/MICINSTATION` (macOS), the `Logs` folder next to the settings
  (Windows, Linux): one file per session, the last 20 sessions. They record what MICINSTATION did
  (the audio device it opened, plugins it scanned, errors it showed, the names of the
  project files it opened and saved) to help find out what went wrong. Help > Open Log
  Folder shows them. They are never sent: you can send one yourself if you want help.
- **Crash reports**: the `Crashes` folder inside the log folder, the last 20, in the
  readable form described above, whether or not they were sent.

Deleting those folders removes everything MICINSTATION keeps; MICINSTATION starts as new afterwards.

Up to v0.8.0-rc.4 the app was called DAW and kept these in folders of that name. The
first start of MICINSTATION copies DAW's settings (DAW's folder stays: delete it when
you no longer need DAW) and moves its logs and crash reports into its own folders.

## Changes

This page changes with MICINSTATION; the version in the repository is the current one.
