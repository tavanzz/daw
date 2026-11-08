# Privacy

What DAW sends over the network, what it keeps on your computer, and how to change it.
DAW has no accounts, no analytics and no advertising. Your projects, recordings and
settings never leave your computer.

## What DAW sends

### Update checks (on by default; Help > Check for Updates at Launch)

At every launch, and when you choose Help > Check for Updates..., DAW asks GitHub, where
its releases are published, whether a newer version exists. It reads a small file from
`https://github.com/tavanzz/daw/releases` (with "Include Pre-releases" on, GitHub's
releases API at `api.github.com`). The request says which version of DAW is asking
("DAW/0.8.0") and, like any web request, reaches GitHub from your IP address. Nothing
else is sent. If you choose Update Now, DAW downloads the new installer from the same
place. GitHub's own privacy statement applies to those requests:
https://docs.github.com/site-policy/privacy-policies/github-general-privacy-statement

### Crash reports (only if you allow it)

The first time DAW starts it asks whether it may send crash reports. Until you answer
yes, nothing about a crash leaves your computer. You can change your answer at any time
with Help > Send Crash Reports.

When DAW crashes, the next start turns what was recorded at the crash into a report. If
you allowed it, that report is sent to Sentry (sentry.io, operated by Functional
Software, Inc.), in its European Union region (Frankfurt, Germany), where it is kept for
up to 30 days and seen only by DAW's developer. A report contains only:

- the version of DAW and the commit it was built from;
- the operating system's name and version and the processor's architecture (e.g. arm64);
- what kind of crash it was (for example "segmentation fault");
- where in the program it happened: the addresses of the functions that were running and
  the names of the program files they belong to (DAW itself, the system's libraries, a
  plugin), with your home folder written as `~` in their paths;
- the name and format of the plugin that was running when it happened, if one was.

A report never contains: your name, your user name, your computer's name, your email,
an identifier for you or your computer, the contents of memory, your projects, their
names or file names, recordings, audio, your settings or your log. DAW asks Sentry not
to store IP addresses.

## What DAW keeps on your computer

- **Settings**: `~/Library/Application Support/DAW` (macOS), `%APPDATA%\DAW` (Windows),
  `~/.config/DAW` (Linux): your audio device, theme, plugin list and choices such as the
  ones above.
- **Logs**: `~/Library/Logs/DAW` (macOS), the `Logs` folder next to the settings
  (Windows, Linux): one file per session, the last 20 sessions. They record what DAW did
  (the audio device it opened, plugins it scanned, errors it showed, the names of the
  project files it opened and saved) to help find out what went wrong. Help > Open Log
  Folder shows them. They are never sent: you can send one yourself if you want help.
- **Crash reports**: the `Crashes` folder inside the log folder, the last 20, in the
  readable form described above, whether or not they were sent.

Deleting those folders removes everything DAW keeps; DAW starts as new afterwards.

## Changes

This page changes with DAW; the version in the repository is the current one.
