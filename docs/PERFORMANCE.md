# Performance

How many tracks the engine mixes within the time of one audio callback (Phase 2),
how many stream from disk and record at once (Phase 3), what effects cost and how
many tracks with effects fit (Phase 4), what the interface costs in each theme
and animation level (Phase 5), and what each instrument's voice costs (Phase 6).

```bash
cmake --preset release && cmake --build --preset release
build/release/tests/daw_bench
```

## Method

`tests/bench/MixBenchmark.cpp` times `Engine::process()` block by block, on one core:

- 48 kHz; blocks of 64, 128, 256 and 512 samples (budgets 1.33, 2.67, 5.33 and
  10.67 ms).
- Every track plays a stereo clip with its own volume and pan, looping; the clips
  come from 256 different one-second files (98 MB), each track reading its own
  part of its file, so most reads come from main memory rather than the caches.
- "Moving" faders: every 4 blocks the engine gets a new arrangement with different
  gains, so every gain ramp runs all the time (the worst case for the mixer).
- A track count fits when the 99th percentile of 600 timed blocks (after 100 warm-up
  blocks) stays within the share of the budget asked for. The largest count is found
  by doubling, then bisection to within 2 %.

The benchmark runs on a normal-priority thread while the machine does other things;
the real audio thread has real-time priority. The results are therefore on the safe
side, but they vary by a few percent between runs.

## Results

Apple M3 Pro (6 performance + 6 efficiency cores), macOS 15.6.1, Release build,
2026-10-30, with the code of Phase 2 step 11.

| Block | Budget | Faders | Within 100% | Within 50% | 100 tracks: median / p99 / max |
|------:|-------:|--------|------------:|-----------:|-------------------------------|
|    64 | 1.33 ms | still  |        6784 |       3584 | 0.015 / 0.017 / 0.018 ms |
|    64 | 1.33 ms | moving |        4864 |       2432 | 0.019 / 0.024 / 0.029 ms |
|   128 | 2.67 ms | still  |        9600 |       5504 | 0.017 / 0.020 / 0.021 ms |
|   128 | 2.67 ms | moving |        6144 |       3168 | 0.032 / 0.038 / 0.040 ms |
|   256 | 5.33 ms | still  |       12544 |       6784 | 0.027 / 0.036 / 0.038 ms |
|   256 | 5.33 ms | moving |        6912 |       3648 | 0.055 / 0.070 / 0.073 ms |
|   512 | 10.67 ms | still  |        4864 |       2400 | 0.095 / 0.101 / 0.103 ms |
|   512 | 10.67 ms | moving |        4864 |       2368 | 0.190 / 0.209 / 0.210 ms |

## What this means

- With 256-sample blocks the mixer alone handles about 12,500 tracks within the whole
  callback budget, and about 6,800 within half of it; with every fader moving at
  once, about 6,900 and 3,600. A project of 100 tracks takes under 1.5 % of the
  budget (0.07 ms of 5.33 ms, p99, faders moving).
- In Phase 2 the limit in practice was memory, not mixing: files were decoded whole
  into RAM (about 23 MB per stereo minute at 48 kHz). Since Phase 3 they stream from
  disk within a fixed memory budget (see below); effects and plugins will dominate
  the callback time once they come.
- Gain ramps cost about twice as much as steady gains (they are computed per sample);
  they only run for 20 ms after a change.
- 512-sample blocks come out lower than 256 in both runs made so far (per sample,
  a 512 block costs about 40 % more than a 256 one). Not investigated yet: worth a
  look before optimising the mixer, e.g. when multi-core mixing comes. Even so, 512
  blocks fit about 2,400 tracks in half the budget with every fader moving.

## Streaming and recording (Phase 3)

```bash
cmake --preset release && cmake --build --preset release
build/release/tests/daw_stream_bench
```

`tests/bench/StreamingBenchmark.cpp` writes 16 stereo 24-bit 48 kHz files of a minute,
then:

- Playback: N stereo tracks, each playing one of the files from its own point,
  stream through a `DiskStreamer` (512 MB budget, 3 s look-ahead; its rounds run
  between blocks, as its thread would) and are mixed, 20 s of audio as fast as
  possible. The real-time factor is audio time over the time taken by the
  read-ahead and the mixer together, on one core. The files were just written, so
  they come from the operating system's cache: this measures reading and
  converting, not the disk.
- Recording: N mono 24-bit 48 kHz tracks go through their FIFOs and the writer
  thread into WAV files, 30 s each, as fast as the writer takes them.

