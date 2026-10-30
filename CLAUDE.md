# CLAUDE.md

Working notes for Claude Code and for humans. Keep this file current: update it in
the same commit as any change to the architecture, the rules, the commands or the
phase status.

## Project

A cross-platform digital audio workstation (DAW). Final targets: Windows, macOS,
Linux, iOS/iPadOS and Android. A personal project today, built so it can become a
commercial product.

- C++20, JUCE 9.0.4 (CMake FetchContent, no Projucer), Catch2 3.16.1 for tests.
- CMake ≥ 3.25 with Ninja, driven by `CMakePresets.json`.
- Development happens locally on macOS (Apple Silicon), the primary test platform:
  build and run the app there. Windows and Linux are verified through GitHub Actions.

## Architecture

Modules with one-way dependencies:

| Module        | Purpose                                                           | May depend on                    |
|---------------|-------------------------------------------------------------------|----------------------------------|
| `engine/`     | audio engine: processing graph, transport, mixer, DSP, audio I/O  | non-GUI JUCE modules             |
| `model/`      | project state (tracks, clips, parameters), versioned project files | non-GUI JUCE modules            |
| `ui/desktop/` | mouse and keyboard interface                                      | engine, model, JUCE GUI          |
| `ui/mobile/`  | touch interface (placeholder only, not built yet)                 | engine, model, JUCE GUI          |
| `app/`        | entry point, connects the modules                                 | everything                       |
| `tests/`      | unit tests, mainly for engine and model                           | engine, model                    |

- `engine` never depends on any UI. `engine` and `model` don't depend on each other;
  `app` (later, a controller layer) maps the model onto the engine.
- Enforced by the CMake target graph and by the `architecture.*` CTest tests
  (`cmake/CheckArchitecture.cmake`): they fail if `engine/` or `model/` include GUI
  JUCE modules, `JuceHeader.h`, `daw/ui/...`, or each other.
- Every module has public headers in `include/daw/<module>/` and private code in
  `src/`. Namespaces: `daw::engine`, `daw::model`, `daw::ui::desktop`.
- JUCE is compiled once into the internal static library `daw::juce`
  (`cmake/JuceLibrary.cmake`), with JUCE's own flags. Include JUCE module headers
  directly (`<juce_audio_basics/juce_audio_basics.h>`), never `JuceHeader.h`.
- Anything both UIs will need (waveform peaks, meter values, transport state) lives
  in `engine` or `model`, never in a UI module.

## Audio thread rules (non-negotiable)

Inside the audio callback and everything it calls (`Engine::process` and below):

- no memory allocation or deallocation: no `new`/`delete`, no growing containers,
  no building `juce::String`s, no capturing `std::function`s;
- no locks or mutexes, no waiting on other threads;
- no file or network I/O;
- no logging (`DBG` included);
- no blocking calls or system calls that may block.

UI ↔ engine communication goes only through lock-free queues (single producer,
single consumer) or atomics. Memory the audio thread stops using is sent back on a
queue and freed on another thread. Mark audio-thread functions `noexcept` and say
"Audio thread" in their doc comment.

- Queues: `daw::engine::rt::SpscQueue<T, Capacity>` (fixed capacity, no allocation).
- Every test that exercises audio-thread code runs it through
  `daw::test::heapOperationsDuring(...)` (`tests/support/RealtimeCheck.h`) and
  requires 0. It counts operator new/delete everywhere; in the `linux-rtsan` CI job
  RealtimeSanitizer also aborts on malloc, locks, blocking system calls and I/O.
  Keep Catch2 assertions outside the checked lambda (Catch2 allocates).

## Verification

- Nobody can listen from the development environment: verify DSP with numeric
  tests (exact sample counts, bit-exact output, measured levels and frequencies
  with explicit tolerances).
- When a human listening test is really needed, write down exactly what to try and
  what should be heard.
- Zero warnings on all three platforms: project targets use `cmake/Warnings.cmake`
  (`-Werror` / `/WX`). JUCE and Catch2 headers are system headers and don't count.

## Conventions

- Types `PascalCase`; functions, variables and constants `camelCase`; macros
  `DAW_UPPER_CASE`.
- One main class per file, named after it. `#pragma once` in headers.
- Include order: own header, project headers, JUCE/Catch2, standard library.
- Formatting follows `.clang-format` (Allman braces, 4 spaces, 100 columns).
- No `using namespace` in headers. Use `[[nodiscard]]`, `const` and `noexcept`
  where they carry meaning.
- Code, comments, commit messages and UI text in English.
- Comments explain why, not what.

## Commands

```bash
cmake --preset debug            # configure; downloads JUCE and Catch2 the first time
cmake --build --preset debug    # build the app and the tests
ctest --preset debug            # run all tests
```

- Release build: same commands with the `release` preset.
- macOS universal binary (arm64 + x86_64): preset `macos-universal` (Release). Run the
  Intel slice of the tests with `arch -x86_64 build/macos-universal/tests/daw_tests`.
- RealtimeSanitizer build (Linux, Clang 20+ only; Apple Clang lacks it): preset
  `linux-rtsan`, run in CI. Pass `-DCMAKE_CXX_COMPILER=clang++-NN` for a versioned Clang.
- Run the app: `open build/debug/app/daw_app_artefacts/Debug/DAW.app`
- Run some tests only: `build/debug/tests/daw_tests "[engine]"`
- Build directories live in `build/<preset>/`; downloaded archives in `.cache/deps/`.

## CI

GitHub Actions (`.github/workflows/ci.yml`) runs on every pull request and on every
push to `main`: configure, build and test with the `debug` preset on Linux (GCC),
macOS (Apple Clang) and Windows (MSVC), plus the `release` preset on Linux, where
optimiser-only warnings show up, and the `linux-rtsan` preset with Clang 22 from
apt.llvm.org.

- Third-party actions are pinned by commit SHA, with the version in a comment.
- The repository is private and Actions minutes are limited (macOS and Windows
  runners cost more): push finished steps, not every edit. A newer push to the same
  branch cancels the run in progress.
- Linux packages needed by JUCE are listed in the workflow and in the README; keep
  the two lists in sync.
- When fixing a red CI job: at most 3 attempts on the same job. If it is still red
  after the third, stop pushing and explain the problem (what fails, suspected
  cause, what was tried) instead of trying again.

## Workflow

- Small steps, one commit per working step: it builds, all tests pass, zero warnings.
- Phase work happens on a branch (`phase-1`) with a pull request into `main`; CI
  must be green on Windows, macOS and Linux.
- Don't implement later phases (multitrack, MIDI, plugins, mobile) beyond keeping
  the architecture ready for them.

## Status

Phase 1 (in progress): a desktop app that picks the audio output device, loads a WAV
or AIFF file, plays it with play/stop/pause/loop, shows the waveform with a playhead
and a level meter. Plus unit tests and CI on Windows, macOS and Linux.

- [x] 0. Skeleton: CMake, presets, dependencies, empty modules, architecture tests
- [x] 1. CI on GitHub Actions (Windows, macOS, Linux)
- [x] 2. Real-time primitives: lock-free queue, allocation guard for tests
- [ ] 3. WAV/AIFF loading and sample-rate conversion
- [ ] 4. Transport: play, pause, stop, loop, seek, de-click fades
- [ ] 5. Level meter and waveform peaks
- [ ] 6. Project model and versioned project files
- [ ] 7. Desktop UI and audio device output
