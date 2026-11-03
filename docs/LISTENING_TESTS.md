# Listening tests

What the automated tests cannot check: the real audio device, the microphone, the
mouse, and your ears. Run these on macOS after any change to the engine, the device
code or the UI.

## Setup

```bash
cmake --preset release && cmake --build --preset release
python3 tools/make_listening_test_files.py
open build/release/app/daw_app_artefacts/Release/DAW.app
```

The reference files are written to `build/listening-tests/` (Phase 2 material in its
`phase2/` folder, Phase 3 in `phase3/`). Use moderate volume: file 05 is at full scale.
For the streaming check (47), add `--long`: it also writes an hour-long file of about
700 MB.

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
| Audio settings (devices, inputs, recording, latency) | Cmd+, |
| Record / stop recording | R |
| Discard the recording in progress (asks first) | Cmd+. |
| Metronome / count-in / punch | K / Shift+K / P |

## Phase 1 checks (single file, still valid)

Start each one from File > New Project (Cmd+N) and add the file with File > Import
Audio (Cmd+I) or by dragging it onto the empty track area; it lands at the playhead.

| # | Do | You should hear / see |
|---|----|------------------------|
| 1 | File > Audio Settings: pick your interface, close the window. Quit and reopen the app. | The same device is selected again. |
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
| 13 | While playing, change the sample rate in File > Audio Settings (e.g. 44.1 to 48 kHz). | The audio reloads by itself (a moment of silence); the pitch is unchanged and the clip stays at the same time. |
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

## Phase 3 checks (recording)

What you need: headphones (monitoring through speakers feeds back into a microphone),
a microphone or an instrument on input 1 of your interface (or the Mac's built-in
microphone), and for checks 38 and 39 a short cable from output 1 to input 2 of the
interface. Turn your speakers down whenever a cable connects an output to an input.

Before starting:

- Launch the app with `open` (as in Setup), not from a terminal: macOS then asks for
  the microphone on behalf of DAW itself.
- Copy `build/listening-tests/phase3/` somewhere outside Documents, Desktop and
  Downloads (for example `~/Music/daw-phase3/`), so that recordings can be written
  without another permission question, and open `phase3-recording.dawproject` from
  there (Cmd+O). It has four tracks at 120 BPM in 4/4 (one bar = 2 s): Guide (a click
  on every beat, louder on each bar), Mic (input 1, monitoring Auto), Loopback
  (input 2, monitoring Off) and Stereo (inputs 1+2, Auto). The loop region (bars 3 and
  4) is set but off.
- Recordings land in the project's `Audio/` folder.

