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
| `controller/` | maps the model onto the engine: session, undo, loading, export, timeline view math | engine, model, non-GUI JUCE |
| `ui/desktop/` | mouse and keyboard interface                                      | controller, engine, model, JUCE GUI |
| `ui/mobile/`  | touch interface (placeholder only, not built yet)                 | controller, engine, model, JUCE GUI |
| `app/`        | entry point, connects the modules                                 | everything                       |
| `tests/`      | unit tests for engine, model and controller                       | controller, engine, model        |

- `engine` never depends on any UI. `engine` and `model` don't depend on each other
  nor on `controller`; `controller` maps the model onto the engine.
- Enforced by the CMake target graph and by the `architecture.*` CTest tests
  (`cmake/CheckArchitecture.cmake`): they fail if `engine/`, `model/` or
  `controller/` include GUI JUCE modules, `JuceHeader.h` or `daw/ui/...`, or if
  `engine/` and `model/` include each other or `controller/`.
- Every module has public headers in `include/daw/<module>/` and private code in
  `src/`. Namespaces: `daw::engine`, `daw::model`, `daw::controller`, `daw::ui::desktop`.
- JUCE is compiled once into the internal static library `daw::juce`
  (`cmake/JuceLibrary.cmake`), with JUCE's own flags. Include JUCE module headers
  directly (`<juce_audio_basics/juce_audio_basics.h>`), never `JuceHeader.h`.
- Anything both UIs will need (waveform peaks, meter values, transport state) lives
  in `engine` or `model`, never in a UI module.
- Audio files: `engine::io::loadAudioFile()` decodes a whole WAV/AIFF into memory
  and converts it to the device sample rate with `engine::dsp::resample()` (offline
  windowed sinc), off the audio thread. `engine::io::SourcePool` loads each file
  once however many clips use it, on worker threads; finished loads are applied
  when the owner calls `collectFinishedLoads()`. Clips read audio only through
  `engine::AudioSource::read()` (real-time safe; `MemoryAudioSource` today), so a
  streaming source can replace the in-memory one later without touching the mixer.
- Engine threading: UI commands go through an `SpscQueue` and apply at the next
  block. A new `Arrangement` (with its `MixState`) arrives through one atomic pointer
  exchange (`Engine::setArrangement()`); if it changes what the tracks play
  (`layoutHash`) while the transport runs, old and new are crossfaded over 5 ms, one
  change at a time; while stopped it simply applies. Replaced snapshots go back on a
  second queue and `Engine::collectGarbage()` frees them on the message thread. State
  for the UI is published with relaxed atomics after every block. Blocks larger than
  announced are rendered in pieces; mono devices get the left channel.
- Transport (Phase 2): renders the timeline through `TimelineRenderer` (the engine,
  i.e. the mixer). Its length is the end of the last clip; stop and the end of the
  timeline go back to the return position (where playback last started from stopped,
  or the last seek); stop while stopped goes to 0.
- Mixing (Phase 2): the message thread builds an immutable `engine::Arrangement`
  (tracks with target gains and their audible `ClipSegment`s, sorted and disjoint;
  master gain; length; loop region; the sources it reads, kept alive by
  `shared_ptr`). `flattenClips()` resolves overlaps (the clip on top hides what it
  covers) and puts 5 ms fades on cut edges only. `engine::Mixer::render()` is the
  one mixing function, for playback and export: per track it reads the segments,
  applies fades and the ramped L/R gains (`dsp::GainRamp`, 20 ms, each gain computed
  from the ramp start), meters the track, and adds it to the mix (plain adds, no
  fused multiply-add); then the master gain. The result is bit-identical however the
  timeline is cut into blocks (tested). `MixState` holds the ramps of one
  arrangement; `continueFrom()` carries them over by track id. Pan law:
  `dsp::panGains()`, constant power compensated to 0 dB in the centre.
- Export: `engine::OfflineRenderer` drives the same `Mixer` faster than real time;
  `engine::io::bounceToWav()` writes a stereo WAV (16/24-bit integer with
  `quantise()` = round(x·2^(bits−1)) clipped, optional seeded TPDF dither, or 32-bit
  float) to a temporary file that replaces the target only when complete; progress
  callback, cancellable, reports the peak. Tested: the export equals real-time
  playback bit for bit after the transport's fade-in, for any device block size.
  An export at another sample rate renders an arrangement built at that rate.
