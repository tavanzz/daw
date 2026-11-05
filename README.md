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
format version 2.

Phase 3 (v0.3.0): audio recording. Inputs per track (mono or stereo),
arming and recording several tracks at once, input monitoring (off / auto / on) and
input meters, latency compensation with a loopback measurement, metronome and
count-in, punch in/out on the loop region, recovery of recordings interrupted by a
crash, and playback and recording streamed from disk. Project format version 3.

Phase 4 (v0.4.0): effects and automation. Insert chains on every track, aux bus
and the master (add, remove, reorder and bypass while playing, without clicks); aux
buses fed by pre- or post-fader sends; built-in equaliser, compressor, limiter,
tempo-synced delay and reverb; delay compensation that keeps every path aligned to
the sample; automation lanes for volume, pan, sends and every effect parameter;
effect editors with the equaliser's response curve and the compressor's gain
reduction; exports with the effects' tails. Project format version 4.

Phase 5 (v0.5.0): themes and animations. Three themes for the whole interface
(Neon Grid, Acid Terminal, Synth Chrome), switched live from View > Theme, with
bundled fonts and readable contrast checked by the tests; animations at three levels
with a switch per effect, following the system's "Reduce motion". The project
format does not change.

Phase 6 (v0.6.0): MIDI and instruments. MIDI tracks with MIDI clips; a piano roll
(draw, move, resize, delete, velocity, quantise with strength, transpose, previews)
with controller lanes for pitch bend, mod wheel, sustain, aftertouch and any CC; MIDI
input from every connected keyboard, monitored on armed tracks and recorded with the
count-in, punch and metronome; three built-in instruments (a polyphonic subtractive
synth with band-limited oscillators, a sampler and a sixteen-pad drum sampler) at the
start of the track's chain, so effects, sends, automation and delay compensation
apply; standard MIDI file import and export (type 0 and 1). Project format version 5.
See [CLAUDE.md](CLAUDE.md#status) for the steps.

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
      libxinerama-dev libxrandr-dev libxrender-dev libxi-dev \
      xvfb
  ```

  `xvfb` is only needed to run the UI tests without a display:
  `xvfb-run -a ctest --preset debug`.

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

Recording:

- Give a track an input with the button showing "In 1" (mono channels and stereo
  pairs of your interface), arm it with R, and press R (or the Rec button) to record
  every armed track from the playhead; press R, Space or Return to stop. Each take
  becomes a clip (one undo step) and a WAV file in the `Audio` folder next to the
  project, which is why a new project is saved first.
- The first time a track needs an input, macOS asks whether DAW may use the
  microphone. If you refuse, allow it later in System Settings > Privacy & Security
  > Microphone.
- Input monitoring per track: Auto plays the input while the track is armed and
  stopped and while it records; On always; Off never. Use headphones.
- Click (K) is the metronome, Count (Shift+K) counts in before recording (right-click
  for 2 bars), Punch (P) records only inside the loop region.
- File > Audio Settings chooses the input and output devices, the recording format
  (16-bit, 24-bit or 32-bit float WAV at the device's rate) and the latency offset.
  "Measure with a Cable..." measures your interface's round trip with a cable from
  an output to an input, so that recordings line up exactly with what you heard.
- Cmd+. discards the recording in progress (after asking). If the app quits during a
  recording, opening the project again offers to recover it.
- Files play from disk; projects of any length need no more than about 600 MB of
  memory for their audio.

MIDI and instruments:

- Alt+Cmd+T adds a MIDI track with the synth. Double-click an empty part of it to make
  a one-bar clip, double-click a clip to edit it in the piano roll (E shows and hides
  it in the mixer's place). Cmd+D duplicates the selected clips; Alt+drag copies one.
- In the piano roll: click to add a note (you hear it), drag a note to move it (Alt
  copies), its edges to resize it, drag on empty space to select, double-click to
  delete. Up and Down transpose (Shift: an octave), Q quantises to the value in the
  toolbar with its strength (and the ends, if ticked). Drag the velocity bars; Lanes
  adds controller lanes (pitch bend, mod wheel, sustain, aftertouch, any CC): click to
  add an event, drag it, double-click to delete it, Alt+drag to draw a line.
- Every connected MIDI keyboard plays the armed MIDI tracks (Auto monitoring), or
  every track whose monitoring is On; the track's "MIDI: All" button picks a device
  and a channel; File > Audio Settings switches keyboards off. R records the armed
  MIDI tracks with the audio ones, with the count-in, punch and metronome.
- The mixer lists a MIDI track's instrument first; click it for its editor (the
  sampler shows its waveform and loop, the drum sampler its pads: drop WAV or AIFF
  files on them), right-click to change, bypass or remove it. Instrument parameters
  can be automated like effect parameters.
- Cmd+I imports MIDI files (one track per part; channel 10 gets the drum sampler);
  File > Export MIDI File... writes the MIDI tracks as a type 1 or type 0 file.

Effects, buses and automation:

- Every mixer strip lists its effects (inserts): "+ Effect" adds an equaliser,
  compressor, limiter, delay or reverb; click one to open its editor; right-click it
  to bypass, move or remove it. Track and bus effects come before the fader, the
  master's after it. All of it works while playing.
- Shift+Cmd+T adds an aux bus. "+ Send" in a track strip sends the track to a bus (or
  a new one); drag along the send to set its level, double-click it for 0 dB,
  right-click it to make it pre-fader or remove it. A bus strip's name can be
  renamed with a double-click; right-click deletes the bus.
- The equaliser's window shows its response curve: drag a band's handle for
  frequency and gain, use the wheel for its width (Q), double-click to switch it on
  or off. The compressor's shows its curve and gain reduction, the limiter's its
  gain reduction. Values can also be typed (in Hz, dB, ms).
- The delay follows the tempo when Sync is on; a delay's or reverb's tail rings on
  after you stop. Export's "Include tail" lets the file run on until it has died away.
- A shows the automation lanes: choose a target in each header (volume, pan, a send,
  any effect parameter), click to add a point, drag to move it, double-click to
  delete it, Alt+drag a segment to curve it; right-click to clear the lane or turn
  automation off. Buses and the master have rows of their own. Automated controls
  follow their lane.
- Effects that delay their output (the limiter looks 1.5 ms ahead) are compensated:
  everything else is delayed to match, so tracks, buses and sends stay aligned.

| Action | Shortcut |
|--------|----------|
| Play / pause | Space |
| Stop (back to where playback started; again: to the start) | Return |
| Loop / Snap / Mixer | L / N / X |
| Undo / Redo | Cmd+Z / Shift+Cmd+Z |
| Delete the selected clips | Backspace or Delete |
| New track | Cmd+T |
| Import audio or MIDI / Export mix | Cmd+I / Cmd+E |
| New / Open / Save / Save As project | Cmd+N / Cmd+O / Cmd+S / Shift+Cmd+S |
| Zoom in / out / to fit | Cmd+= / Cmd+- / Cmd+0 (or Cmd + wheel, or pinch) |
| Record / stop recording | R |
| Discard the recording in progress | Cmd+. |
| Metronome / count-in / punch | K / Shift+K / P |
| Automation lanes on/off | A |
| New aux bus | Shift+Cmd+T |
| Audio settings | Cmd+, |
| Theme: Neon Grid / Acid Terminal / Synth Chrome | Cmd+1 / Cmd+2 / Cmd+3 |
| All animations off (again: back on) | Shift+Cmd+A |
| New MIDI track | Alt+Cmd+T |
| Duplicate the selected clips (or notes) | Cmd+D |
| Piano roll / quantise | E / Q |
| Transpose the selected notes (Shift: an octave) | Up / Down |
| Export MIDI file | Alt+Cmd+E |

(Ctrl instead of Cmd on Windows and Linux.)

Appearance: View > Theme chooses one of three themes (Neon Grid, the default; Acid
Terminal; Synth Chrome), which applies at once to every window; Show Previews puts
them side by side. View > Animations sets how much moves: Full, Reduced (no moving
backgrounds, glitches or panel transitions) or Off, and each effect on its own; by
default the app follows the system's "Reduce motion". Both are kept in the user
settings, not in projects.

Projects (`.dawproject`) are small JSON files that refer to the audio files; keep the
audio next to the project (or in a subfolder) and the folder can be moved as a whole.
Projects saved by earlier phases open too. Manual checks with reference files:
[docs/LISTENING_TESTS.md](docs/LISTENING_TESTS.md). How many tracks fit in an audio
callback, stream from disk and record, what each effect costs and what each
instrument's voice costs:
[docs/PERFORMANCE.md](docs/PERFORMANCE.md).

## Repository layout

| Path          | Contents                                                        |
|---------------|-----------------------------------------------------------------|
| `engine/`     | audio engine; never depends on the user interface               |
| `model/`      | project state, timeline time, grid, versioned project files     |
| `controller/` | maps the project onto the engine; shared by every UI            |
| `appearance/` | themes (colours, fonts, shapes) and animation settings; no GUI |
| `ui/desktop/` | mouse and keyboard interface                                    |
| `ui/mobile/`  | touch interface (placeholder)                                   |
| `app/`        | application entry point                                         |
| `tests/`      | unit tests; `tests/bench/` the benchmarks                       |
| `cmake/`      | build helpers: dependencies, warnings, architecture checks      |
| `tools/`      | development scripts (listening-test files)                      |
| `docs/`       | manual test procedures, performance measurements, licences     |

Architecture, audio-thread rules and coding conventions are described in
[CLAUDE.md](CLAUDE.md).

## Licence

Copyright © 2026. All rights reserved. No open-source licence has been chosen for
this project's own code yet.

Third-party components keep their own licences: JUCE (AGPLv3 or commercial, see
the note at the top), Catch2 (Boost Software License 1.0, tests only) and the fonts
built into the app (SIL Open Font License 1.1). See
[docs/THIRD_PARTY.md](docs/THIRD_PARTY.md).