Apple M3 Pro, macOS 15.6.1, internal SSD, Release build, 2026-10-31, with the code of
Phase 3 step 15.

| Playback tracks | Real-time factor | Dropouts | Memory |
|----------------:|-----------------:|---------:|-------:|
|              16 |              219 |        0 | 138 MB |
|              64 |               51 |        0 | 512 MB |
|             128 |               25 |        0 | 512 MB |
|             256 |               10 |        0 | 512 MB |
|             320 |              7.6 |        0 | 512 MB |
|             384 |              5.9 |   10,612 | 512 MB |

| Recording tracks | Real-time factor |
|-----------------:|-----------------:|
|                8 |              506 |
|               32 |              130 |
|               64 |               58 |
|              128 |               24 |

What this means:

- Reading, converting and mixing 256 stereo tracks takes about a tenth of one core.
  The memory budget fills up early (blocks stay cached until it is full, so small
  projects end up wholly in memory) and stays at 512 MB however long the files are.
- The limit is the budget, not the processor: 3 s of look-ahead for every track
  must fit in it. Up to about 320 stereo tracks it does; at 384 the read-ahead cannot
  hold what is about to play and audio goes missing (counted, and shown as "the disk
  is too slow"). A larger budget, or a shorter look-ahead, would raise it.
- From a real disk the bandwidth counts too: a stereo 24-bit 48 kHz track needs
  288 KB/s, so 300 tracks need about 86 MB/s, far below what an SSD delivers; a
  spinning disk with seeks between files would be the limit.
- Recording 128 mono tracks at once takes about 1/24 of real time: the writer thread
  is far from being a bottleneck; the disk's write speed would be (128 tracks need
  18 MB/s at 24-bit 48 kHz).

## Effects (Phase 4)

```bash
cmake --preset release && cmake --build --preset release
build/release/tests/daw_fx_bench
```

`tests/bench/EffectBenchmark.cpp`:

- Each built-in effect, through the host (`fx::EffectInstance`, as the mixer runs it,
  with denormals flushed as on the audio thread), processes 10 s of stereo noise in
  blocks of 256 at 48 kHz: the time per sample, and the share of one core that
  takes in real time. Then 30 s of silence, of which the last 10 s are timed: what
  an effect costs once its tail has died away.
- Tracks playing noise, each through an equaliser (every band working) and a
  compressor, with two post-fader sends to a reverb bus and a delay bus shared by
  all tracks, and a limiter on the master: the most tracks whose 99th-percentile
  block time stays within the share of the budget asked for (the best of three
  runs per count, then doubling and bisection as `daw_bench` does).

Apple M3 Pro, macOS 15.6.1, Release build, 2026-11-03, with the code of Phase 4
step 13.

| Effect | Signal: ns/sample | Share of a core | Silence: ns/sample |
|--------|------------------:|----------------:|-------------------:|
| Equaliser, neutral (bands on at 0 dB) | 48.6 | 0.23 % | 2.9 |
| Equaliser, every band working | 53.2 | 0.26 % | 2.8 |
| Equaliser, a bell's frequency automated | 82.5 | 0.40 % | 6.9 |
| Compressor | 15.4 | 0.07 % | 2.4 |
| Limiter | 14.3 | 0.07 % | 14.1 |
| Delay (350 ms, 35 %) | 9.8 | 0.05 % | 2.3 |
| Reverb (2 s) | 142.2 | 0.68 % | 2.3 |

| Block | Budget | Tracks within 100% | Within 50% |
|------:|-------:|-------------------:|-----------:|
|   128 | 2.67 ms |               264 |        130 |
|   256 | 5.33 ms |               256 |        124 |
|   512 | 10.67 ms |              158 |         75 |

What this means:

- One core runs about 400 equalisers, 1,400 compressors or limiters, 2,000 delays
  or 150 reverbs at 48 kHz. The equaliser costs the same at 0 dB as working (its
  filters run so that they are ready when a band moves); per-sample automation of a
  frequency costs about 60 % more (the coefficients are worked out every sample).
- In silence, once their tails are gone, the equaliser, compressor, delay and
  reverb come to rest and skip their work (2 to 3 ns per sample: the host's own
  checks). The limiter always runs (14 ns). Without the resting rule, the
  equaliser's filter states kept flipping sign just above the smallest normal
  double (a limit cycle that flushing denormals creates) and it never rested:
  states below 1e-20 (1e-15 in the delay and reverb) are now set to zero, sample by
  sample, far below hearing and the same in exports.