- Meters and waveforms: the engine measures its output with `dsp::PeakLevelMeter`
  (one reader takes the peak since its last read); `dsp::MeterBallistics` turns
  readings into what a meter shows (time-based, so refresh-rate independent);
  `dsp::WaveformPeaks` keeps min/max per 256-sample bucket. All three are UI-agnostic.
- Project model: `model::Project` / `Track` / `Clip` are typed handles onto a
  `juce::ValueTree`. `model::ProjectFile` maps it explicitly to JSON (`.dawproject`,
  LF, UTF-8) with `"formatVersion"`.
  - Tracks hold mixer settings (volume as linear gain 0 to +6 dB, pan -1 to 1, mute,
    solo) and clips in stacking order (last = on top). `Project::isTrackAudible()`
    is the one place with the mute/solo rule. A clip is a file, a timeline position,
    an offset into the file and a length (unknown until the file is read, for
    migrated projects: `Clip::resolveLength()`).
  - Tracks and clips have ids, unique across both, computed as the largest id in
    the project + 1 (no hidden counter; see `model/src/ObjectIds.h`).
  - Undo: setters that take a `juce::UndoManager*` are undoable; the others (loop
    on/off, snap, grid, length resolution) deliberately are not. The caller groups
    changes into transactions.
- Timeline time (Phase 2): positions and lengths in the model are `model::Ticks`,
  integers at 282,240,000 per second, a number every common sample rate divides, so
  sample positions at any of them are exact and the project has no sample rate of its
  own. `model::TempoMap` (one tempo in quarter notes per minute and one metre per
  project) converts to bars and beats, rounding each bar and beat start to a tick
  once; `model::Grid` gives grid lines (restarting at every bar line) and snapping.
  The engine never sees ticks: they become samples when the arrangement is built.

- Controller (Phase 2): `controller::Session` owns the open project, its
  `juce::UndoManager` (gestures group a drag into one step), a `SourcePool` at the
  device rate, and the per-track meters; it listens to the project's ValueTree and,
  at the next `update()` (UI timer), rebuilds the arrangement with
  `buildArrangement()` and hands it to the engine. Lengths learnt from loaded files
  fill in unknown clip lengths without counting as edits. `TimelineViewport` holds
  zoom/scroll and pixel↔tick conversion, `timeRulerMarks()` / `barRulerMarks()`
  the rulers; `ClipEditing.h` the move/trim rules with `Snapper`. `ExportJob` copies
  the project on the message thread and exports on another, loading files at the
  export rate when it differs from the device's.
- Audio I/O: `engine::io::AudioDeviceHost` owns JUCE's `AudioDeviceManager` and
  forwards the device callback to `Engine::process(inputs, output)` (stereo output;
  extra outputs cleared). Inputs reach the engine as `AudioInputs`, indexed by the
  device's channel number (JUCE packs the active ones; the host unpacks them), up to
  64; `dsp::InputMeters` measures every input channel before any processing.
- Desktop UI (`ui/desktop`): views over a `controller::Session`; every edit goes
  through it. `MainComponent` (transport bar, `ArrangeView`, master meter) refreshes
  at 60 Hz and handles the commands (menus and shortcuts, JUCE's
  ApplicationCommandManager). `ArrangeView` = `TimelineRuler` + `TrackHeaderList` +
  `TrackLanes` (drawing and clip mouse editing), sharing an `ArrangeState` (session,
  `TimelineViewport`, selection). `MixerView` has a strip per track and the master
  (faders in dB, −∞ to +6; a drag is one undo gesture). `ExportDialog` runs an
  `ExportJob` in a `ThreadWithProgressWindow`. New/Open/Quit offer to save unsaved
  changes first (`MainComponent::askToSaveChanges()`). Views redraw when
  `Session::getRevision()` changes. Peak meters have one reader each: the output
  peaks are taken once per tick and fed to both master meters.
