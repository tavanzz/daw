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

Phase 1: play a single WAV or AIFF file with transport controls, waveform display
and level meter. See [CLAUDE.md](CLAUDE.md#status) for the steps.

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

Load a WAV or AIFF file by dragging it onto the window or with File > Open Audio
File. The file is converted to the output device's sample rate when needed.

| Action | Shortcut |
|--------|----------|
| Play / pause | Space |
| Stop and return to the start | Return |
| Loop the whole file | L |
| Move the playhead | click or drag on the waveform |
| Open audio file / project | Cmd+O / Shift+Cmd+O (Ctrl on Windows and Linux) |
| Save project / save as | Cmd+S / Shift+Cmd+S |
| Choose the audio output | Cmd+, |

Projects (`.dawproject`) are small JSON files that refer to the audio file; keep
the audio next to the project (or in a subfolder) and the folder can be moved as a
whole. Manual checks with reference files: [docs/LISTENING_TESTS.md](docs/LISTENING_TESTS.md).

## Repository layout

| Path          | Contents                                                        |
|---------------|-----------------------------------------------------------------|
| `engine/`     | audio engine; never depends on the user interface               |
| `model/`      | project state and versioned project files                       |
| `ui/desktop/` | mouse and keyboard interface                                    |
| `ui/mobile/`  | touch interface (placeholder)                                   |
| `app/`        | application entry point                                         |
| `tests/`      | unit tests                                                      |
| `cmake/`      | build helpers: dependencies, warnings, architecture checks      |
| `tools/`      | development scripts (listening-test files)                      |
| `docs/`       | manual test procedures                                          |

Architecture, audio-thread rules and coding conventions are described in
[CLAUDE.md](CLAUDE.md).

## Licence

Copyright © 2026. All rights reserved. No open-source licence has been chosen for
this project's own code yet.

Third-party components keep their own licences: JUCE (AGPLv3 or commercial, see
the note at the top) and Catch2 (Boost Software License 1.0, tests only).
