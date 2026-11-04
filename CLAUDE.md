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
| `appearance/` | theme values (colours, fonts, shapes, meter styles, effects) and animation settings, for every UI | non-GUI JUCE modules |
| `ui/desktop/` | mouse and keyboard interface                                      | controller, engine, model, JUCE GUI |
| `ui/mobile/`  | touch interface (placeholder only, not built yet)                 | controller, engine, model, JUCE GUI |
| `app/`        | entry point, connects the modules                                 | everything                       |
| `tests/`      | unit tests for engine, model, controller and appearance; UI tests for ui/desktop | everything but app |

- `engine` never depends on any UI. `engine` and `model` don't depend on each other
  nor on `controller`; `controller` maps the model onto the engine. `appearance`
  depends on no other module, and only the UIs (and `app`) use it.
- Enforced by the CMake target graph and by the `architecture.*` CTest tests
  (`cmake/CheckArchitecture.cmake`): they fail if `engine/`, `model/`, `controller/`
  or `appearance/` include GUI JUCE modules, `JuceHeader.h` or `daw/ui/...`, if
  `engine/` and `model/` include each other or `controller/`, or if `appearance/`
  includes another module or is included by `engine/`, `model/` or `controller/`.
- Every module has public headers in `include/daw/<module>/` and private code in
  `src/`. Namespaces: `daw::engine`, `daw::model`, `daw::controller`,
  `daw::appearance`, `daw::ui::desktop`.
- JUCE is compiled once into the internal static library `daw::juce`
  (`cmake/JuceLibrary.cmake`), with JUCE's own flags. Include JUCE module headers
  directly (`<juce_audio_basics/juce_audio_basics.h>`), never `JuceHeader.h`.
- Anything both UIs will need (waveform peaks, meter values, transport state) lives
  in `engine` or `model`, never in a UI module.
- Audio files: `engine::io::SourcePool` opens each file once however many clips use
  it, on worker threads, as a streamed source (`io::DiskStreamer`, below) and reads it
  through once for its waveform; finished loads are applied when the owner calls
  `collectFinishedLoads()`. Clips read audio only through `engine::AudioSource::read()`
  (real-time safe; `StreamingAudioSource` in the app, `MemoryAudioSource` in tests)
  and `isReady()`. No file is held whole in memory, so there is no length limit.
  `engine::io::loadAudioFile()` still decodes a whole file (tests compare against it),
  converting with `engine::dsp::resample()` (offline windowed sinc; `dsp::Resampling`
  also converts a block at a time, identically).
- Engine threading: UI commands go through an `SpscQueue` and apply at the next
  block. A new `Arrangement` (with its `MixState`) arrives through one atomic pointer
  exchange (`Engine::setArrangement()`); if it changes what a track plays
  (`layoutHash`, per track too) while the transport runs, that track's clips
  crossfade from old to new over 5 ms before its effects (which both arrangements
  share and which run once), one change at a time; tracks that are gone fade out
  over the same time whenever they may be heard; otherwise it simply applies. Replaced snapshots go back on a
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
- Monitoring (Phase 3): a track plays either its clips or its input
  (`MixState::TrackGains::timeline` / `input`, crossfaded over 5 ms when it
  switches); the input then goes through the track's gains and meter like the clips.
  `MixContext` tells the mixer about the block: the device inputs (null offline, so
  exports never hear an input), whether the timeline runs, the transport's fade and
  whether automatic monitoring hears inputs now. The transport hands its de-click
  fade to the renderer as a `TimelineFade`, applied to each track's clips only, and
  calls `renderStopped()` while stopped, so monitored inputs are never faded by
  play/stop and are heard while stopped.
