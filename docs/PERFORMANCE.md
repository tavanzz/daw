# Performance

How many tracks the engine mixes within the time of one audio callback (Phase 2), and
how many stream from disk and record at once (Phase 3).

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