- `app/AppController` owns the engine, the device host, the session and the window;
  drives `Session::update()` at 60 Hz, passes device sample-rate changes to the
  session, keeps the window title, and stores the chosen device in the user settings
  (`~/Library/Application Support/DAW/DAW.settings` on macOS).
- macOS asks the user before an app reads files in Documents, Desktop or Downloads;
  until they answer, file loads block in `open()`. Test from a session with audio
  copied outside those folders.

## Project file format rules

- Any change to what is written to disk bumps `model::currentFormatVersion`
  (`model/include/daw/model/FormatVersion.h`) and appends a migration to the chain
  in `model/src/ProjectFile.cpp` that upgrades the previous version's JSON.
- Update the golden test ("The file format is exactly the documented JSON") in the
  same commit, and add a test that loads a file of the previous version.
- Never reuse or rename a JSON field without a migration; unknown fields are ignored
  on load, files from newer versions are refused with a message.

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
- `daw::test::SimulatedDevice` (`tests/support/SimulatedDevice.h`) plays the sound
  card: it drives `AudioDeviceHost`'s callback with known input, variable block
  sizes, switched-off channels, a loopback cable with a known latency and a stream
  counter that can start near 2^32.

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
- Test case names in plain ASCII: CTest passes each name on the command line, and on
  Windows other characters arrive mangled, so the test is not found and fails.
- Commits are authored as `tavanzz` with the GitHub noreply address
  (`94704314+tavanzz@users.noreply.github.com`, set in the repository's git config):
  the repository is public, so no personal email in new commits.
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
- Listening tests: `docs/LISTENING_TESTS.md`; reference files and the Phase 2 demo
  project from `python3 tools/make_listening_test_files.py` (into
  `build/listening-tests/`).
- Mixing benchmark (Release only, not in CTest): `build/release/tests/daw_bench`;
  results in `docs/PERFORMANCE.md`.
- Screenshots for checking the UI from a session: capture only the app's window
  (`screencapture -l <window number>`), never the whole screen.

## CI

GitHub Actions (`.github/workflows/ci.yml`) runs on every pull request and on every
push to `main`: configure, build and test with the `debug` preset on Linux (GCC),
macOS (Apple Clang) and Windows (MSVC), plus the `release` preset on Linux, where
optimiser-only warnings show up, and the `linux-rtsan` preset with Clang 22 from
apt.llvm.org.

- Third-party actions are pinned by commit SHA, with the version in a comment.
- The repository is public (since Phase 2), so standard runners cost nothing; still
  push finished steps, not every edit, to keep runs readable. A newer push to the
  same branch cancels the run in progress.
- Linux packages needed by JUCE are listed in the workflow and in the README; keep
  the two lists in sync.
- When fixing a red CI job: at most 3 attempts on the same job. If it is still red
  after the third, stop pushing and explain the problem (what fails, suspected
  cause, what was tried) instead of trying again.

## Workflow

- Small steps, one commit per working step: it builds, all tests pass, zero warnings.
- Phase work happens on a branch (`phase-N`) with a draft pull request into `main`;
  CI must be green on Windows, macOS and Linux.
- Don't implement later phases (multiple takes and comping, MIDI, effects, plugins,
  automation, mobile) beyond keeping the architecture ready for them.

## Status

Phase 1 (done, released as v0.1.0): a desktop app that picks the audio output device, loads a WAV
or AIFF file, plays it with play/stop/pause/loop, shows the waveform with a playhead
and a level meter. Plus unit tests and CI on Windows, macOS and Linux.

- [x] 0. Skeleton: CMake, presets, dependencies, empty modules, architecture tests
- [x] 1. CI on GitHub Actions (Windows, macOS, Linux)
- [x] 2. Real-time primitives: lock-free queue, allocation guard for tests
- [x] 3. WAV/AIFF loading and sample-rate conversion
- [x] 4. Transport: play, pause, stop, loop, seek, de-click fades
- [x] 5. Level meter and waveform peaks
- [x] 6. Project model and versioned project files
- [x] 7. Desktop UI and audio device output
- [x] Listening tests (`docs/LISTENING_TESTS.md`): all 14 passed on macOS, 2026-10-30