- Metronome and count-in (Phase 3): the controller gives each arrangement an
  `engine::BeatGrid` (`controller::TempoBeatGrid`: the tempo map's beat starts
  converted to samples, so the engine never repeats the model's rounding).
  `dsp::Metronome` starts a synthesised click (accented on downbeats) on the exact
  sample of each beat while playing with the metronome on; clicks are added after
  the master meter and never exported. `Engine::playWithCountIn()` clicks for the
  count-in length from stopped (the timeline silent, inputs monitored as when
  recording), then starts the transport on the next sample with its usual fade-in.
- Recording pipeline (Phase 3): the audio thread only copies samples into a
  `rt::AudioFifo` per recorded file (lock-free, one producer and one consumer, a
  whole block or nothing, 10 s of audio); `io::RecordingWriter` empties the FIFOs on
  its own thread into WAV files at the device rate (16/24-bit rounded and clipped,
  no dither, or 32-bit float), rewrites their headers every second (readable after
  a crash), builds their waveforms as they grow (`WaveformPeaks::Builder`) and
  checks the free disk space. A full FIFO refuses the block (the producer knows
  which samples were lost); a refused write or less than 10 s of disk left is
  reported as a `Problem`. Tests replace the disk with a `RecordingSink`.
- Recording in the engine (Phase 3): `Engine::startRecording(RecordingSetup)` (the
  targets' input channels and FIFOs, start position, count-in, latency, punch
  range) goes through the command queue, so it keeps its order with stop and play;
  it starts as soon as the transport is silent and the audio at its start is loaded,
  and a stop before that cancels it. `engine::Recorder` works in
  stream indices (samples since the engine started): the recording runs from s0
  (count-in or playback start, timeline `firstPosition`) to s1 (stop); what was
  played at s comes back at s + latency, so it captures inputs [s0 + latency,
  s1 + latency) and sample k belongs at `firstPosition + k`, exactly. While
  recording the transport is linear (no loop, no end, seeks ignored, pause stops);
  automatic monitoring follows the punch range to the sample. A FIFO without room
  ends the capture for all targets (overrun); a device restart interrupts it.
  `RecordingStatus` reports it all.
- Export: `engine::OfflineRenderer` drives the same `Mixer` faster than real time;
  `engine::io::bounceToWav()` writes a stereo WAV (16/24-bit integer with
  `quantise()` = round(x·2^(bits−1)) clipped, optional seeded TPDF dither, or 32-bit
  float) to a temporary file that replaces the target only when complete; progress
  callback, cancellable, reports the peak. Tested: the export equals real-time
  playback bit for bit after the transport's fade-in, for any device block size.
  An export at another sample rate renders an arrangement built at that rate.
- Effects (Phase 4): `engine::fx::Effect` is the one interface for built-in effects
  and, later, plugin adapters: a description (type, name, `ParameterInfo`s with
  stable ids, ranges, linear or logarithmic scales, choices), `prepare()` on the
  message thread, `reset()` and `process()` noexcept on the audio thread, latency,
  tail length and opaque extra state. The host owns the parameter values: each block
  an effect gets `ParameterValues`, per parameter one value or a value per sample.
  `fx::EffectInstance` is the host around one effect: 20 ms ramps in the normalised
  domain (choices jump), automation values taken exactly, a 10 ms dry/wet crossfade
  when added, removed or bypassed (a bypassed effect is not processed and restarts
  clean), the dry signal delayed by the latency (`dsp::DelayLine`), and a guard that
  resets and silences an effect producing NaN or infinity (`hasFailed()`). The audio
  and export threads run with denormals flushed to zero (`juce::ScopedNoDenormals`;
  Accelerate's vector operations on macOS ignore the mode, the effects' own code
  does not).
- Insert chains (Phase 4): an `EffectChain` of `ChainSlot`s (a shared
  `EffectInstance`, the normalised parameter values, enabled, revival) on every
  track, before its fader, and on the master, after the master fader. The
  instances persist across arrangements, never twice in one. Effects run whenever
  the mixer renders, also while stopped, so tails ring on; a silent track without
  effects is still skipped. With empty chains, bypassed effects or exact unity
  gains the mix equals Phase 3 bit for bit: tests compare it with a frozen copy of
  the v0.3.0 mixer (`tests/support/Phase3Mixer`).
- Buses and sends (Phase 4): `ArrangementBus` (id, gains, inserts, meter) sums the
  sends it gets (`ArrangementSend`: id, bus id and index, gain, pre- or post-fader)
  through its effects and its fader into the mix, before the master gain. Pre-fader
  sends tap after the track's inserts, post-fader ones after its volume and pan;
  send and bus gains ramp like faders and carry over by id (`MixState`, which also
  holds the buses' summing buffer; longer stretches are mixed in pieces). A track
  with only a pre-fader send is rendered even with its fader down.
- Latency compensation (Phase 4): `MixState::startingAt()` works out each track's
  insert latency L(t), what each bus aligns its sends to (its slowest sender,
  A(b)), and where all tracks and buses meet (M); sends are delayed by A(b) - L(t),
  tracks by M - L(t), buses by M - A(b) - L(b), through `dsp::DelayLine`s carried
  over by id (`adopt()` swaps their memory, no allocation). The output lags the
  timeline read by `latency` = M + the master's insert latency. Bypassed effects
  keep their latency. A `TimelineHistory` records what the engine read at each
  stream index, so what is heard is known: the playhead shows it, the metronome and
  the count-in click on it, recordings add the latency to their own, and
  `OfflineRenderer` reads that far ahead and drops what comes out first (export
  sample k is timeline position start + k). A change that moves audio in time
  (`MixState::movesAudioFrom()`: the total latency, or a latency or delay of a path
  both arrangements have) while anything may be heard dips the output: 5 ms fade
  out, silence while the new delays fill, 5 ms fade in. Tracks with delays run on
  while stopped so the delayed audio still comes out.
- Automation in the engine (Phase 4): `engine::AutomationCurve` (points in
  samples with a normalised value and a bend; straight or bent segments, steps,
  flat before the first and after the last point) is a pure function of the
  position. Tracks and buses carry a `GainAutomation` (volume and pan curves),
  sends a level curve, the arrangement a master curve, chain slots a curve per
  parameter; volumes and send levels are fader positions (`dsp::faderGain()`, even
  in dB from -60 to +6). The mixer reads each curve at the timeline position of the
  audio where it applies (`MixContext::history`): a fader after effects of latency
  L reads what was read L samples ago, through loops and seeks. Automation that
  starts is reached over 20 ms from the fixed gains (or the parameter's ramp),
  which keep moving underneath; when it stops they ramp on from its last value.
- Built-in effects (Phase 4), in `engine/fx/`: `Equaliser` (eight bands: high-pass,
  low shelf, four bells, high shelf, low-pass, and an output gain; trapezoidal SVF
  sections in double, the Audio EQ Cookbook's bells and shelves and Butterworth
  passes of 6 to 48 dB/oct; `magnitude()` is the exact response for the UI's curve;
  band switches and slopes crossfade over 10 ms; silence in with filters at rest
  skips the work; filter states below 1e-20 are set to zero sample by sample, or
  flushing denormals leaves them flipping sign for ever). `Compressor` (Giannoulis et al.: linked peak level in dB, static
  curve with threshold, ratio and a quadratic knee, smoothed in dB with attack and
  release time constants, makeup, mix; `staticCurve()` for the UI). `Limiter`
  (1.5 ms lookahead = its latency; the lowest required gain over the lookahead,
  released exponentially, averaged over the lookahead, so every output sample is
  under the ceiling by construction; a final clip only catches rounding). Both
  report their gain reduction to the UI through a `GainReductionMeter`. `Delay`
  (whole-sample delay up to 4 s, in ms or a synced note at the block's tempo;
  feedback at most 95 % through one-pole low/high cuts that never amplify, off at
  the ends of their ranges; ping-pong; a new time crossfades between two taps over
  50 ms, one change at a time). `Reverb` (pre-delay, four allpass diffusers, a
  16-line feedback delay network with a Hadamard matrix and per-line gains
  10^(-3 d / (T60 rate)), so it loses 60 dB in the decay time; one-pole damping
  with unity gain at DC; half the lines slowly modulated; fractional lengths read
  through allpass interpolation, which loses nothing; size glides the lengths;
  width, low/high cut, mix). Its tests measure T30 on Schroeder's energy decay
  curve. Tests run effects through `tests/support/EffectRunner.h` and
  compare measured responses with formulas written independently of the code.
- MIDI events and instruments (Phase 6): `engine::midi::Event` (sample offset,
  note-on/off with note id, channel, key, velocity 0..1; controller, pitch bend,
  channel pressure normalised; exact to MIDI 1.0's 7 and 14 bits), sorted per sample
  as note-offs, controllers, note-ons (`comesBefore()`). `midi::EventBuffer` (fixed
  capacity, insertion sort, only note-offs may use its second half so they always
  fit) and `midi::HeldNotes` (notes started and not ended, matched by note id or the
  oldest on the key). An instrument is an `fx::Effect` whose description says
  `isInstrument`; it gets `ProcessBlock::events`. `EffectInstance` hands an
  instrument the events of each piece at their samples, passes a note-off only for
  a note it gave it, refuses note-ons while 256 are held, ends every held note at
  once when the instrument is switched off (bypass, removal) and forgets them when
  it fails.
- MIDI tracks in the engine (Phase 6): an `ArrangementTrack` with `playsNotes` has
  `ArrangementNote`s (start and end samples, key, channel, velocity, an identity that
  survives rebuilds), sorted by start, and `ControllerLane`s (steps; rest values
  from `midi::restValue()`: pitch bend centred, CC7 and CC11 full, the rest 0); its
  instrument is the first slot of its inserts, so effects, sends, automation and
  delay compensation work as for audio. `NotePlayback` (one per MIDI track in
  `MixState`, carried over by `adopt()`) turns each stretch into events: a stretch
  that does not continue the last ends every note and chases the controllers (after
  first returning moved ones to rest, so a sustain pedal releases the ended notes);
  notes are not restarted; a key struck again is ended first; `stop()` ends notes and
  rests controllers (stop, pause, any fade-out, a vanishing track). A new arrangement
  keeps notes whose identity and start are still there, with their new end, and ends
  the others at once. The transport's fade does not apply to instruments. The mixer
  hands the sorted events to slot 0.
- Live MIDI in the engine (Phase 6): `AudioDeviceHost` (a `juce::MidiInputCallback`)
  hands each device message, with JUCE's time stamp and a device index
  (`getMidiDeviceIndex()`, by identifier hash, lock-free), to `Engine::pushMidi()`: an
  SPSC queue (JUCE serialises its MIDI callbacks). The host stamps every audio block
  with `midi::hostSeconds()` (`AudioInputs::time`); the engine places a message at the
  block start + (its time - the block's) + one block, so live MIDI keeps its rhythm a
  block late, without jitter. `Engine::startPreview()`/`stopPreview()` queue the piano
  roll's notes for a track. The block's `midi::LiveEvent`s reach the mixer in
  `MixContext::live`; a MIDI track hears devices through `midi::InputFilter` (device,
  channel) while monitoring is on, or automatic and armed, and its previews always;
  `midi::LiveNotes` gives live notes ids of their own and ends them (and rests the
  controllers they moved) when the track stops listening, goes away, or blocks were
  skipped (`MixContext::liveRestarts`). Tests play a keyboard with
  `SimulatedDevice::playMidi()` on a simulated clock (`AudioDeviceHost::setClock()`).
- MIDI recording in the engine (Phase 6): `RecordingSetup::midiTargets` (track,
  `midi::InputFilter`), a `MidiRecordingQueue` (SPSC to the message thread, on the
  heap) and `midiLatency` (the device's output latency). As the engine drains live
  MIDI it hands each event, with the stream index it happened at, to
  `Recorder::captureMidi()`: what was heard then left the engine `midiLatency`
  earlier, so the event belongs at firstPosition + (s - midiLatency - s0), s0 being
  where the recording is heard to start (effect latency included), count-in
  included, until the heard stop. The recording completes once messages stamped
  before the stop are in; a full queue is reported (`midiOverrun`).
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
    on/off, snap, grid, length resolution, arming, metronome, count-in, punch,
    recording format) deliberately are not. The caller groups changes into
    transactions.
  - Recording (format version 3): each track has an input (`TrackInput`: none, or a
    device channel, mono or a stereo pair 1+2, 3+4, ...; stored counting from 1),
    a `MonitoringMode` (off / auto / on) and an arm state; the project has the
    metronome (on/off, volume), count-in (on/off, 1 or 2 bars), punch on/off and
    the recording bit depth (16, 24 or 32 float).
  - Effects and automation (format version 4): a project's children are its tracks
    then the `Buses` and `Master` containers; a track's are its clips then
    `Effects`, `Sends` and `Automation` (containers last, so track and clip
    indices are unchanged; `detail::countLeading()`). `model::EffectList` /
    `Effect` (type, bypass, parameter values by id at a float's precision so files
    hold them exactly, extra state in standard Base64), `Send` (bus id, level,
    pre-fader; one per bus), `Bus` (name, volume, pan, mute, effects, automation),
    `Automation` (mode read/off, not undoable; one `AutomationLane` per
    `AutomationTarget`: "volume", "pan", "send:<id>", "effect:<id>:<parameter>")
    with points (ticks, normalised value, bend) kept sorted. Removing an effect,
    a send or a bus removes what refers to it; on load, sends and lanes whose
    targets are missing are left out.
  - MIDI (format version 5): every track has a kind, "audio" or "midi"
    (`TrackKind`, fixed when made; version 4 tracks and clips migrate to audio). A
    MIDI track has a `MidiInput` (device name or every device, channel or every
    channel), the channel its notes have in MIDI files, and an instrument
    (`Track::getInstrument()`, an `Effect` in an `Instrument` container after the
    clips; `setInstrument()` gives a new id and removes the old one's lanes). A MIDI
    clip is a `Clip` with `isMidi()`, a name, and always a length; `MidiClip` gives
    its `Note`s (id unique in the clip, start relative to the content, length, key,
    velocity 1..127, MIDI 1.0 values) and `ControllerEvent`s (`ControllerType`: CC by
    number, pitch bend 0..16383, aftertouch; `restValue()`), each list kept whole in
    one binary property and replaced by every edit, so an edit is one undo step and
    thousands of notes stay cheap. `Track::addCopyOf()` duplicates a clip.
    `Effect::getFiles()` / `setFile()`: files an instrument plays (a sampler's), by
    key, stored relative to the project like audio. The grid has 1/64 and triplets
    (1/4T to 1/32T); the automatic grid goes down to 1/64.
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
- Effects in the controller (Phase 4): `buildArrangement()` turns chains (values
  normalised in the effect's parameter order, bypass, automation curves in read
  mode), buses (muted: gain 0), sends (none from a track that is not heard) and
  the master chain into the arrangement; without an `EffectLookup` every effect
  gets a fresh instance (exports). `controller::EffectInstances` keeps one engine
  instance per model effect across rebuilds, and gives a new one (fading in) to an
  effect that is new, changed type, moved (`markMoved()`, or found out of order)
  or after a rate change. `FadeOutKeeper` keeps removed effects (switched off),
  sends and buses (gain 0) in the arrangement for 150 ms so they fade out.
  `Session` edits effects (`ChainOwner`: track, bus or master), buses and sends
  with undo; delay and reverb start fully wet on a bus; while recording, edits
  that would change the latency are refused (`takeEditProblem()`).
- Automation in the controller (Phase 4): `Session` adds, moves (a drag is one
  gesture), removes and clears points of a target's lane (undoable) and switches
  an owner's mode (not undoable); `getAutomatedValue()` gives a target's value at
  the playhead, which controls show. `AutomationEditing.h` has the value of a lane
  at a position (the engine's own curve, in ticks), point and segment hit tests and
  the bend for dragging a segment.
- Export tail (Phase 4): `BounceSettings::includeTail` (the export dialog's
  "Include tail", on by default) renders on after the end with the timeline silent
  (`OfflineRenderer::setTimelineEnd()`), until the mix has stayed below -96 dBFS for
  1 s or for at most 30 s, and writes it up to its last sound: a mix without
  tails gets nothing more.
- MIDI edits in the controller (Phase 6): `Session` adds MIDI tracks (with
  `defaultInstrument`, "MIDI n"), sets their MIDI input, channel and instrument
  (`setInstrument()`, only instrument types; effects never go in as instruments, nor
  instruments in chains; `findEffect()` also finds instruments, index -1), adds,
  renames, duplicates (`duplicateClips()`: right after themselves, audio clips too)
  and copies clips (never across track kinds, nor moves), and replaces a MIDI clip's
  notes (`setNotes()`, `addNote()`, `removeNotes()`) or one controller type's events,
  each one undo step (a gesture groups a drag; an edit that changes nothing adds
  none).
- Note editing (Phase 6): `NoteEditing.h` holds the piano roll's rules for every UI,
  pure functions on note lists (content ticks) through a `ClipFrame` (content to
  timeline): `quantised()` (grid on the timeline, strength, ends with at least one
  step), `transposed()` (refused off the keyboard), `movedNotes()` (the grabbed note
  snaps, the selection follows, copies get new ids), `resizedEnds()`/`resizedStarts()`,
  velocities (set, or along a line), `notesIn()` a rectangle, `newNoteAt()` (on the
  line at or before), `hitNote()` (body or edge), and controller lanes
  (`eventsAlongLine()`, `hitControllerEvent()`, `controllerValueAt()`).
  `PianoRollViewport` is a `TimelineViewport` across and the keys up the side.
- Recording in the controller (Phase 3): `Session::record()` records every armed
  track with an input from the playhead (count-in, punch range, latency = device
  input + output latency + manual offset) through `controller::RecordingController`,
  which names the files (`Audio/<track> <n>.wav` next to the saved project, never
  overwriting), runs the writer, stops on problems (overrun, nearly full disk, a
  refused write, the device stopping) and reports a `RecordingOutcome`. At the
  `Session::update()` after the files are complete, the takes become clips in one
  undo step ("Record"), trimmed to what is kept (from the start or punch in to the
  stop or punch out); the files keep the rest as handles. Undo keeps the files;
  `discardRecording()` deletes them. Save As to another folder copies the project's
  recordings into the new `Audio/` folder.
- Crash recovery (Phase 3): the session keeps a `RecordingJournal`
  (`Audio/.recording-journal.json`) of the recordings its saved file does not have
  yet: written when a recording starts (end unknown) and when it ends, removed on
  save and when the project is left on purpose. Opening a project with a journal
  offers its recordings whose files exist and no clip uses
  (`getRecoverableRecordings()`); `recoverRecordings()` repairs them with
  `engine::io::repairWav()` (header lengths from the file size, whole frames,
  RIFF to RF64 when needed) and adds them as clips in one undo step.
- Latency measurement (Phase 3): `Session::measureLatency(output, input)` hands the
  engine a `LatencyTest` (the output carries only `dsp::LatencyProbe`'s signal:
  five pseudo-random noise bursts, each followed by 0.5 s of silence; no playback,
  no monitoring, so a cable cannot feed back) and the input is captured from the
  same sample. `controller::LatencyMeasurement` cross-correlates each burst's window
  (FFT, message thread), requires a clear peak and the bursts to agree within a
  sample, and gives the offset = measured round trip - reported latency.
- Disk streaming (Phase 3): `io::DiskStreamer` opens files as
  `io::StreamingAudioSource`s whose samples live in blocks of 32,768 samples per
  channel within a memory budget (512 MB; least recently needed blocks go first).
  The read-ahead thread loads what the plan (`setPlan()`: the arrangement's segments)
  plays near the playhead (`followPlayhead()`): 3 s ahead, the loop start, a second
  from where a stopped transport would start. `read()` copies loaded blocks only; a
  missing one gives silence, a counted dropout and `false`, never a wait (except in
  synchronous mode, for offline rendering). Files at another rate are converted
  while loading with `dsp::Resampling`, the same code `dsp::resample()` uses, so
  streamed audio equals the in-memory conversion to the bit. A dropped block is
  reused only once the engine's `RenderClock` shows the audio block that could read
  it is over.
- Streaming everywhere (Phase 3): the session's pool streams with a read-ahead
  thread that follows the engine's playhead (and the last seek target, loaded
  first); its plan is the arrangement playing. Playback that would start, or jump,
  onto audio not loaded yet waits for it (up to 2 s, then plays with dropouts):
  `Engine::isWaitingForData()`, reported as playing. A new arrangement while playing
  goes to the engine once its audio at the playhead is loaded (up to 0.5 s); the
  plan covers both meanwhile. Exports stream their own files synchronously (the
  export thread may wait for the disk). `Session::takeDropouts()` counts missing
  audio for the UI. Tests use `SessionOptions{DiskStreamer::Mode::manual}`: the
  read-ahead runs in `update()`, between audio blocks, deterministically.
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
- Desktop mixer (Phase 4): `MixerView` has a strip per track, then per aux bus
  (bluish; double-click the name to rename, right-click to delete), then the
  master. Every strip has its inserts (`EffectSlots`: click to open the editor,
  right-click to bypass, move, remove or reactivate; "+ Effect" adds one); track
  strips their sends (`SendSlots`: drag along a row for the level, double-click for
  0 dB, right-click for pre-fader or remove; "+ Send" to a bus or a new one).
  Faders and knobs whose automation reads follow it and are disabled. Shift+Cmd+T
  adds a bus.
- Effect editors (Phase 4): `EffectEditors` keeps one `EffectEditorWindow` per
  effect (closed when the effect goes): a header (Bypass; a warning with
  Reactivate after NaN or infinity), then `EqualiserView` (the exact response
  curve, a handle per band: drag for frequency and gain, wheel for Q, double-click
  to switch; automated values follow and cannot be changed) with the selected
  band's controls, `CompressorView` (static curve
  with the input level, gain-reduction bar and history; for the limiter only the
  reduction), and `GenericEffectEditor` rows made from `ParameterInfo` (sliders
  over 0..1 in the parameter's scale, menus, switches; automated ones follow and
  are disabled). The export dialog has "Include tail" (on).
- Automation view (Phase 4, A): `ArrangeState` holds whether it is on and each
  owner's chosen target; rows are the tracks, then the buses and the master
  (`numRows()`, `ownerOfRow()`). `TrackLanes` dims the clips and hands its rows to
  `AutomationLanes` (the lane drawn from the engine's own curve in ticks; click to
  add a point at the grid, drag to move, double-click to delete, Alt+drag to bend,
  right-click to clear or switch read/off; each one undo step). Track headers swap
  the recording controls for the target menu and the Read/Off button; buses and
  the master get headers of their own.
- Desktop recording UI (Phase 3): `TrackInputControls` (arm, monitoring cycling
  off/auto/on, the input menu of the device's channels, mono and stereo pairs) sits
  in each track header, with an input meter, and in each mixer strip. The transport
  bar has Rec (R), Click (K, right-click for its volume), Count (Shift+K, right-click
  for 1 or 2 bars), Punch (P) and a status line (count-in, disk time left, warnings
  such as dropouts). Recording a project never saved first asks where to save it;
  Cmd+. discards the recording in progress after a confirmation, and Escape never
  does. While recording, commands that could change or replace the project are off.
  Recordings are drawn as they grow, from the writer's waveform. File > Audio
  Settings (Cmd+,) has JUCE's device selector with inputs and a Recording section
  (file format, the latency the device reports, the manual offset, "Measure with a
  Cable..." opening `LatencyMeasurementDialog`); opening either asks for the inputs.
  Opening a project with recoverable recordings offers Recover / Delete / Not Now.
- `app/AppController` owns the engine, the device host, the session and the window;
  drives `Session::update()` at 60 Hz, passes device sample-rate and latency changes
  to the session, keeps the window title, and stores the chosen device and the
  latency offsets in the user settings (`~/Library/Application
  Support/DAW/DAW.settings` on macOS).
- Microphone permission (Phase 3): `engine::io::MicrophoneAccess` (AVFoundation on
  macOS and iOS, always granted elsewhere). The app opens the device's inputs only
  once it is granted; without it `AudioDeviceHost::initialise()` drops the input
  device from the saved state, so launching never prompts. `AppController` asks the
  first time `Session::needsInputs()` (a track armed, or monitoring always), then
  `AudioDeviceHost::openInputs()`. Denied: `Session::InputAccess::denied`, arming
  and recording refuse with `microphoneDeniedMessage`. The Info.plist carries
  `NSMicrophoneUsageDescription` (JUCE's `MICROPHONE_PERMISSION_*`). The app also
  passes the device's reported latencies to the session and keeps the manual offset
  in the user settings per input/output device pair and sample rate.
- Themes (Phase 5): `appearance::Theme` holds everything a UI needs to draw in one
  theme: a colour per `ColourRole` (components ask for roles, never literal colours),
  a font per `FontRole` (family, style, size, tracking, capitals), the shape (corner
  style and sizes, borders, glow, label, status-bar and pan styles), the meter
  (gradient or LED segments, colour stops from the loudest down), the clip colours
  and the animated effects it draws with their cost. The built-in themes are JSON
  files in `appearance/themes/`, embedded with `juce_add_binary_data` and read by
  `readTheme()`, which refuses missing, unknown or invalid values (all reported at
  once); colours are palette names, hex or `mix`/`alpha` formulas, so derived
  colours show where they come from. `ThemeLibrary::builtIn()` lists them in menu
  order; unknown ids fall back to Neon Grid.
- Contrast (Phase 5): `appearance::contrastProblems()` checks a theme against
  `colourPairs()` (every text/background and graphic/background combination the UIs
  draw: text 4.5:1, large text and graphics 3:1, WCAG 2 ratios), text on fills
  (`textRoleOn()` picks text or textOnAccent, whichever reads better), clip names and
  waveforms on every clip colour, the meter at every level, and status colours
  (record, mute, solo, clip, automation) at least 10 apart in CIELAB L*. Decorative
  lines, translucent overlays and disabled text are exempt. A UI that draws a new
  combination adds it to `colourPairs()`; the tests run the check on every theme.
- Animation settings (Phase 5): `appearance::AnimationSettings` (level full /
  reduced / off, a switch per `AnimationEffect`, the level to go back to after
  "switch all off", following the system's reduce motion). An effect runs when the
  theme draws it, its switch is on and the effective level allows it: reduced drops
  moving backgrounds, glitch and panel transitions; a system asking to reduce motion
  caps full at reduced. `animationLoad()` adds up the running effects' costs (low 1,
  medium 2, high 3; rated low up to 4, medium up to 7). `AppearancePreferences` (theme
  id and animations) goes to the user settings as JSON; reading it never fails.
- Fonts (Phase 5): six OFL families as static TTF files in `appearance/fonts/`
  (two weights at most each), embedded with the themes; `appearance::bundledFonts()`
  lists them by family and style. `ui::desktop::ThemeFonts` loads them as JUCE
  typefaces once and gives the font for a theme's `FontSpec` (an unknown family falls
  back to the default sans-serif). The licences go into the app (`Licences/`) with
  `docs/THIRD_PARTY.md`, which lists every third-party component.
- Theming the desktop UI (Phase 5): `ThemedLookAndFeel` (a `LookAndFeel_V4`) draws
  JUCE's widgets from a theme: JUCE colour ids set from colour roles, fonts from font
  roles (a Label's `fontRole` property picks its role), shapes in the theme's corner
  style (`shapeOf()`), text on fills via `textColourOn()`. One subclass per built-in
  theme (`src/ThemeLookAndFeels.h`) adds what the data cannot say; `make()` picks it.
  Components ask `ThemedLookAndFeel::of(component)` for roles and never hold literal
  colours or fonts. `ThemeManager` (owned by `AppController`, created before the
  window and destroyed after it) owns the fonts and one look and feel per theme,
  sets the current one as JUCE's default (JUCE then restyles every window, dialog,
  menu and tooltip on screen), notifies its listeners and hands each preference
  change to the app, which keeps it in the user settings under "appearance".
- Themed components (Phase 5): components state what they are with
  `ThemedLookAndFeel::setFontRole()`, `setColourRole()` (a label's text, a toggle's
  fill when on: record, mute, solo, monitor, automation) and `setSurface()` (a
  display on an inset), and paint through `ThemedLookAndFeel::get(*this)`: lanes,
  grid, loop, playhead (`playheadReach()` is what a move repaints), clips
  (`clipStyle()` for the track's clip colour, its header strip and readable name),
  ruler, meters (gradient or LED segments from the theme's meter), track headers, the
  `Logo` and the `StatusBar` (recording state, warnings, rate and format; a shell
  prompt in Acid Terminal), which replaced the transport's status line. The mixer
  (`drawMixerStrip()`, effect and send slots), the effect editors (graph background,
  grid, labels, response curves with their fill, EQ band handles in series colours
  taken from the clip colours, the compressor's curve, level dot and gain reduction)
  and the automation lanes (shade over the clips, curve, dashed level, points,
  labels) draw the same way; `AutomationLanes::paintRow()` takes the look and feel of
  the lanes it paints on.
  Dialogs (export, audio settings, latency measurement, recovery, alerts) use the
  look and feel too; `ThemeManager` gives every open window the new background
  colour when the theme changes, since JUCE windows keep the one they opened with.
  File choosers, the macOS menu bar and the title bars stay native.
- Animation clock (Phase 5): `AnimationClock` (owned by `ThemeManager`) is the one
  timer behind every decorative animation, at 30 Hz. Animated components subscribe
  for the `AnimationEffect` they draw, say when they want frames, and repaint only
  what changes. It runs only while a subscriber of an active effect wants frames and
  the main window can be seen (`MainWindow` reports minimised, hidden and, on macOS,
  fully covered via `detail::WindowOcclusion`); otherwise no timer runs. The 60 Hz
  functional refresh in `MainComponent` (meter levels, playhead, status) is separate.
  `ThemeManager::isEffectActive()` combines the theme, the switches, the level and
  `SystemMotion` (macOS NSWorkspace, Windows SPI_GETCLIENTAREAANIMATION, GNOME's
  enable-animations via gsettings on a background thread), read at start and when
  the window becomes active. Platform code lives in `src/SystemQueries_apple.mm` and
  `src/SystemQueries.cpp`.
- Animations (Phase 5): components get frames through `AnimationLink` (an RAII
  subscription to the clock of their look and feel) and ask it whether the effect is
  active; each theme's look and feel draws the effect (`drawTimelineBackground()`,
  `drawPlayhead()` with a `PlayheadLook`, `drawLogo()` with the clock's time,
  `drawHazardBand()`, `drawRecordPulse()`, `drawMeterBar()` with a `MeterAnimation`).
  `TrackLanes` paints its content (lanes, grid, loop, clips, automation) into an image
  of the visible area, redrawn only on `contentChanged()` (edits, view, selection,
  theme, size); frames put the image back and draw the background (under translucent
  lanes in Synth Chrome, over them in Neon Grid) and the playhead on it. Meters
  animate with the functional refresh, not the clock. Panel transitions: the mixer
  slides (`MainComponent::showMixer()`), and on a theme change a snapshot of the old
  look fades out (`ThemeCrossfade`, after `ThemeManager::Listener::
  themeAboutToChange()`); switching transitions off mid-way ends them at once.
- Theme and animation menus (Phase 5): View > Theme lists the library's themes as
  commands `MainComponent::firstTheme + index` (Cmd+1, Cmd+2, ...; ticked when in
  use) and Show Previews (`ThemePreviewsPanel`: a card per theme drawn with that
  theme's own look and feel, `ThemeManager::getLookAndFeelFor()`, with its fonts,
  shapes and meters; clicking one uses it). View > Animations opens `AnimationsPanel`
  (levels, a switch per effect with its cost and the theme's description, disabled
  when the level turns it off, the load, following the system), and All Animations
  Off (Shift+Cmd+A) toggles off and back. Panels are non-modal `PanelWindow`s that
  fade in with panel transitions. UI tests can write pictures of panels into the
  folder named by `DAW_UI_SNAPSHOTS`.
- Prerendered images (Phase 5): `PrerenderedImages` keeps images costly to paint
  (glows, background tiles) by key and size, painted once, least recently used
  dropped beyond 96; each look and feel has its own set, emptied when its theme
  leaves. Glows (`ThemedLookAndFeel::drawGlow()`, themes with `shape.glow` > 0) are
  painted this way.
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
- Closing a phase: the pull request goes into `main` as a true merge commit (never a
  squash), with `gh pr merge <N> --merge --author-email
  94704314+tavanzz@users.noreply.github.com` (otherwise GitHub authors it with the
  account's email), then an annotated tag `vX.Y.0` on that merge commit.
- Comments explain why, not what.
- Desktop UI components hold no literal colours or fonts (`juce::Colour(...)`,
  `Colours::...`, `FontOptions`, `juce::Font(...)`): they use theme roles through
  `ThemedLookAndFeel`. Checked by `architecture.ui_literals`
  (`cmake/CheckUiLiterals.cmake`); only the look and feel files are exempt.
- Message boxes go through `showAlert()` (`Alerts.h`), JUCE's alert window in the
  theme, never `NativeMessageBox`; it reports button indices as `NativeMessageBox`
  does.

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
- UI tests (themes, windows; JUCE's GUI running): `build/debug/tests/daw_ui_tests`. On
  Linux without a display, run them (or `ctest`) under `xvfb-run -a`. With
  `DAW_UI_SNAPSHOTS=<folder>` they also write pictures of the panels, the window (with
  and without automation), the effect editors and the export dialog in every theme.
  They play a full project twice, once with the window switching themes and animation
  levels between audio blocks, and require the same audio bit for bit, the same
  project JSON, revision, undo state and export.
- Build directories live in `build/<preset>/`; downloaded archives in `.cache/deps/`.
- Listening tests: `docs/LISTENING_TESTS.md`; reference files, the Phase 2 demo
  project, the Phase 3 recording project and the Phase 4 effects and delay
  compensation projects from `python3 tools/make_listening_test_files.py` (into
  `build/listening-tests/`; add `--long` for the hour-long streaming file).
- Benchmarks (Release only, not in CTest): `build/release/tests/daw_bench` (mixing),
  `build/release/tests/daw_stream_bench` (streaming and recording),
  `build/release/tests/daw_fx_bench` (effects) and the interface's cost per theme and
  animation level, `"build/release/tests/daw_ui_bench_artefacts/Release/DAW UI
  Bench.app/Contents/MacOS/DAW UI Bench"` (opens a window, always on top, for about
  four minutes); results in `docs/PERFORMANCE.md`.
- Screenshots for checking the UI from a session: capture only the app's window
  (`screencapture -l <window number>`), never the whole screen. The app's
  `--show=previews,animations,mixer,editors` option opens the theme previews, the
  animation settings, the mixer and the first track's effect editors, so they can be
  captured without clicking. The theme and the animations come from the user settings ("appearance" in
  `DAW.settings`, JSON): back the file up before changing them for screenshots and
  put it back afterwards.

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
  the two lists in sync. On Linux the tests run under `xvfb-run` (the UI tests need a
  display); with no window manager there, the UI tests first create the X11 atoms
  JUCE only looks up (`tests/support/WindowManagerAtoms.h`).
- When fixing a red CI job: at most 3 attempts on the same job. If it is still red
  after the third, stop pushing and explain the problem (what fails, suspected
  cause, what was tried) instead of trying again.

## Workflow

- Small steps, one commit per working step: it builds, all tests pass, zero warnings.
- Phase work happens on a branch (`phase-N`) with a draft pull request into `main`;
  CI must be green on Windows, macOS and Linux.
- Don't implement later phases (multiple takes and comping, plugins, MPE, MIDI clock,
  mobile) beyond keeping the architecture ready for them.

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

Phase 3 (done, released as v0.3.0): audio recording. Input device and channels;
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
- [x] 1. Audio input in the engine: input channels from the device, channel mapping,
      input meters; simulated device for tests
- [x] 2. Project format version 3: track input, monitoring and arm; metronome,
      count-in, punch and recording format; migration from version 2
- [x] 3. Input monitoring: per-track input/timeline switch with ramps, off/auto/on,
      transport fade on the timeline part only, mixer running while stopped
- [x] 4. Metronome and count-in, sample-exact on the tempo map's beats
- [x] 5. Recording pipeline: lock-free audio FIFO, WAV writer thread, overrun and
      disk-space reporting
- [x] 6. `Recorder` in the engine: latency-compensated capture, several tracks,
      punch in/out
- [x] 7. Recording in the controller: files, clips in one undo step, discard,
      errors, disk-space checks
- [x] 8. Crash recovery: recording journal, WAV repair, recovery on open
- [x] 9. Latency measurement with a loopback cable
- [x] 10. Disk streaming in the engine: block cache, read-ahead thread, sample-rate
      conversion while streaming, dropout reporting
- [x] 11. Streaming everywhere: source pool, transport waiting for data, export
- [x] 12. Microphone permission and app wiring (input device, latency offset)
- [x] 13. Desktop UI: arm, monitoring, inputs, input meters, record/metronome/
      count-in/punch controls, live recording clips
- [x] 14. Desktop UI: audio and recording settings, latency measurement, recovery,
      warnings
- [x] 15. Benchmark: tracks streamed and recorded at once (Release)
- [x] 16. Listening tests and files for Phase 3 (`docs/LISTENING_TESTS.md`)
- [x] Listening tests (`docs/LISTENING_TESTS.md`): all 18 passed on macOS, 2026-11-03

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

Phase 4 (done, released as v0.4.0): effects and automation. An insert chain
per track, per bus and on the master (add, remove, reorder, bypass, also while
playing, without clicks); aux buses fed by pre- or post-fader sends; built-in
parametric EQ (with high- and low-pass), compressor, limiter, tempo-synced delay and
reverb; plugin delay compensation keeping tracks, buses and sends aligned to the
sample; automation lanes for volume, pan, sends and every effect parameter (points
edited with the mouse, linear and curved segments, read/off); generic effect editors
plus dedicated EQ and compressor views; project format version 4 with a migration
from version 3. Out of scope: external plugins, sidechain, presets, MIDI, takes and
comping, mobile UI, writing automation live (touch/latch).

- [x] 0. Plan (this list) and draft pull request
- [x] 1. Effect interface (`fx::Effect`) and the host's slot: parameters, ramps,
      dry/wet fades, latency, NaN/Inf guard, denormals; test effects
- [x] 2. Insert chains in the mixer and the engine: persistent instances across
      snapshots, per-track clip crossfade, tails after stop; bit-identical to Phase 3
      with empty or bypassed chains
- [x] 3. Aux buses and pre/post-fader sends, mute and solo rules
- [x] 4. Plugin delay compensation: latencies, compensation delays, the heard
      position (playhead, metronome, count-in, recording), latency changes, export
- [x] 5. Automation in the engine: curves, per-sample values at the audio's position,
      volume, pan, sends and effect parameters
- [x] 6. Equaliser: SVF bands, Butterworth high/low-pass, response function
- [x] 7. Compressor and limiter, gain-reduction meters
- [x] 8. Delay: tempo sync, ping-pong, filtered feedback
- [x] 9. Reverb: feedback delay network with an exact decay time
- [x] 10. Project format version 4: effects, buses, sends, automation; migration from
      version 3
- [x] 11. Controller: chains, buses and sends in the session, effect instances,
      arrangement building with latencies, NaN reports
- [x] 12. Controller: automation editing and undo, values for the UI, export with tail
- [x] 13. Benchmark: cost of each effect, tracks with a typical chain per budget
- [x] 14. Desktop UI: inserts, sends, bus strips and master inserts in the mixer
- [x] 15. Desktop UI: generic effect editor, EQ curve, compressor view
- [x] 16. Desktop UI: automation lanes
- [x] 17. Listening tests and files for Phase 4 (`docs/LISTENING_TESTS.md`)
- [x] Listening tests (`docs/LISTENING_TESTS.md`): all 19 passed on macOS, 2026-11-03

Phase 4 decisions (agreed before starting):

- Effect interface: parameters with stable string ids, ranges, scales and
  normalised 0..1 mapping; the host owns the values and hands every block a constant
  or a per-sample value per parameter (ramps and automation already applied), so
  automation is sample-exact and block-size independent; `prepare()` on the message
  thread, `process()` and `reset()` noexcept on the audio thread; latency and tail
  length; opaque extra state for future plugins.
- Inserts are pre-fader on tracks and buses, post-fader on the master (a limiter
  there holds its ceiling in exports too). Buses go to the master only: no sends
  from buses, no bus to bus. A bus has inserts, volume, pan, mute and a meter, no
  solo.
- Post-fader sends tap after volume and pan, pre-fader sends after the inserts. A
  track that is not heard (mute, or another track soloed) sends nothing; solo never
  mutes a bus. A new send is post-fader at 0 dB (-inf to +6 dB); one send per bus
  per track.
- Chain changes: adding, removing or bypassing fades the effect's dry/wet over
  10 ms; moving an effect fades it out at its old place while a new instance with
  the same parameters fades in at the new one. A bypassed effect is not processed,
  starts from a clean state when re-enabled and keeps its latency.
- Parameter changes ramp over 20 ms in the normalised domain (in the host); choice
  parameters (filter type, slope, sync) switch with an internal 10 ms crossfade.
- A change of the total latency while audio runs: 5 ms fade-out, silence while the
  new latency fills, 5 ms fade-in. While recording such edits are refused.
- Delay compensation delays the faster paths; monitored inputs go through the
  track's chain and get the compensated latency too (no low-latency mode).
- Automation mode per track, bus and master: read or off (not undoable; points
  are). Targets: volume, pan, send levels, every automatable effect parameter, bus
  volume and pan, master volume; not mute or bypass. Segments linear or curved
  (tension -1 to 1, Alt+drag); points snap to the grid (Cmd bypasses); values stored
  normalised (volume with the fader law). In read, automated controls follow the
  curve and cannot be dragged. Lane visibility is view state (not saved). Shortcuts:
  A automation lanes, Shift+Cmd+T new bus.
- EQ: 8 bands (high-pass, low shelf, 4 bells, high shelf, low-pass); Butterworth
  high/low-pass 6 to 48 dB/oct; gains +-24 dB, Q 0.1 to 18; trapezoidal SVF filters;
  no analyser.
- Compressor: feed-forward, peak detector in dB, linked channels, no latency;
  threshold -60 to 0 dB, ratio 1:1 to 20:1, knee 0 to 24 dB, attack 0.1 to 100 ms,
  release 10 to 2000 ms, makeup 0 to 24 dB, mix; attack and release are time
  constants (63 %).
- Limiter: sample peak (true peak later), fixed 1.5 ms lookahead (its latency),
  ceiling -24 to 0 dB (-0.3 by default), gain 0 to 24 dB, release 1 to 1000 ms.
- Delay: up to 4 s, feedback 0 to 95 %, sync 1/64 to 1/1 with dotted and triplets,
  stereo or ping-pong, low/high cut in the feedback; time changes crossfade between
  two taps over 50 ms (no tape pitch effect).
- Reverb: 16-line feedback delay network with light modulation; pre-delay 0 to
  250 ms, decay (RT60) 0.2 to 20 s, size, damping, low/high cut, width, mix.
- Delay and reverb start at 100 % wet on a bus, 25 % on a track or the master.
- An effect that outputs NaN or infinity is reset and silenced (not passed dry,
  which could be much louder), and reported; it stays silent until re-enabled.
- Denormals: flush-to-zero on the audio and export threads (the same mode, so
  exports still equal playback); effects skip work when input and state are exactly
  zero.
- Tails ring on after stop and pause. Export option "Include tail" (on): after the
  end, without clips, until the mix stays below -96 dBFS for 1 s, at most 30 s.
- One editor window per effect; delay, reverb and limiter use the generic editor.
- Effects are saved as type, bypass and parameters by id in real units (Hz, dB,
  ms), plus optional opaque data for future plugins. A device sample-rate change
  recreates the effects.

Phase 5 (done, released as v0.5.0): themes and animations. Three cyberpunk
themes (Neon Grid, Acid Terminal, Synth Chrome) applied to the whole interface:
transport, tracks, timeline, clips, ruler, mixer, meters, automation lanes, effect
editors (EQ curve and compressor gain reduction included), dialogs, menus and
alerts; switched live, without a restart; the choice is a user setting, not part of
the project (default Neon Grid). Animations at three levels (Full / Reduced / Off)
with a switch per effect, following the system's "reduce motion" setting. Theme
values live in a GUI-free data module the mobile UI can reuse; one LookAndFeel per
theme in `ui/desktop`; bundled OFL fonts; one shared animation timer; UI frame time
measured in Release. The project format does not change. Out of scope: a custom
theme editor, a light theme, interface scaling, the mobile UI, MIDI.

Steps 7 and later restyle the Phase 4 components (inserts, effect editors,
automation lanes), so they started once Phase 4 was in `main` (v0.4.0) and merged
into `phase-5`.

- [x] 0. Plan (this list) and draft pull request
- [x] 1. `appearance` module: theme data (colour roles, fonts, shapes, meter style,
      effects), the three themes as embedded JSON, loader; every theme defines every
      value
- [x] 2. Contrast and status colours: WCAG contrast for every text/background pair,
      status colours apart in lightness, derived colours
- [x] 3. Animation settings: levels, per-effect switches, reduced/off rules, system
      reduce motion, costs and total load, user-setting persistence
- [x] 4. Bundled fonts (OFL), `docs/THIRD_PARTY.md`, typeface loading
- [x] 5. LookAndFeel base and one per theme for JUCE widgets; live switching across
      windows; theme stored in the user settings; UI test target
- [x] 6. Shared animation clock, window visibility, system reduce motion, cache of
      precomputed images
- [x] 7. Arrange view themed: transport bar with logo, bottom status bar, track
      headers, input controls, ruler, lanes, clips, waveforms, playhead, loop, meters
- [x] 8. Mixer (strips, faders, pan, inserts, sends, buses), effect editors (generic,
      EQ curve, compressor gain reduction), automation lanes themed
- [x] 9. Dialogs and alerts themed (export, audio settings, latency, recovery,
      themed alert windows); no colour or font literals outside the LookAndFeels
- [x] 10. Animations: each theme's own and the shared ones (meters, record pulse,
      panel transitions, theme crossfade)
- [x] 11. View > Theme (Cmd+1/2/3), theme previews panel; View > Animations panel,
      Shift+Cmd+A
- [x] 12. UI tests: every theme paints every component; switching themes leaves the
      project and the audio unchanged; the clock stops when off or hidden
- [x] 13. Benchmark: UI frame time and CPU per theme and animation level with a large
      project (Release), `docs/PERFORMANCE.md`
- [x] 14. Screenshots and visual checks in `docs/LISTENING_TESTS.md`; version 0.5.0
- [x] Visual checks (`docs/LISTENING_TESTS.md`): all 15 passed on macOS, 2026-11-03

Phase 5 decisions (agreed before starting):

- Neon Grid: background #07060B, panels #0E0C17 and #0A0912, lines #221E36, text
  #E9E6FF, secondary #8F8AAE; accents cyan #00F0FF, magenta #FF2BD6, yellow
  #F8F32B, violet #9D7BFF. Chakra Petch (interface), JetBrains Mono (numbers and
  times). Rounded corners 3 to 6 px, neon glows on active elements. Meters a
  cyan-yellow-magenta gradient. Own animations: scrolling scanlines, flickering
  logo, pulsing playhead.
- Acid Terminal: background #0B0D08, panels #12150E, lines #2A3020, text #EDEFE6,
  secondary #949A80; accents acid yellow #D4FF00, red #FF3B30, light blue #7CE0FF.
  Space Grotesk (titles), IBM Plex Mono (everything else). Sharp corners, 2 px
  borders, terminal-style labels, a prompt-style status bar. LED segment meters.
  Own animations: scrolling hazard stripes, logo glitch, blinking status-bar cursor.
- Synth Chrome: background #0F0820, panels #140B2A, #180E30 and #241645, lines
  #2E2050, text #F2EAFF, secondary #A397C4; accents orange #FF8A33, violet #B07CFF,
  pink #FF3D7F, turquoise #5CE1E6. Michroma (titles and displays), Rajdhani
  (interface). Corners cut at 45 degrees, arc pots for pan. Meters a
  violet-orange-pink gradient. Own animations: perspective grid scrolling under the
  timeline, playhead trail, chrome sweep on the logo.
- Data: a new module `appearance/` (`daw::appearance`, juce_core only, no GUI): the
  themes as embedded JSON read into typed values, contrast maths, animation settings
  and their rules, preference serialisation. Components draw only through their
  theme's LookAndFeel; no colour or font literals in components (checked by a test).
- Colours the palettes lack (text on accents, control borders, disabled text, status
  variants) are derived from the palette (lighter or darker mixes) and documented.
  Grid lines and separators are decorative and exempt from the 3:1 rule, as in WCAG;
  so are disabled controls.
- Contrast: text at least 4.5:1 on its background; large text and graphic elements
  at least 3:1. Status colours (record, mute, solo, clip, active automation): every
  pair at least 10 apart in CIELAB lightness (L*), each at least 3:1 on its
  background.
- Shortcuts: Cmd+1/2/3 select the themes; Shift+Cmd+A switches animations off, and
  again back to the previous level. The previews and animation panels have no
  shortcut.
- Effects and their switches: moving backgrounds = scanlines (Neon), a hazard-stripe
  band under the transport, faster and red while recording (Acid), perspective grid
  (Synth); glitch = the logo's flicker, glitch or chrome sweep; cursor glow = pulsing
  playhead (Neon), blinking status-bar cursor (Acid), playhead trail (Synth);
  animated meters = a glow on the level (Neon, Synth), LED afterglow (Acid) (off:
  meters still show level and peak, whose fall is the meter's own ballistics); record pulse = the Rec button and recording clips;
  panel transitions = the mixer sliding, panels fading, a crossfade on theme change.
  "Reduced" turns off moving backgrounds, glitch and transitions.
- One animation timer at 30 Hz, stopped when nothing animates, at level Off, or when
  the window is minimised, invisible or (macOS) fully covered; the functional 60 Hz
  refresh (meters, playhead, status) stays separate. Only changed areas repaint;
  glows and backgrounds are images rebuilt on theme or size changes only.
- System "reduce motion" (macOS, Windows, GNOME on Linux), followed by default,
  caps the level at Reduced.
- Themed: context menus, the menu bar on Windows and Linux, alerts and dialogs
  (native message boxes become themed alert windows). Native: the macOS menu bar,
  file choosers and the title bar.
- Layout: a status bar at the bottom in every theme (the transport's status line
  moves there; a prompt in Acid Terminal); a "DAW" logo at the left of the transport
  in the theme's title font.
- Fonts: static TTF files in the repository (at most two weights per family), from
  google/fonts and the upstream releases of JetBrains Mono and Space Grotesk; OFL
  texts next to them, in `docs/THIRD_PARTY.md` and in the app bundle.
- UI tests: a `daw_ui_tests` target in CTest on all five jobs (under `xvfb-run` on
  Linux if a display is needed).
- Benchmark project: 64 tracks and 4 buses, an EQ, a compressor and a send on every
  track, automation on volume and two parameters; a 1600x1000 window with the mixer
  open, playing; 20 s per combination.
- Screenshots are not committed (`build/screenshots/phase5/`).

Phase 6 (in progress, branch `phase-6`): MIDI and instruments. MIDI tracks with MIDI
clips on the timeline (create, move, trim, duplicate, delete); a piano roll (draw,
move, resize and delete notes, velocity, multiple selection, grid and snap,
quantisation with value and strength, transposition, a preview of the note being
drawn); MIDI input from devices with monitoring of armed tracks and MIDI recording
with Phase 3's count-in, punch and metronome (one undo step per recording); built-in
instruments (a polyphonic subtractive synth, a sampler, a drum sampler with pads) at
the start of the track's chain, so Phase 4's inserts, sends, automation and delay
compensation apply to MIDI tracks and instrument parameters are automatable; pitch
bend, mod wheel, sustain, aftertouch and any CC as lanes under the piano roll; import
and export of standard MIDI files (type 0 and 1); project format version 5 with a
migration from version 4. Out of scope: external plugins, MPE, MIDI clock and
external sync, arpeggiator, step sequencer, score editor, mobile UI, MIDI output to
devices, loop/take/merge recording, swing, presets.

- [x] 0. Plan (this list) and draft pull request
- [x] 1. MIDI events and the instrument interface: `fx::Effect` takes sample-exact
      events, `EffectInstance` delivers them (note-offs and reset on bypass and
      removal); a test instrument
- [x] 2. MIDI in the arrangement and the mixer: notes and controllers in samples per
      track, the instrument first in the chain, note-on and note-off on the exact
      sample for any block size, at clip edges and in loops; controller chase
- [x] 3. No hanging notes: stop, pause, seek, loop, new arrangements (notes reconciled
      by identity), mute and solo, bypass, removal, rate changes; export equals
      real-time playback with instruments and automation
- [x] 4. MIDI input in the engine: lock-free timestamped queues per device, time to
      sample, monitoring of armed tracks, preview notes, note-offs when a track stops
      listening; simulated MIDI device for tests
- [x] 5. MIDI recording in the engine: what was heard (history, output latency and
      delay compensation), count-in, punch, metronome; exact ticks with a known latency
- [x] 6. Project format version 5: track type, instrument, MIDI input and channel; MIDI
      clips with notes and controller lanes; grid with 1/64 and triplets; migration
      from version 4
- [x] 7. Undoable edits: MIDI tracks, MIDI clips (create, move, trim, duplicate,
      delete; duplicate audio clips too), notes (add, move, resize, delete, velocity,
      transpose) and controller events
- [x] 8. Note editing in the controller: quantisation (value, strength, ends), snap,
      selection, hit tests, drag rules, transposition, `PianoRollViewport`
- [ ] 9. Controller: MIDI tracks in the arrangement (clip overlaps and edges,
      controller steps), instrument instances, sample loading, preview, MIDI inputs
- [ ] 10. MIDI recording in the controller and the app: armed MIDI tracks with the
      audio ones in one "Record" step, handles, punch, discard; MIDI devices in
      `AudioDeviceHost`
- [ ] 11. Subtractive synth: band-limited wavetable oscillators, resonant SVF, two
      envelopes, LFO, glide, preallocated voices and stealing
- [ ] 12. Sampler: samples in memory, interpolated pitch, envelope, loop
- [ ] 13. Drum sampler: 16 pads, choke groups
- [ ] 14. Standard MIDI files: import and export, type 0 and 1, round trips
- [ ] 15. Benchmark: cost per voice of each instrument, voices per budget (Release)
- [ ] 16. Desktop UI: MIDI tracks in the arrange view (headers, MIDI clips with their
      notes), the instrument slot in the mixer, new colour roles in the three themes
- [ ] 17. Desktop UI: piano roll (keyboard, notes, grid, selection, velocity,
      quantisation, transposition, preview)
- [ ] 18. Desktop UI: controller lanes (pitch bend, mod wheel, sustain, aftertouch, CC)
- [ ] 19. Desktop UI: instrument editors (grouped generic synth editor, sampler and drum
      pad views), MIDI settings, MIDI import and export in the menus
- [ ] 20. UI tests of the new components in every theme; listening tests and files
      for Phase 6 (`docs/LISTENING_TESTS.md`); version 0.6.0

Phase 6 decisions (agreed before starting):

- Note positions are ticks (as audio clips); a tempo change keeps notes at their time
  position, like audio. Overlapping MIDI clips on a track: the one on top covers the
  ones below (as audio). A clip's notes are relative to its content: trimming its
  start hides notes, extending it reveals them; a note plays if it starts inside the
  clip and ends at the clip's end at the latest.
- Starting playback in the middle of a note does not play it (no note chase);
  controllers are chased (the value at that point is sent first). At the end of a
  clip that changed them, controllers return to their rest values: pitch bend centre,
  CC7 and CC11 127, every other CC and aftertouch 0. Controller events are steps
  inside their clip.
- No hanging notes: the engine keeps a table of the notes sounding on each track and
  sends their note-offs itself (never relying on CC123); stop, pause, seek, the loop's
  wrap, mute/solo (a track not heard plays no notes), bypass and removal of the
  instrument send note-offs; a new arrangement keeps unchanged notes sounding, ends
  removed or moved ones at once and gives shortened ones their new end. A key struck
  again while sounding gets a note-off first. At one sample: note-offs, controllers,
  note-ons.
- Instrument tails ring on after stop, as effect tails do.
- Instrument interface: `fx::Effect` with events in `ProcessBlock` (note on/off with
  note id, channel, key, velocity 0..1; controller, pitch bend, channel pressure
  normalised), mapping one to one onto VST3 event lists, CLAP note/MIDI events and AU
  MIDI events; exact for MIDI 1.0, ready for MIDI 2.0 resolution. The instrument is
  the first slot of a MIDI track's chain; its parameters are automated through the
  `effect:<id>:<parameter>` targets.
- Voices preallocated (up to 32 per instrument, plus spare voices for stealing).
  Stealing: the quietest releasing voice, otherwise the oldest; the stolen voice fades
  out over 3 ms on a spare voice while the new note starts on time.
- Instruments are independent of the host's block size: everything per sample or on
  a 16-sample grid counted by the instrument itself.
- MIDI input: every device enabled by default (also those plugged in later), switched
  off in Audio Settings; per-track input = all devices or one, all channels or one.
  Events are placed by their timestamps one audio block late (no jitter), as JUCE's
  MidiMessageCollector does. Monitoring: auto while armed, on, off; the live input is
  added to the clips' notes. Recording compensates the device output latency and the
  delay compensation (no manual offset: that one measures the audio round trip).
- MIDI recording: armed MIDI tracks record with the audio ones, one undo step
  ("Record"); the clip runs from the start (or punch in) to the stop (or punch out);
  notes from the count-in and before the punch stay hidden in the clip as handles;
  notes held at the stop end there. MIDI-only recordings do not need a saved project
  and are not recovered after a crash. No loop, take or merge recording.
- Piano roll: a lower panel instead of the mixer (E shows it; double-clicking a MIDI
  clip opens it). Click on empty space adds a note (length and velocity of the last
  note touched; at first one grid step and velocity 100), drag on empty space selects
  with a rectangle, drag a note to move it, its edges to resize it, double-click
  deletes, Alt+drag duplicates. Q quantises the selected notes (all if none) to the
  grid by default at 100 % strength, starts only unless "ends" is on; no swing. Up/down
  transpose by a semitone, Shift by an octave (refused beyond 0..127). The preview
  plays through the track while the mouse is down, and from the piano keys. Middle C
  (note 60) is C3.
- Lanes under the notes: velocity, then any of pitch bend, mod wheel (CC1), sustain
  (CC64), aftertouch and any CC by number. Click adds an event, drag moves it,
  double-click deletes it, Alt+drag draws a straight line of events.
- Built-in instruments follow pitch bend (range +-2 semitones by default, up to 24),
  mod wheel (vibrato), sustain, CC7 and CC11 (level); other CCs are saved, exported
  and delivered (for future plugins).
- Synth: two oscillators (sine, triangle, saw, square/pulse) and noise; band-limited
  wavetables (no harmonic above Nyquist by construction, adjacent tables crossfaded as
  the pitch moves), aliasing below -80 dB of the fundamental up to C8 at 44.1 and
  48 kHz; a linear trapezoidal SVF (low-pass 12 and 24 dB, band-pass, high-pass) with
  resonance, key tracking, envelope and velocity amounts; envelope 1 on the amplitude,
  envelope 2 on the filter and the pitch (attack linear to the peak in the set time,
  decay and release exponential, 60 dB in the set time); an LFO (free or synced) to
  pitch, filter, amplitude and pulse width; glide 0 to 2 s, poly, mono or legato; 1 to
  32 voices (16).
- Sampler: WAV or AIFF in memory (decoded on a worker thread), 8-point windowed-sinc
  interpolation with filtered copies for upward transposition, root key, transpose,
  fine tune, start, loop with crossfade, envelope, velocity. Drum sampler: 16 pads on
  notes 36 to 51 (General MIDI), each with a sample, level, pan, tune, decay and choke
  group. Sample files are kept relative to the project, like audio clips.
- Synth editor: the generic editor in sections; the sampler and the drum sampler have
  views of their own (waveform with loop and root key; pad grid).
- MIDI files: Cmd+I (or a drop) also imports .mid: one track per file track (and per
  channel when a track mixes several), named after it; channel 10 gets the drum
  sampler; positions in beats at the project tempo; an empty project first takes the
  file's tempo and metre; tempo changes, program changes, polyphonic aftertouch, SysEx
  and other meta events are left out and reported. File > Export MIDI File...
  (Alt+Cmd+E): every MIDI track, type 1 (or 0), 960 PPQ, names, channels, tempo,
  metre, notes and controllers.
- New MIDI track: Alt+Cmd+T, with the synth (two slightly detuned saws through a
  low-pass), every input, every channel, monitoring auto, channel 1. Cmd+D duplicates
  the selected clips right after themselves (audio too); Alt+drag copies; a
  double-click on an empty part of a MIDI track makes a one-bar clip.
- Benchmark: cost per voice of each instrument at 48 kHz and how many voices fit in
  the whole and half the budget with 128, 256 and 512-sample blocks.