- With the typical chain, about 250 tracks fit in a whole callback of 128 or 256
  samples and about 125 in half of it: effects, not mixing, now set the limit (the
  mixer alone fits thousands, see above). As with the mixer, 512-sample blocks come
  out lower per sample than 256; still 75 tracks fit in half the budget.
- One core only: the mixer and the effects run on the audio thread. Spreading
  tracks over cores is the way to more.

## The interface in each theme (Phase 5)

```bash
cmake --preset release && cmake --build --preset release
"build/release/tests/daw_ui_bench_artefacts/Release/DAW UI Bench.app/Contents/MacOS/DAW UI Bench"
```

`tests/bench/UiBenchmark.cpp` opens the desktop window, always on top, at 1600 x 1000
with the mixer open, over a large project: 64 stereo tracks playing in a loop, each
with an equaliser, a compressor, a send to one of 4 buses (two reverbs, two delays)
and automation on its volume, an equaliser gain and the compressor's threshold. The
audio runs on a real-time thread of its own, as a device's callback does: a
512-sample block every 10.67 ms at 48 kHz. For each theme and animation level, after
3 s to settle, 20 s are measured:

- frames per second, and the time to paint each one, from the window's first
  `paint()` to its last (macOS then composites the result: not included);
- CPU: the message thread's (all of the interface: timers, painting, the system's
  drawing on that thread) and the whole process's, as a percentage of one core;
- the audio callback: median, 99th percentile and maximum time, and how many
  callbacks overran their 10.67 ms.

Apple M3 Pro, macOS 15.6.1, a Retina display (the window paints at twice its size
in pixels), Release build, 2026-11-03, with the code of Phase 5 step 13.

| Theme | Animations | Frames/s | Paint median | p95 | max | UI CPU | Process CPU | Audio median | p99 | max | Missed |
|-------|------------|---------:|-------------:|----:|----:|-------:|------------:|-------------:|----:|----:|-------:|
| Neon Grid     | Full    | 60.3 | 3.40 ms | 3.89 ms | 4.35 ms | 52.5 % | 100.9 % | 3.917 ms | 4.097 ms | 4.137 ms | 0 |
| Neon Grid     | Reduced | 60.1 | 3.39 ms | 3.86 ms | 4.14 ms | 53.5 % | 100.8 % | 3.919 ms | 4.070 ms | 4.107 ms | 0 |
| Neon Grid     | Off     | 54.6 | 3.00 ms | 3.40 ms | 3.75 ms | 50.7 % |  97.9 % | 3.921 ms | 4.106 ms | 4.164 ms | 0 |
| Acid Terminal | Full    | 56.7 | 3.46 ms | 3.94 ms | 4.27 ms | 63.8 % | 110.3 % | 3.924 ms | 4.089 ms | 4.189 ms | 0 |
| Acid Terminal | Reduced | 54.9 | 3.47 ms | 3.92 ms | 4.24 ms | 62.1 % | 108.0 % | 3.922 ms | 4.056 ms | 4.149 ms | 0 |
| Acid Terminal | Off     | 53.5 | 3.47 ms | 3.91 ms | 4.12 ms | 60.6 % | 106.0 % | 3.918 ms | 4.078 ms | 4.169 ms | 0 |
| Synth Chrome  | Full    | 59.3 | 3.46 ms | 3.91 ms | 4.49 ms | 55.2 % | 105.2 % | 3.917 ms | 4.070 ms | 4.120 ms | 0 |
| Synth Chrome  | Reduced | 60.2 | 3.38 ms | 3.86 ms | 4.33 ms | 54.5 % | 102.5 % | 3.913 ms | 4.065 ms | 4.138 ms | 0 |
| Synth Chrome  | Off     | 54.5 | 3.02 ms | 3.43 ms | 3.74 ms | 51.5 % |  99.0 % | 3.916 ms | 4.087 ms | 4.145 ms | 0 |

What this means:

- The interface never reached the audio: the callback took the same time in every
  theme and level (median 3.91 to 3.92 ms, 99th percentile 4.06 to 4.11 ms, never more
  than 4.19 ms of the 10.67 ms budget) and none of about 17,000 callbacks was late. The
  audio thread shares nothing with the interface but the engine's lock-free queues
  and atomics; themes and animations live entirely on the message thread.
