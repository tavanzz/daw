# Listening tests

What the automated tests cannot check: the real audio device, the mouse, and your ears.
Run these on macOS after any change to the engine, the device code or the UI.

## Setup

```bash
cmake --preset release && cmake --build --preset release
python3 tools/make_listening_test_files.py
open build/release/app/daw_app_artefacts/Release/DAW.app
```

The reference files are written to `build/listening-tests/` (Phase 2 material in its
`phase2/` folder). Use moderate volume: file 05 is at full scale.

The first time the app reads a file in your Documents folder by itself (when it
opens a project), macOS asks whether DAW may access the Documents folder: allow it.
Until you answer, the clips stay "Loading".

| Action | Shortcut |
|--------|----------|
| Play / pause | Space |
| Stop (back to where playback started; again: to the start) | Return |
| Loop on/off | L |
| Snap on/off (hold Cmd while dragging to ignore it) | N |
| Mixer on/off | X |
| Import audio / Export mix | Cmd+I / Cmd+E |
| New / Open / Save / Save As | Cmd+N / Cmd+O / Cmd+S / Shift+Cmd+S |
| Undo / Redo | Cmd+Z / Shift+Cmd+Z |
| Delete the selected clips | Backspace or Delete |
| New track | Cmd+T |
| Zoom in / out / to fit | Cmd+= / Cmd+- / Cmd+0, or Cmd + wheel, or pinch |
| Audio output | Cmd+, |

## Phase 1 checks (single file, still valid)

Start each one from File > New Project (Cmd+N) and add the file with File > Import
Audio (Cmd+I) or by dragging it onto the empty track area; it lands at the playhead.

| # | Do | You should hear / see |
|---|----|------------------------|
| 1 | File > Audio Output: pick your interface, close the window. Quit and reopen the app. | The same device is selected again. |
| 2 | Import `01-sine-1k-minus6dB.wav`, press Space. | A steady 1 kHz tone, no crackles. Both master meter bars sit at -6 dB; the time display counts up; the orange playhead moves. |
| 3 | While it plays, press Space twice (pause, then play). | The tone stops and restarts with no click or thump, from where it stopped. |
| 4 | Press Return. | The tone stops with no click; the playhead goes back to where playback started (0:00.000). |
| 5 | Play, then click at different points in the empty part of the track lane or on the ruler. | Playback jumps there with no click (just a very short dip). |
| 6 | New project, import `02-left-then-right.wav`, play. | 2 s of a low tone on the **left** speaker only, then a higher tone on the **right** only; twice. |
| 7 | New project, import `03-loop-seam-480Hz.wav`, press L, play for 10 s. | One continuous tone: no gap, click or rhythm every second where it wraps. |
| 8 | Press L again (loop off), let it reach the end. | Playback stops by itself and returns to where it started. |
| 9 | New project, import `04-a440-mono-96k.wav`, play. | Same loudness on both speakers. The pitch is A4 (440 Hz): check with a tuner app; a wrong conversion would be clearly sharp or flat. |
| 10 | New project, import `05-full-scale-220Hz-44k.wav`, play. | Both red clip boxes at the top of the master meters light up and stay on; clicking a meter clears them. |
| 11 | New project, import `06-sine-1k-minus6dB.aiff`, play. | Same as check 2 (AIFF support). |
| 12 | With a file imported and Loop on, Save (Cmd+S), choose a name. Quit, reopen, Open (Cmd+O). | The same clip is there and Loop is on. |
| 13 | While playing, change the sample rate in File > Audio Output (e.g. 44.1 to 48 kHz). | The audio reloads by itself (a moment of silence); the pitch is unchanged and the clip stays at the same time. |
| 14 | Import one of your own songs, play it through, use loop, pause and seek. | No dropouts, no clicks, no distortion. |

## Phase 2 checks (multitrack and mixer)

Open `build/listening-tests/phase2/phase2-demo.dawproject` (Cmd+O). It is a small song
at 120 BPM in 4/4 (one bar = 2 s): Kick on every beat, Bass from bar 2 to bar 6,
Pad from bar 3 (a little to the left), Tone (muted), and Beeps on every bar line.