Phase 2 (done, released as v0.2.0): multitrack and mixer. Several audio tracks
with clips on a timeline (add, move, trim start/end, delete; one load per file), a
ruler in time and bars (fixed tempo and metre per project), grid with snap, zoom and
scroll, playhead, loop region; a mixer with volume, pan, mute, solo and meters per
track plus a master bus; WAV export (bounce) through the same code as real-time
playback; undo/redo for every project change; project format version 2 with a
migration from version 1. Out of scope: recording, MIDI, effects, plugins,
automation, mobile UI.

- [x] 0. Plan (this list) and draft pull request
- [x] 1. Timeline time in the model: ticks, tempo and metre, conversions, grid, snap
- [x] 2. Multitrack model: track mixer settings, clip position/offset/length, ids,
      undoable editing operations
- [x] 3. Project format version 2 and the migration from version 1
- [x] 4. Audio sources: `AudioSource` interface, `SourcePool` loading each file once
- [x] 5. Mix renderer: immutable `Arrangement` snapshot, sample-exact clips,
      volume/pan/mute/solo with smoothing, track and master meters
- [x] 6. Engine on the timeline: atomic snapshot exchange, crossfade on structural
      changes, transport over the timeline with a loop region
- [x] 7. Offline bounce to WAV (16/24-bit, 32-bit float; sample rate; dither)
- [x] 8. `controller` module: session, snapshot building, timeline viewport and ruler,
      clip drags with snap
- [x] 9. Benchmark: tracks per audio callback budget (Release)
- [x] 10. Desktop UI: timeline, track headers, clip editing, zoom/scroll, loop, snap
- [x] 11. Desktop UI: mixer, export dialog, undo/redo, new project, unsaved changes
- [x] 12. Listening-test files and `docs/LISTENING_TESTS.md` for Phase 2
- [x] Listening tests (`docs/LISTENING_TESTS.md`): all 31 passed on macOS, 2026-10-30

Phase 2 decisions (agreed before starting):

- Overlapping clips on a track: the topmost (most recently added or moved) covers
  the ones below without cutting them; moving it away reveals them again.
- Pan law: constant power (sin/cos), compensated so the centre is 0 dB on both
  channels; hard left = +3 dB left, silence right. A neutral track plays exactly
  like Phase 1.
- Changing the tempo keeps clips at their time position; only the grid moves.
- Stop returns to where playback started; Stop while stopped goes to 0. Without
  loop, playback stops at the end of the last clip and returns to where it started.
- Clip edges: a 5 ms fade only on cut edges (trimmed, or covered by another clip);
  an untrimmed file plays exactly as it is.
- Undo covers clips, tracks, mixer, tempo/metre and the loop region; loop on/off,
  snap, grid, zoom and the playhead are not undoable (snap and grid are still
  saved in the project).
- Export: 44.1/48/88.2/96 kHz (device rate preselected); 16/24-bit integer or 32-bit
  float (24 preselected); TPDF dither at 16 bit only; range = whole project or the
  loop region. The export starts at full level on its first sample (no transport
  fade-in); apart from that fade it equals real-time playback sample for sample.
- Shortcuts: Cmd+O open project, Cmd+I import audio, Cmd+N new project, Cmd+T new
  track, X mixer, N snap, Cmd while dragging bypasses snap, Backspace or Delete
  deletes the selected clips. Several dropped files: one new track each, all at the
  same position.

Phase 3 (in progress, branch `phase-3`): audio recording. Input device and channels;
each track records mono or stereo from chosen inputs; arming and recording on several
tracks at once; input monitoring per track (off / auto / on) and input meters;
latency compensation (driver-reported plus a manual offset, and a loopback
measurement tool) so recordings line up to the sample with what was heard; metronome
and count-in; punch in/out on the loop region; every recording becomes clips in one
undo step, files in the project's `Audio/` folder with relative paths; recordings
can be discarded; recovery of interrupted recordings after a crash; disk-space
checks; macOS microphone permission (ready for iOS); playback and recording stream
from disk. Out of scope: multiple takes and comping, MIDI, effects, plugins,
automation, mobile UI.