- A frame takes 3.0 to 3.5 ms to paint (95 % of frames under 4 ms, none over 4.5 ms)
  for a refresh every 16.7 ms. The animations add at most 0.4 ms a frame: the glows
  of Neon Grid and Synth Chrome (on the meters and the playhead, prerendered images
  put on screen) are that 0.4 ms; moving backgrounds, glitches and transitions do
  not show in the paint time, because the timeline keeps its clips and waveforms in
  an image (`TrackLanes`) and the backgrounds draw over or under it. Acid Terminal has
  no glow, so its three levels paint alike.
- The message thread is busy about half of the time (51 to 64 % of a core), whatever
  the level: the animations add at most 3 points. The rest is the interface's own
  60 Hz refresh of a big project while it plays: dozens of meters, 64 faders and pan
  knobs following their automation, the playhead and the time displays. A profile
  (`sample`) shows a third of that thread's time in our painting and about as much
  in macOS drawing the painted layers; the rest is the system's per-frame work. Acid
  Terminal costs about 10 points more than the others in every level: its LED
  meters fill a rectangle per segment. Meters now repaint only when what they show
  changes, so a silent or stopped project costs much less than this.
- The costs shown next to each effect in View > Animations follow these numbers:
  meters' glow medium (Neon Grid, Synth Chrome), LED afterglow low; moving
  backgrounds medium in Neon Grid and Synth Chrome (they repaint the whole timeline
  30 times a second, which costs more on bigger windows and screens), low for Acid
  Terminal's 6-pixel band; panel transitions low (they run for a fifth of a second);
  cursor glow, record pulse and glitch low.
- Not measured here: a window kept small or minimised (no frames at all: the
  animation clock stops), Windows and Linux (their renderers differ), and the
  functional refresh's own cost, which a later phase could cut (fewer repaints of
  faders that do not move, meters at a lower rate when there are many).

## Instruments (Phase 6)

```bash
cmake --preset release && cmake --build --preset release
build/release/tests/daw_instrument_bench          # or a name: daw_instrument_bench Drum
```

`tests/bench/InstrumentBenchmark.cpp`:

- Each built-in instrument, through the host (`fx::EffectInstance`, as the mixer
  runs it, with denormals flushed as on the audio thread), holds 0, 1, 8 and 16 notes
  on different keys for 10 s in blocks of 256 at 48 kHz; the first second (the
  attacks) is not timed. A voice costs the time with 16 notes less the time with
  none, over 16: in nanoseconds per sample, and how many voices one core plays in
  real time. The samplers play a 12 s stereo sample; the drum sampler's pads decay
  over 10 s here, so that every voice sounds through the measurement.
- Tracks, each an instrument holding 16 notes, played by the engine: the most tracks
  whose 99th-percentile block time stays within the share of the budget asked for
  (the best of three runs per count, doubling then bisection, as `daw_bench`
  does), shown as voices, and the median block time at that count.

Apple M3 Pro, macOS 15.6.1, Release build, 2026-11-04, with the code of Phase 6
step 15.

| Instrument | Idle: ns/sample | 1 voice | 8 voices | 16 voices | Per voice: ns/sample | Voices per core |
|------------|----------------:|--------:|---------:|----------:|---------------------:|----------------:|
| Synth, default (two saws, low-pass 24) | 5.6 | 24.2 | 179.7 | 340.7 | 20.9 | 995 |
| Synth, busy (two pulses, noise, LFO on pitch and cutoff) | 5.1 | 38.9 | 246.2 | 514.8 | 31.9 | 654 |
| Sampler (stereo, looped with a crossfade) | 7.6 | 21.2 | 141.4 | 249.1 | 15.1 | 1380 |
| Drum sampler (stereo one-shots) | 10.7 | 23.6 | 105.8 | 179.1 | 10.5 | 1979 |

| Instrument | Block | Budget | Voices within 100% | Median block there | Within 50% |
|------------|------:|-------:|-------------------:|-------------------:|-----------:|
| Synth, default | 128 | 2.67 ms | 336 | 0.83 ms | 224 |
| Synth, default | 256 | 5.33 ms | 448 | 2.21 ms | 160 |
| Synth, default | 512 | 10.67 ms | 944 | 9.35 ms | 528 |
| Synth, busy | 256 | 5.33 ms | 720 | 5.12 ms | 352 |
| Sampler | 256 | 5.33 ms | 1472 | 5.12 ms | 736 |
| Drum sampler | 256 | 5.33 ms | 1760 | 5.05 ms | 912 |

What this means:

