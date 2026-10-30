# Listening tests

What the automated tests cannot check: the real audio device, and your ears.
Run these on macOS after any change to the engine, the device code or the UI.

## Setup

```bash
cmake --preset release && cmake --build --preset release
python3 tools/make_listening_test_files.py
open build/release/app/daw_app_artefacts/Release/DAW.app
```

The reference files are written to `build/listening-tests/`. Load a file by dragging
it onto the window or with File > Open Audio File (Cmd+O). Use moderate volume:
file 05 is at full scale.

Shortcuts: Space = play/pause, Return = stop, L = loop, Cmd+, = audio output.

## Checks

| # | Do | You should hear / see |
|---|----|------------------------|
| 1 | File > Audio Output: pick your interface, close the window. Quit and reopen the app. | The same device is selected again. |
| 2 | Load `01-sine-1k-minus6dB.wav`, press Space. | A steady 1 kHz tone, no crackles. Both meter bars sit at -6 dB; the time display counts up; the orange playhead moves. |
| 3 | While it plays, press Space twice (pause, then play). | The tone stops and restarts with no click or thump, from where it stopped (the time does not jump). |
| 4 | Press Return. | The tone stops with no click; time goes back to 0:00.000. |
| 5 | Play, then click at different points of the waveform. | Playback jumps there with no click (just a very short dip). |
| 6 | Load `02-left-then-right.wav`, play. | 2 s of a low tone on the **left** speaker only, then a higher tone on the **right** only; twice. |
| 7 | Load `03-loop-seam-480Hz.wav`, press L, play for 10 s. | One continuous tone: no gap, click or rhythm every second where the file wraps. |
| 8 | Press L again (loop off), let it reach the end. | Playback stops by itself and returns to the start. |
| 9 | Load `04-a440-mono-96k.wav`, play. | The details line ends with "played at 44.1/48 kHz". Same loudness on both speakers. The pitch is A4 (440 Hz): check with a tuner app; a wrong conversion would be clearly sharp or flat. |
| 10 | Load `05-full-scale-220Hz-44k.wav`, play. | Both red clip boxes at the top of the meter light up and stay on; clicking the meter clears them. |
| 11 | Load `06-sine-1k-minus6dB.aiff`, play. | Same as check 2 (AIFF support). |
| 12 | With a file loaded and Loop on, File > Save Project (Cmd+S), choose a name. Quit, reopen, File > Open Project (Shift+Cmd+O). | The same file is loaded and Loop is on. |
| 13 | While a file is loaded, change the sample rate in File > Audio Output (e.g. 44.1 to 48 kHz). | The file reloads by itself; the details line shows the new rate; the pitch is unchanged. Playback restarts from the beginning. |
| 14 | Load one of your own songs, play it through, use loop, pause and seek. | No dropouts, no clicks, no distortion. |

Anything else (a click, a crackle, wrong pitch, swapped channels, a meter that
disagrees with the file) is a bug: note the check number and what you heard.