- [x] 0. Plan (this list) and draft pull request
- [ ] 1. Audio input in the engine: input channels from the device, channel mapping,
      input meters; simulated device for tests
- [ ] 2. Project format version 3: track input, monitoring and arm; metronome,
      count-in, punch and recording format; migration from version 2
- [ ] 3. Input monitoring: per-track input/timeline switch with ramps, off/auto/on,
      transport fade on the timeline part only, mixer running while stopped
- [ ] 4. Metronome and count-in, sample-exact on the tempo map's beats
- [ ] 5. Recording pipeline: lock-free audio FIFO, WAV writer thread, overrun and
      disk-space reporting
- [ ] 6. `Recorder` in the engine: latency-compensated capture, several tracks,
      punch in/out
- [ ] 7. Recording in the controller: files, clips in one undo step, discard,
      errors, disk-space checks
- [ ] 8. Crash recovery: recording journal, WAV repair, recovery on open
- [ ] 9. Latency measurement with a loopback cable
- [ ] 10. Disk streaming in the engine: block cache, read-ahead thread, sample-rate
      conversion while streaming, dropout reporting
- [ ] 11. Streaming everywhere: source pool, transport waiting for data, export
- [ ] 12. Microphone permission and app wiring (input device, latency offset)
- [ ] 13. Desktop UI: arm, monitoring, inputs, input meters, record/metronome/
      count-in/punch controls, live recording clips
- [ ] 14. Desktop UI: audio and recording settings, latency measurement, recovery,
      warnings
- [ ] 15. Benchmark: tracks streamed and recorded at once (Release)
- [ ] 16. Listening tests and files for Phase 3 (`docs/LISTENING_TESTS.md`)

Phase 3 decisions (agreed before starting):

- Monitoring: a track plays either its timeline or its input, never both; the switch
  is a 5 ms ramp, and the input goes through the track's volume, pan, mute, solo and
  meter. Auto: the input is heard while the track is armed and the transport is
  stopped, during the count-in, and while recording inside the recorded range (with
  punch: between punch in and punch out only); otherwise the timeline. On: always the
  input. Off: never.
- New tracks take input 1 (mono). Stereo inputs are channel pairs 1+2, 3+4, ...
- Recording in a project that was never saved first asks to save it.
- Recording is linear: loop playback is suspended while recording, seeks are ignored,
  the end of the project does not stop the transport.
- Punch: when on, the loop region is the recorded range. The file keeps everything
  captured (count-in, pre-roll and post-roll) as handles; the clip is trimmed to the
  punch range, so its edges get the usual 5 ms fades.
- If the disk cannot keep up (FIFO overrun), recording stops; the audio up to the
  overrun is kept and an error says where it happened. Disk space: at least 1 minute
  of recording time to start, a warning below 10 minutes, a clean stop at 10 seconds.
- Files: `Audio/<track name> <n>.wav` next to the project, never overwritten; WAV at
  the device rate, 24-bit by default (16-bit and 32-bit float available). Save As to
  another folder copies the project's recordings into the new `Audio/` folder.
- Undo removes recorded clips and keeps their files (redo needs them). Discarding a
  recording in progress (Cmd+., after the confirmation "Discard the recording and
  delete its files?") deletes its files and adds no clip. Escape never discards a
  recording; at most it closes dialogs.
- Arm state is saved in the project and is not undoable (like loop on/off); track
  input and monitoring mode are undoable.
- Metronome: when on, it clicks during playback and recording, after the master
  fader, never in exports or on the master meter. Count-in: 1 bar (or 2) of clicks
  only before recording starts, the timeline silent.
- Latency offset: user settings, per input/output device pair and sample rate.
- Microphone permission is asked for the first time an input is needed (arming,
  monitoring, the input settings, a project with armed tracks), not at launch.
- Disk streaming: a fixed 512 MB block cache.
- Shortcuts: R record, K metronome, Shift+K count-in, P punch, Cmd+. discard the
  recording in progress (with confirmation).
- The one-hour recording test records 48 kHz mono 24-bit to disk and reads it back
  (8 kHz if it takes more than about a minute in the Debug jobs).