| # | Do | You should hear / see |
|---|----|------------------------|
| 15 | Look at the window, then press Space. | Five tracks with their waveforms; the mixer below with five strips and the master. Kick on every beat, a beep on every downbeat, the bass enters at bar 2 and the pad at bar 3. The ruler shows bars (top) and seconds (bottom): bar 2 is at 0:02, bar 3 at 0:04; the bar display follows the playhead. No clicks anywhere. |
| 16 | Press Return twice (back to the start), drag across the ruler from bar 3 to bar 5, press Space. | The region turns green and Loop switches on. Playback runs from the start to bar 5, then bars 3 and 4 repeat with no gap and no click; kick and beeps stay exactly in time across the loop point. Return goes back to the start. |
| 17 | While playing: M on Bass; then S on Kick; then S on Pad as well; then M on Pad; then release every S and M. | Bass disappears without a click. Then only the kick. Then kick and pad. Then only the kick again (mute wins over solo). Then everything except Tone. Track names of tracks you cannot hear turn grey. |
| 18 | Unmute Tone (a steady 440 Hz). While playing, move its fader up and down quickly, then pull it to the bottom; double-click it. | The level changes smoothly: no crackle, stepping or "zipper" sound, even with fast moves. At the bottom (-inf) it is silent; double-click returns to 0 dB. The strip's meter follows. |
| 19 | Solo Tone. Turn its pan knob fully left, then fully right, then double-click it (centre). | Hard left: only the left speaker, slightly louder (+3 dB) than either speaker at the centre. Hard right: the same on the right. Centre: equal on both, as loud as without the mixer. |
| 20 | Release solo. While playing, drag the Pad clip a bar to the right, then drag it back; then hold Cmd and drag it a little. | No clicks or dropouts while the clip moves under the playhead. Without Cmd it jumps from grid line to grid line; with Cmd it moves freely. |
| 21 | Drag the left edge of the Bass clip to bar 3, then drag its right edge far to the right. | The bass now starts at bar 3, with no click at the cut (a 5 ms fade). The right edge stops at the end of the file (bar 10) and goes no further. |
| 22 | Drag the Tone clip (unmuted) onto the Bass track so that it covers part of the bass; play that part. | Where they overlap you hear only the tone (the clip on top); before and after it the bass plays. No clicks where one takes over from the other. Dragging the tone clip away brings the whole bass back. |
| 23 | Drag `noise-44k.wav` and `tone-440-mono.wav` together from the Finder onto the empty area below the tracks. Then drag `beeps-on-bars.wav` alone onto the Kick track. | Two new tracks named after the files, both starting where you dropped them (on a grid line). The beeps clip lands on the Kick track; where it covers the kick clip only the beeps are heard (the newest clip is on top). Everything plays at the right pitch (the noise file is 44.1 kHz). |
| 24 | Press Cmd+Z repeatedly, then Shift+Cmd+Z repeatedly. Look at the Edit menu in between. | Each step undoes one action (a whole fader or clip drag is one step), in reverse order, and the sound follows; Edit shows "Undo Move Clip", "Undo Volume", etc. Redo brings everything back. |
| 25 | Double-click the tempo, type 90 and Enter; then double-click 4/4 and type 7/8. | The ruler and grid change (at 90 BPM a bar lasts 2.67 s); the clips stay where they were in seconds and sound the same. With 7/8 the bars are shorter and numbered accordingly. Cmd+Z undoes each change. |
| 26 | Cmd+Z back to the original song. Export (Cmd+E): Whole project, 48 kHz, WAV 24-bit. Open the file in QuickTime Player. | A progress window, then a message with the length (20 s, to the end of the pad) and the peak. The file sounds exactly like playback: same balance, pan and mutes. |
| 27 | Export again at 44.1 kHz, WAV 16-bit (Dither is ticked), then with Range: Loop region. | The 44.1 kHz file has the same pitch and length. The loop-region file lasts exactly 4 s (bars 3–4). |
| 28 | Raise the master fader to +6 dB and export; then put it back to 0 dB. | The message warns that the mix goes over 0 dBFS. |
| 29 | Save (Cmd+S). Make an edit, then quit (Cmd+Q). | Saving silences the question. After the edit, quitting asks whether to save: Cancel keeps the app open; Don't Save quits. Reopen the project: tracks, clips, mixer, loop region, tempo, snap and grid are as saved. The window title shows "(edited)" whenever there are unsaved changes. |
| 30 | Open `build/listening-tests/phase1-project.dawproject` (saved by Phase 1). | One track with the whole 1 kHz file at the start, Loop on; it plays and loops as in Phase 1. |
| 31 | Import 20 or more files (your own, or these several times) on separate tracks and play. | Smooth playback with no dropouts; the meters of every strip move. |

Anything else (a click, a crackle, wrong pitch, swapped channels, a meter that
disagrees with the file, a clip in the wrong place) is a bug: note the check number
and what you heard.
