# DAW

A cross-platform digital audio workstation written in C++20 with
[JUCE](https://juce.com). "DAW" is a working title.

Targets: Windows, macOS and Linux now; iOS/iPadOS and Android later.

> **Licensing: check the JUCE licence before any commercial distribution.**
> JUCE is dual-licensed under the [AGPLv3](https://www.gnu.org/licenses/agpl-3.0.en.html)
> and the commercial [JUCE 9 licence](https://juce.com/legal/juce-9-licence/).
> Distributing this application under the AGPLv3 means releasing its complete
> source code under the AGPLv3; distributing it as closed source requires a
> suitable commercial JUCE licence. Review the current terms before releasing
> anything. See [Licence](#licence).

## Status

Phase 1 (v0.1.0): play a single WAV or AIFF file with transport controls, waveform
display and level meter.

Phase 2 (v0.2.0): multitrack and mixer. Tracks with clips on a timeline (move,
trim, delete; one load per file), a ruler in time and bars with grid and snap, zoom
and scroll, a loop region; a mixer with volume, pan, mute, solo and meters per track
and a master bus; WAV export through the same code as playback; undo/redo; project
format version 2. See [CLAUDE.md](CLAUDE.md#status) for the steps.

## Building

### Requirements

- CMake 3.25 or newer, Ninja, and a C++20 compiler.
- **macOS 12+**: Xcode or the Xcode Command Line Tools (`brew install cmake ninja`).
- **Windows 10+ (64-bit)**: Visual Studio 2022 with the C++ workload. Run the
  commands from a "x64 Native Tools Command Prompt" so that Ninja finds MSVC.
- **Linux** (tested on Ubuntu 24.04):

  ```bash
  sudo apt install build-essential cmake ninja-build pkg-config \
      libasound2-dev libfreetype-dev libfontconfig1-dev \
      libx11-dev libxcomposite-dev libxcursor-dev libxext-dev \
      libxinerama-dev libxrandr-dev libxrender-dev libxi-dev
  ```

JUCE and Catch2 are downloaded automatically, at pinned versions verified by
SHA-256, the first time you configure.

### Build, test, run

```bash
cmake --preset debug
cmake --build --preset debug
ctest --preset debug
```

Use the `release` preset for an optimised build. On macOS, the `macos-universal`
preset builds a universal binary for Apple Silicon and Intel Macs.

The macOS app bundle ends up in `build/<preset>/app/daw_app_artefacts/<Config>/DAW.app`.

## Using the app

Add WAV or AIFF files with File > Import Audio (they land at the playhead, one new
track each) or by dragging them onto the tracks: one file dropped on a track goes onto
that track. Files are converted to the output device's sample rate when needed.

- Drag a clip to move it (also onto another track), drag its left or right edge to
  trim it, click it to select it (Shift adds to the selection). Moves and edges snap
  to the grid unless Cmd is held. Where clips overlap, the one on top is heard.
- Click the ruler or an empty part of a track to move the playhead; drag across the
  ruler to set the loop region.
- Double-click the tempo or the time signature to change them; double-click a track
  name to rename it; right-click a track header to delete the track.
- In the mixer, double-click a fader or a pan knob to reset it.

| Action | Shortcut |
|--------|----------|
| Play / pause | Space |
| Stop (back to where playback started; again: to the start) | Return |
| Loop / Snap / Mixer | L / N / X |
| Undo / Redo | Cmd+Z / Shift+Cmd+Z |
| Delete the selected clips | Backspace or Delete |
| New track | Cmd+T |
| Import audio / Export mix | Cmd+I / Cmd+E |
| New / Open / Save / Save As project | Cmd+N / Cmd+O / Cmd+S / Shift+Cmd+S |
| Zoom in / out / to fit | Cmd+= / Cmd+- / Cmd+0 (or Cmd + wheel, or pinch) |
| Choose the audio output | Cmd+, |

(Ctrl instead of Cmd on Windows and Linux.)

Projects (`.dawproject`) are small JSON files that refer to the audio files; keep the
audio next to the project (or in a subfolder) and the folder can be moved as a whole.
Projects saved by Phase 1 open too. Manual checks with reference files:
[docs/LISTENING_TESTS.md](docs/LISTENING_TESTS.md). How many tracks fit in an audio
callback: [docs/PERFORMANCE.md](docs/PERFORMANCE.md).

## Repository layout

| Path          | Contents                                                        |
|---------------|-----------------------------------------------------------------|
| `engine/`     | audio engine; never depends on the user interface               |
| `model/`      | project state, timeline time, grid, versioned project files     |
| `controller/` | maps the project onto the engine; shared by every UI            |
| `ui/desktop/` | mouse and keyboard interface                                    |
| `ui/mobile/`  | touch interface (placeholder)                                   |
| `app/`        | application entry point                                         |
| `tests/`      | unit tests; `tests/bench/` the mixing benchmark                 |
| `cmake/`      | build helpers: dependencies, warnings, architecture checks      |
| `tools/`      | development scripts (listening-test files)                      |
| `docs/`       | manual test procedures, performance measurements               |

Architecture, audio-thread rules and coding conventions are described in
[CLAUDE.md](CLAUDE.md).

## Licence

Copyright © 2026. All rights reserved. No open-source licence has been chosen for
this project's own code yet.

Third-party components keep their own licences: JUCE (AGPLv3 or commercial, see
the note at the top) and Catch2 (Boost Software License 1.0, tests only).