| # | Do | You should hear / see |
|---|----|------------------------|
| 32 | Click R on the Mic track. | The first time ever: macOS asks whether DAW may use the microphone, with the sentence "DAW records from your microphone and audio interface inputs." Allow. R turns red; the small meter beside the Mic header moves when you speak. Quit and reopen the app: no question this time. |
| 33 | In System Settings > Privacy & Security > Microphone, turn DAW off; quit and reopen the app, open the project, click R on Mic. Then turn DAW back on and restart the app. | R does not arm; a message says microphone access is off and where to allow it. Press R (record): the same message. After turning it back on, arming works again. |
| 34 | Cmd+, (Audio Settings). | Your interface as input and output device, its input channels listed; below, Recording: file format (24-bit selected), the latency the device reports in samples and ms, the latency offset (0), the total compensation, and "Measure with a Cable...". Close it. |
| 35 | With headphones on and Mic armed (monitoring Auto): speak while stopped; press Space; press Return. Then click Auto until it says On and play; then Off. Finally move the Mic fader and pan knob, and press M. | Stopped and armed: you hear yourself. Playing: you hear the Guide clicks and not yourself. Stopped again: yourself again, with no click or jump in either switch. On: you hear yourself even while playing. Off: never. The fader and pan change your voice's level and side; M silences it. |
| 36 | Monitoring back to Auto. Playhead at the start (Return twice), press R, count aloud from 1 to 8 on the beats for about 10 s, press R again. Then play it back. | While recording: Rec is red and the status line says "Recording" with the disk time left; a red clip grows on the Mic track with your waveform. After stopping, a moment later, a clip "Mic 1" appears from 0:00 to where you stopped. Playing back, your counting is on the Guide's beats as you heard them while recording. In the Finder, `Audio/Mic 1.wav` exists. |
| 37 | Cmd+Z, then Shift+Cmd+Z. Then save (Cmd+S) and open the `.dawproject` in a text editor. | Undo removes the clip in one step ("Undo Record" in the Edit menu); redo brings it back. The file names the clip as `"audioFile": "Audio/Mic 1.wav"`. |
| 38 | Connect the cable from output 1 to input 2. Cmd+, > "Measure with a Cable...": Output 1 (left), Input 2, Measure. Measure again twice. Then "Use This Offset". | A few short bursts of noise (about 3 s in all). The result: the round trip in samples and ms, what the device reports, and the offset (usually a few to a few dozen samples). The three measurements agree to within one sample. After "Use This Offset" the Audio Settings window shows the new offset and total. Without the cable (unplug it and measure): a message saying the signal did not come back. |
| 39 | Keep the cable. Arm Loopback only (its monitoring is Off: no feedback), playhead at the start, record 8 bars, stop. Solo Guide and Loopback and play; then zoom in as far as you can (Cmd+= repeatedly) on one click of each. | One click per beat, not a double or "flammed" click. Zoomed in, each recorded click starts exactly where the Guide's click starts. (Set the offset to 0 and record again: the recorded clicks now come late by the offset you measured.) |
| 40 | Metronome: press K and play; right-click Click and choose -18 dB. Count-in: Shift+K, right-click Count and choose 2 bars; playhead at bar 3, arm Mic, press R, sing a few bars, stop. | K: a click on every beat, higher on the first beat of each bar, exactly with the Guide's clicks; quieter at -18 dB. Count-in: two bars of clicks while the timeline is silent and the status says "Count-in...", then recording from bar 3 with the Guide. The new clip starts exactly at bar 3. Dragging its left edge to the left reveals the count-in you sang over (the file keeps it). |
| 41 | Punch: Mic armed, Auto. Press P (Punch on: the loop region, bars 3 and 4, is the punch range). Playhead at bar 1, press R; sing something different from your earlier take from bar 1 to bar 6; stop at bar 6. Play from bar 1. | While recording, you hear your earlier Mic take up to bar 3, then yourself live until bar 5, then the earlier take again (monitoring switches exactly at the punch points, without clicks). The new clip covers exactly bars 3 and 4; playing back, the old take is heard before and after it, the new one inside, with no click at either edge. Dragging the new clip's edges outwards reveals what you sang before bar 3 and after bar 5. Cmd+Z: the old take alone again. |
| 42 | Arm Mic and Stereo, record a few seconds, stop. | Two new clips, one on each track, from the same position; one undo step removes both. The Stereo clip has two channels (inputs 1 and 2). |
| 43 | Press R and, while recording, Cmd+.; choose "Keep Recording". Then Cmd+. again and choose "Discard". Also press Escape during a recording. | The question "Discard the recording and delete its files?". Keep Recording: recording continues. Discard: recording stops, no clip appears, and no new file is left in `Audio/`. Escape does nothing to the recording. |
| 44 | Cmd+N (New Project), Cmd+T, arm the new track, press R. | A save dialog titled "Save the project before recording". After saving, recording starts; Cancel leaves nothing recorded. |
| 45 | Long recording: arm Mic, record for at least 30 minutes (a radio, or the room). Keep Activity Monitor open on DAW. Stop, then play back from several places (click the ruler). | During recording the status keeps showing the disk time left; no "disk too slow" warning. The clip lasts the whole time. Playback starts within a moment wherever you click, with no gaps. DAW's memory stays well under 1 GB, during recording and playback. |
| 46 | Crash: start a recording on Mic, speak for about 20 s, then in Terminal run `kill -9 $(pgrep -x DAW)`. Open the app and the project again. | A question: "1 recording was made in this project but not saved in it... Recover them as clips, or delete their files?" Recover: a clip with your 20 s (at most the last moment missing) where you recorded it; Cmd+Z removes it. Do it again and choose Delete: the file is gone from `Audio/`. |
| 47 | Streaming: `python3 tools/make_listening_test_files.py --long`, then import `phase3/long-60min.wav` (Cmd+I; reading it through for its waveform takes a little while). Play, click the ruler at many places (the tone changes every minute: 200 Hz, 220 Hz, ...), loop a region near the end. Watch DAW in Activity Monitor. | Playback starts within a moment wherever you click, the pitch matches the minute, loops are seamless, no "disk too slow" warning. DAW's memory stays near 600 MB at most, although the file decoded into memory, as Phase 2 did it, would take about 1.4 GB. |
| 48 | In Audio Settings, change the device's sample rate during a recording. Then record at 44.1 kHz and switch back to 48 kHz. | Changing the rate stops the recording, keeps what was recorded and says why. A take recorded at 44.1 kHz plays at the right pitch and in time at 48 kHz (`Audio/` holds it at 44.1 kHz). |
| 49 | Optional, disk full: in Terminal `hdiutil create -size 25m -fs HFS+ -volname Small /tmp/small.dmg && hdiutil attach /tmp/small.dmg`, save a project in `/Volumes/Small`, arm Mic and record. Afterwards `hdiutil detach /Volumes/Small`. | The status shows about 2 min of disk left, counting down. When about 10 s are left, recording stops on its own with "The disk is almost full"; the clip keeps everything up to there. |

Anything else (a click, a crackle, wrong pitch, swapped channels, a meter that
disagrees with the file, a clip in the wrong place) is a bug: note the check number
and what you heard.