- A synth voice (two band-limited wavetable oscillators read with cubic
  interpolation, a 24 dB filter whose coefficients move every sample, two
  envelopes) costs about 21 ns per sample: one core plays about 1,000 of them at
  48 kHz, 650 with pulses, noise and the LFO on pitch and cutoff. A sampler voice
  (8-point sinc interpolation, stereo, a crossfaded loop) costs 15 ns, a drum voice
  (the same reading, no loop) 10.5 ns. An instrument holding no notes costs 5 to
  11 ns per sample (its control grid and the host's ramps).
- Where the machine was quiet, the engine adds nothing measurable to the voices:
  with 1,472 sampler voices the median block takes 5.1 ms, what the voices cost
  alone (1,472 x 15.1 ns x 256 = 5.7 ms, measured separately), and the 99th
  percentile stays within the 5.33 ms. So one callback of 256 samples holds about
  1,400 sampler, 1,700 drum, 700 busy or 900 default synth voices, half of that
  in half the budget.
- The default synth's rows at 128 and 256 samples were measured while other
  applications were busy (a music player, a browser): blocks three times slower
  than the median came often enough to set the 99th percentile, so the budget was
  "full" with the median at a third of it. Those rows understate what fits; they
  are left as measured, as a reminder that a callback shares the machine. The same
  noise makes the "within 50 %" column uneven.
- With the instruments' own limit of 32 voices each, a song of 16 synth tracks all
  holding 16 notes (256 voices) takes about a quarter of a 256-sample callback on one
  core, before effects. Like the mixer and the effects, instruments run on the
  audio thread only.

## Plugins (Phase 7)

```bash
cmake --preset release && cmake --build --preset release
build/release/tests/daw_plugin_bench
```

`tests/bench/PluginBenchmark.cpp` measures what hosting a plugin costs on top of the
plugin itself, with the VST3 test plugins (`tests/plugins`), which cost next to
nothing: a gain, and a synth whose output shows the notes it holds. For each block
size, 10 s of stereo noise at 48 kHz with denormals flushed, the best of five runs:

- JUCE alone: `AudioPluginInstance::processBlock()`, what any JUCE host pays (VST3's
  buffers, events and parameter queues).
- Adapter: `PluginEffect::process()` (MIDI conversion, play head, timing, the
  real-time checks' scope).
- Host: `EffectInstance::process()` over a `PluginSlot`, as the mixer calls it (dry
  copy for the fades, NaN guard, parameter bookkeeping).
- Host, automated: the gain moving along a ramp, so every block is cut into calls of
  32 samples with a parameter change before each.
- Synth, 8 events: the instrument through the host with four notes struck and
  released in every block.

Apple M3 Pro, macOS 15.6.1, Release build, 2026-11-05, with the code of Phase 7
step 11. Microseconds per block:

| Block | Budget | JUCE alone | Adapter | Host | Host, automated | Synth, 8 events |
|------:|-------:|-----------:|--------:|-----:|----------------:|----------------:|
| 64 | 1333 us | 0.19 | 0.25 | 0.37 | 0.58 | 1.27 |
| 128 | 2667 us | 0.11 | 0.16 | 0.35 | 0.91 | 1.22 |
| 256 | 5333 us | 0.14 | 0.17 | 0.50 | 1.69 | 1.33 |
| 512 | 10667 us | 0.21 | 0.26 | 0.94 | 3.40 | 2.90 |

What this means:

- The adapter adds 0.03 to 0.06 microseconds to a block, the host around it (the same
  as for a built-in effect) under 1 microsecond: hosting one plugin costs about a
  hundredth of a percent of a 256-sample callback. A hundred plugins cost the host
  about 50 microseconds per block, 1 % of the budget; what the plugins themselves
  cost is theirs (a real equaliser or synth takes tens to hundreds of times more).
- Automation that moves costs about 0.1 microseconds per 32-sample call (3.4
  microseconds per 512-sample block): the price of steps landing on their sample and
  of results that do not depend on the blocks. A plugin with a heavy fixed cost per
  call (some run their own buffering whatever the call's length) pays that cost
  every 32 samples while its automation moves, and only then.
- MIDI costs about 0.1 microseconds per event (conversion to MIDI 1.0 and JUCE's
  conversion to VST3 events).
- The time each call takes is also measured in the app, per plugin
  (`PluginEffect::takeLoad()`), against the block's duration: a plugin whose average
  exceeds half the budget, or that overruns a block, is flagged.
