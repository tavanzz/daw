#!/usr/bin/env python3
"""Writes the reference files used by docs/LISTENING_TESTS.md.

Usage: python3 tools/make_listening_test_files.py [output folder] [--long]
(default: build/listening-tests). --long also writes an hour-long stereo file (about
700 MB) for the streaming checks. Needs only the Python standard library.
"""

import argparse
import json
import math
import random
import struct
import wave
from pathlib import Path

# Project files store times in ticks: 282,240,000 per second (see model/TimelineTime.h).
TICKS_PER_SECOND = 282_240_000


def sine(frequency, amplitude, rate, seconds):
    return [amplitude * math.sin(2 * math.pi * frequency * n / rate) for n in range(int(rate * seconds))]


def to_int16(value):
    return max(-32768, min(32767, int(round(value * 32767))))


def write_wav(path, rate, channels):
    """channels: list of equally long lists of floats in [-1, 1]."""
    with wave.open(str(path), "wb") as file:
        file.setnchannels(len(channels))
        file.setsampwidth(2)
        file.setframerate(rate)
        frames = bytearray()
        for samples in zip(*channels):
            for value in samples:
                frames += struct.pack("<h", to_int16(value))
        file.writeframes(bytes(frames))


def faded(samples, rate, seconds=0.01):
    """Short linear fades at both ends, so that the file itself never clicks."""
    length = min(len(samples) // 2, int(rate * seconds))
    for n in range(length):
        gain = n / length
        samples[n] *= gain
        samples[-1 - n] *= gain
    return samples


def kick(rate, seconds, bpm, amplitude):
    """A synthetic kick drum on every beat: a decaying sine sweeping from 150 down to 50 Hz."""
    out = [0.0] * int(rate * seconds)
    beat = 60.0 / bpm
    length = int(rate * 0.25)
    for start in range(0, len(out), int(rate * beat)):
        phase = 0.0
        for n in range(min(length, len(out) - start)):
            t = n / rate
            frequency = 50 + 100 * math.exp(-t * 40)
            phase += 2 * math.pi * frequency / rate
            out[start + n] += amplitude * math.exp(-t * 12) * math.sin(phase)
    return out


def beeps(rate, seconds, every, frequency, amplitude, length=0.05):
    """A short beep exactly at the start of every period (a bar), to hear the grid."""
    out = [0.0] * int(rate * seconds)
    count = int(rate * length)
    for start in range(0, len(out), int(round(rate * every))):
        for n in range(min(count, len(out) - start)):
            envelope = min(1.0, n / 48, (count - n) / 48)
            out[start + n] = amplitude * envelope * math.sin(2 * math.pi * frequency * n / rate)
    return out


def ticks(seconds):
    return int(round(seconds * TICKS_PER_SECOND))


def track(track_id, name, clips, volume=1.0, pan=0.0, mute=False, solo=False):
    return {"id": track_id, "name": name, "volume": volume, "pan": pan, "mute": mute,
            "solo": solo, "clips": clips}


def clip(clip_id, audio_file, position, offset=0.0, length=None):
    result = {"id": clip_id, "audioFile": audio_file, "position": ticks(position),
              "offset": ticks(offset)}
    if length is not None:
        result["length"] = ticks(length)
    return result


def write_phase2_files(out):
    """Phase 2: material for a small multitrack song at 120 BPM in 4/4 (a bar is 2 s)."""
    folder = out / "phase2"
    folder.mkdir(parents=True, exist_ok=True)
    rate = 48000
    seconds = 16  # 8 bars
    db = lambda value: 10 ** (value / 20)

    drums = kick(rate, seconds, 120, 0.45)
    write_wav(folder / "kick-120bpm.wav", rate, [drums, drums])

    write_wav(folder / "bass-110Hz.wav", rate, [faded(sine(110, db(-12), rate, seconds), rate)])

    left = [a + b + c for a, b, c in zip(sine(220, db(-24), rate, seconds),
                                         sine(277.18, db(-24), rate, seconds),
                                         sine(329.63, db(-24), rate, seconds))]
    right = [a + b + c for a, b, c in zip(sine(220.5, db(-24), rate, seconds),
                                          sine(277.6, db(-24), rate, seconds),
                                          sine(330.2, db(-24), rate, seconds))]
    write_wav(folder / "pad-chord.wav", rate, [faded(left, rate, 0.2), faded(right, rate, 0.2)])

    write_wav(folder / "tone-440-mono.wav", rate, [faded(sine(440, db(-12), rate, seconds), rate)])

    bar_beeps = beeps(rate, seconds, 2.0, 2000, db(-10))
    write_wav(folder / "beeps-on-bars.wav", rate, [bar_beeps, bar_beeps])

    random.seed(7)
    noise = [random.uniform(-1, 1) * db(-18) for _ in range(44100 * 4)]
    write_wav(folder / "noise-44k.wav", 44100, [faded(noise, 44100), faded(list(noise), 44100)])

    project = {
        "format": "daw-project",
        "formatVersion": 2,
        "project": {
            "tempo": 120.0,
            "timeSignature": {"numerator": 4, "denominator": 4},
            "loop": {"enabled": False, "start": ticks(4), "end": ticks(8)},
            "snap": {"enabled": True, "grid": "auto"},
            "master": {"volume": 1.0},
            "tracks": [
                track(1, "Kick", [clip(2, "kick-120bpm.wav", 0)]),
                track(3, "Bass", [clip(4, "bass-110Hz.wav", 2, 0, 8)]),
                track(5, "Pad", [clip(6, "pad-chord.wav", 4)], volume=db(-3), pan=-0.3),
                track(7, "Tone", [clip(8, "tone-440-mono.wav", 0)], volume=db(-6), mute=True),
                track(9, "Beeps", [clip(10, "beeps-on-bars.wav", 0)], volume=db(-6)),
            ],
        },
    }
    (folder / "phase2-demo.dawproject").write_text(json.dumps(project, indent=2) + "\n")

    # A project saved by Phase 1 (format version 1), for the migration check.
    phase1 = {
        "format": "daw-project",
        "formatVersion": 1,
        "project": {"loopEnabled": True,
                    "tracks": [{"name": "Track 1",
                                "clips": [{"audioFile": "01-sine-1k-minus6dB.wav"}]}]},
    }
    (out / "phase1-project.dawproject").write_text(json.dumps(phase1, indent=2) + "\n")


def clicks(rate, seconds, bpm, beats_per_bar):
    """A sharp click on every beat, louder on the first of each bar. Each click starts
    at full height on the beat's exact sample, so recordings of it can be lined up
    with it to the sample."""
    out = [0.0] * int(rate * seconds)
    samples_per_beat = rate * 60 / bpm
    length = int(rate * 0.004)
    beat = 0
    while True:
        start = int(round(beat * samples_per_beat))
        if start >= len(out):
            break
        amplitude = 0.8 if beat % beats_per_bar == 0 else 0.4
        for n in range(min(length, len(out) - start)):
            out[start + n] = amplitude * math.exp(-n / (rate * 0.0008)) * (1 if (n // 6) % 2 == 0 else -1)
        beat += 1
    return out


def write_phase3_files(out, long_file):
    """Phase 3: a project to record into, a click track to record back through a
    cable, and optionally an hour-long file for streaming."""
    folder = out / "phase3"
    folder.mkdir(parents=True, exist_ok=True)
    rate = 48000

    # 32 bars at 120 BPM in 4/4: 64 s of clicks.
    write_wav(folder / "click-120bpm.wav", rate, [clicks(rate, 64, 120, 4)])

    def input_track(track_id, name, channel, stereo, monitoring):
        result = track(track_id, name, [])
        result.update({"input": {"channel": channel, "stereo": stereo},
                       "monitoring": monitoring, "armed": False})
        return result

    guide = track(1, "Guide", [clip(10, "click-120bpm.wav", 0.0)])
    guide.update({"input": {"channel": 0, "stereo": False}, "monitoring": "off", "armed": False})
    project = {
        "format": "daw-project",
        "formatVersion": 3,
        "project": {
            "tempo": 120.0,
            "timeSignature": {"numerator": 4, "denominator": 4},
            # Bars 3 and 4 (from 4 s to 8 s): the punch range.
            "loop": {"enabled": False, "start": ticks(4.0), "end": ticks(8.0)},
            "snap": {"enabled": True, "grid": "auto"},
            "master": {"volume": 1.0},
            "metronome": {"enabled": False, "volume": 0.5},
            "countIn": {"enabled": False, "bars": 1},
            "punch": {"enabled": False},
            "recording": {"bitsPerSample": 24},
            "tracks": [
                guide,
                input_track(2, "Mic", 1, False, "auto"),
                input_track(3, "Loopback", 2, False, "off"),
                input_track(4, "Stereo", 1, True, "auto"),
            ],
        },
    }
    (folder / "phase3-recording.dawproject").write_text(json.dumps(project, indent=2) + "\n")

    if long_file:
        # An hour of stereo 16-bit: a different steady tone every minute (200 Hz, then
        # 220 Hz, ...), so that where playback is can be heard.
        with wave.open(str(folder / "long-60min.wav"), "wb") as file:
            file.setnchannels(2)
            file.setsampwidth(2)
            file.setframerate(rate)
            for minute in range(60):
                frequency = 200 + 20 * minute
                second = bytearray()
                for n in range(rate):
                    value = to_int16(0.3 * math.sin(2 * math.pi * frequency * n / rate))
                    second += struct.pack("<hh", value, value)
                file.writeframes(bytes(second) * 60)


def snare(rate, seconds, bpm, amplitude, seed=4):
    """Noise bursts on beats 2 and 4 of every bar, decaying in about 0.12 s."""
    generator = random.Random(seed)
    out = [0.0] * int(rate * seconds)
    beat = 60.0 / bpm
    length = int(rate * 0.25)
    index = 0
    while True:
        start = int(round((index * 2 + 1) * beat * rate))
        if start >= len(out):
            break
        for n in range(min(length, len(out) - start)):
            envelope = math.exp(-n / (rate * 0.04))
            out[start + n] += amplitude * envelope * (generator.random() * 2 - 1)
        index += 1
    return out


def notes(rate, seconds, bpm, frequencies, amplitude, harmonics, decay):
    """One note per beat, cycling through frequencies: a few harmonics, decaying."""
    out = [0.0] * int(rate * seconds)
    beat = int(round(rate * 60.0 / bpm))
    for index, start in enumerate(range(0, len(out), beat)):
        frequency = frequencies[index % len(frequencies)]
        for n in range(min(beat, len(out) - start)):
            t = n / rate
            envelope = math.exp(-t / decay) * min(1.0, n / 96) * min(1.0, (beat - n) / 96)
            value = sum(math.sin(2 * math.pi * frequency * h * t) / h
                        for h in range(1, harmonics + 1))
            out[start + n] += amplitude * envelope * value
    return out


def effect(effect_id, type_name, parameters, bypass=False):
    return {"id": effect_id, "type": type_name, "bypass": bypass, "parameters": parameters}


def send(send_id, bus_id, level, pre_fader=False):
    return {"id": send_id, "bus": bus_id, "level": level, "preFader": pre_fader}


def automation(lanes, mode="read"):
    """lanes: (target, [(seconds, normalised value, curve), ...]) pairs."""
    return {"mode": mode, "lanes": [
        {"target": target,
         "points": [{"position": ticks(at), "value": value, "curve": curve}
                    for at, value, curve in points]}
        for target, points in lanes]}


def mixer_track(track_id, name, clips, effects=(), sends=(), lanes=(), volume=1.0, mute=False):
    result = track(track_id, name, clips, volume, 0.0, mute)
    result.update({"input": {"channel": 1, "stereo": False}, "monitoring": "auto",
                   "armed": False, "effects": list(effects), "sends": list(sends),
                   "automation": automation(list(lanes))})
    return result


def bus(bus_id, name, effects, lanes=()):
    return {"id": bus_id, "name": name, "volume": 1.0, "pan": 0.0, "mute": False,
            "effects": effects, "automation": automation(list(lanes))}


def project_v4(master_effects, buses, tracks):
    return {
        "format": "daw-project",
        "formatVersion": 4,
        "project": {
            "tempo": 120.0,
            "timeSignature": {"numerator": 4, "denominator": 4},
            "loop": {"enabled": False, "start": ticks(8.0), "end": ticks(16.0)},
            "snap": {"enabled": True, "grid": "auto"},
            "master": {"volume": 1.0, "effects": master_effects, "automation": automation([])},
            "metronome": {"enabled": False, "volume": 0.5},
            "countIn": {"enabled": False, "bars": 1},
            "punch": {"enabled": False},
            "recording": {"bitsPerSample": 24},
            "buses": buses,
            "tracks": tracks,
        },
    }


def write_phase4_files(out):
    """Phase 4: a song at 120 BPM in 4/4 (a bar is 2 s, 16 bars) to put effects on, a
    steady tone and noise to hear filters, and a project to check delay compensation."""
    folder = out / "phase4"
    folder.mkdir(parents=True, exist_ok=True)
    rate = 48000
    seconds = 32

    drums = [k + s for k, s in zip(kick(rate, seconds, 120, 0.6), snare(rate, seconds, 120, 0.3))]
    write_wav(folder / "drums.wav", rate, [drums, drums])

    bass = notes(rate, seconds, 120, [55.0, 55.0, 43.65, 43.65, 65.41, 65.41, 49.0, 49.0],
                 0.25, 8, 0.35)
    write_wav(folder / "bass.wav", rate, [bass])

    # A sustained chord (A minor) with plenty of harmonics.
    pad = [0.0] * (rate * seconds)
    for frequency in (220.0, 261.63, 329.63):
        for harmonic in range(1, 7):
            amplitude = 0.05 / harmonic
            step = 2 * math.pi * frequency * harmonic / rate
            for n in range(len(pad)):
                pad[n] += amplitude * math.sin(step * n)
    pad = faded(pad, rate, 0.5)
    write_wav(folder / "pad.wav", rate, [pad, [v * 0.95 for v in pad]])

    # A short pluck on beats 1 and 3: what the delay repeats.
    pluck = [0.0] * (rate * seconds)
    for start in range(0, len(pluck), rate):
        for n in range(min(int(rate * 0.4), len(pluck) - start)):
            t = n / rate
            partials = math.sin(2 * math.pi * 880 * t) + 0.4 * math.sin(2 * math.pi * 1320 * t)
            pluck[start + n] = 0.5 * math.exp(-t / 0.05) * partials
    write_wav(folder / "pluck.wav", rate, [pluck])

    tone = sine(440, 10 ** (-12 / 20), rate, seconds)
    write_wav(folder / "tone-440.wav", rate, [faded(tone, rate)])

    # Pink-ish noise (Paul Kellet's filter), about -21 dBFS RMS: where an EQ cuts or
    # boosts is easy to hear.
    generator = random.Random(7)
    b = [0.0] * 7
    noise = []
    for _ in range(rate * 16):
        white = generator.random() * 2 - 1
        b[0] = 0.99886 * b[0] + white * 0.0555179
        b[1] = 0.99332 * b[1] + white * 0.0750759
        b[2] = 0.96900 * b[2] + white * 0.1538520
        b[3] = 0.86650 * b[3] + white * 0.3104856
        b[4] = 0.55000 * b[4] + white * 0.5329522
        b[5] = -0.7616 * b[5] - white * 0.0168980
        noise.append((sum(b) + white * 0.5362) * 0.05)
        b[6] = white * 0.115926
    write_wav(folder / "noise-pink.wav", rate,
              [faded(noise, rate), faded(list(reversed(noise)), rate)])

    click_track = clicks(rate, seconds, 120, 4)
    write_wav(folder / "click-120bpm.wav", rate, [click_track])
    write_wav(folder / "click-120bpm-inverted.wav", rate, [[-v for v in click_track]])

    unity = 60 / 66  # 0 dB on the fader, as automation stores it (-60 to +6 dB)
    reverb_bus = bus(200, "Reverb", [effect(201, "reverb", {"decay": 2.5, "mix": 100.0,
                                                             "preDelay": 20.0})],
                     # Swells to +6 dB over the last 4 bars, so the tail is easy to hear.
                     [("volume", [(24.0, unity, 0.0), (32.0, 1.0, 0.0)])])
    delay_bus = bus(210, "Delay", [effect(211, "delay", {"sync": 1.0, "note": 10.0,  # 1/8D
                                                          "feedback": 40.0, "mix": 100.0})])
    compressor = {"threshold": -20.0, "ratio": 4.0, "attack": 10.0, "release": 100.0}
    band_limits = {"highPassOn": 1.0, "highPassFrequency": 40.0,
                   "lowPassOn": 1.0, "lowPassFrequency": 2000.0}
    tracks = [
        mixer_track(1, "Drums", [clip(2, "drums.wav", 0.0)],
                    effects=[effect(3, "equaliser", {}), effect(4, "compressor", compressor)],
                    sends=[send(5, 200, 0.4)], volume=0.8),
        mixer_track(10, "Bass", [clip(11, "bass.wav", 0.0)],
                    effects=[effect(12, "equaliser", band_limits)]),
        # A curved fade in over 4 bars; centre, then left, right and back to the
        # centre over bars 5 to 8.
        mixer_track(20, "Pad", [clip(21, "pad.wav", 0.0)], sends=[send(22, 200, 0.6)],
                    lanes=[("volume", [(0.0, 0.0, -0.6), (8.0, unity, 0.0)]),
                           ("pan", [(8.0, 0.5, 0.0), (10.0, 0.0, 0.0), (14.0, 1.0, 0.0),
                                    (16.0, 0.5, 0.0)])]),
        mixer_track(30, "Pluck", [clip(31, "pluck.wav", 0.0)],
                    sends=[send(32, 210, 0.8, pre_fader=True)], volume=0.7),
        mixer_track(40, "Tone", [clip(41, "tone-440.wav", 0.0)],
                    effects=[effect(42, "equaliser", {})], mute=True),
        mixer_track(50, "Noise", [clip(51, "noise-pink.wav", 0.0)],
                    effects=[effect(52, "equaliser", {})], mute=True),
    ]
    limiter = effect(100, "limiter", {"inputGain": 0.0, "ceiling": -1.0, "release": 100.0})
    project = project_v4([limiter], [reverb_bus, delay_bus], tracks)
    (folder / "phase4-effects.dawproject").write_text(json.dumps(project, indent=2) + "\n")

    # Delay compensation: clicks on one track and the same clicks inverted on two
    # others, one through a limiter (1.5 ms of lookahead), one sent pre-fader (its own
    # fader down) to a bus with a limiter. Aligned, the clicks and either inverted copy
    # cancel to silence.
    limited_bus = bus(300, "Limited bus", [effect(301, "limiter", {"ceiling": 0.0})])
    tracks = [
        mixer_track(1, "Clicks", [clip(2, "click-120bpm.wav", 0.0)], volume=0.5),
        mixer_track(10, "Inverted, limited", [clip(11, "click-120bpm-inverted.wav", 0.0)],
                    effects=[effect(12, "limiter", {"ceiling": 0.0})], volume=0.5),
        mixer_track(20, "Inverted, via bus", [clip(21, "click-120bpm-inverted.wav", 0.0)],
                    sends=[send(22, 300, 0.5, pre_fader=True)], volume=0.0, mute=True),
    ]
    latency = project_v4([], [limited_bus], tracks)
    (folder / "phase4-latency.dawproject").write_text(json.dumps(latency, indent=2) + "\n")


# ---------------------------------------------------------------------------- Phase 6

def one_shot(rate, seconds, generator):
    """A sound of the given length from generator(t, n), faded at the end."""
    out = [generator(n / rate, n) for n in range(int(rate * seconds))]
    return faded(out, rate, 0.005)


def drum_sounds(rate):
    """Synthetic drum hits for the drum sampler's pads."""
    noise = random.Random(11)
    white = [noise.random() * 2 - 1 for _ in range(rate)]
    bright = [white[n] - white[n - 1] if n else 0.0 for n in range(rate)]  # a high-pass

    def kick_hit(t, n):
        phase = 2 * math.pi * (50 * t + 100 * (1 - math.exp(-t * 40)) / 40)
        return 0.8 * math.exp(-t * 9) * math.sin(phase)

    def snare_hit(t, n):
        return math.exp(-t * 18) * (0.35 * math.sin(2 * math.pi * 185 * t) + 0.45 * white[n])

    def clap_hit(t, n):
        bursts = sum(math.exp(-(t - start) * 120) for start in (0.0, 0.011, 0.022) if t >= start)
        return 0.5 * white[n] * (min(bursts, 1.0) + 0.4 * math.exp(-t * 14))

    return {
        "kick.wav": one_shot(rate, 0.5, kick_hit),
        "snare.wav": one_shot(rate, 0.3, snare_hit),
        "clap.wav": one_shot(rate, 0.35, clap_hit),
        "closed-hat.wav": one_shot(rate, 0.08, lambda t, n: 0.35 * math.exp(-t * 60) * bright[n]),
        "open-hat.wav": one_shot(rate, 0.6, lambda t, n: 0.3 * math.exp(-t * 6) * bright[n]),
    }


def sampler_tone(rate):
    """C3 (261.63 Hz) with a few harmonics: a quick attack, a decay to half, then steady
    until 3 s, where the sampler's loop (40 to 90 %) sits."""
    frequency = 440 * 2 ** ((60 - 69) / 12)

    def tone(t, n):
        envelope = min(1.0, t / 0.005) * (0.5 + 0.5 * math.exp(-t * 6))
        value = sum(math.sin(2 * math.pi * frequency * h * t) / (h * h) for h in range(1, 7))
        return 0.45 * envelope * value

    return one_shot(rate, 3.0, tone)


def vlq(value):
    """A MIDI file's variable-length quantity."""
    out = [value & 0x7F]
    value >>= 7
    while value:
        out.insert(0, (value & 0x7F) | 0x80)
        value >>= 7
    return bytes(out)


def smf_track(events):
    """events: (tick, order, bytes); at one tick note-offs (order 0) come before
    controllers (1) and note-ons (2)."""
    data = bytearray()
    last = 0
    for tick, _, message in sorted(events, key=lambda e: (e[0], e[1])):
        data += vlq(tick - last) + message
        last = tick
    data += vlq(0) + b"\xff\x2f\x00"
    return b"MTrk" + struct.pack(">I", len(data)) + bytes(data)


def write_smf(path, file_type, ppq, tracks):
    body = b"".join(smf_track(events) for events in tracks)
    header = b"MThd" + struct.pack(">IHHH", 6, file_type, len(tracks), ppq)
    Path(path).write_bytes(header + body)


def meta(tick, kind, payload):
    return (tick, 0, bytes([0xFF, kind]) + vlq(len(payload)) + payload)


def note(channel, key, velocity, start, length):
    return [(start, 2, bytes([0x90 | channel, key, velocity])),
            (start + length, 0, bytes([0x80 | channel, key, 0]))]


def controller(channel, number, value, tick):
    return (tick, 1, bytes([0xB0 | channel, number, value]))


def bend(channel, value, tick):
    return (tick, 1, bytes([0xE0 | channel, value & 0x7F, value >> 7]))


def song_parts(ppq):
    """Eight bars of 4/4 at 110 BPM: drums (channel 10), bass (2), chords (1) with
    the sustain pedal and the mod wheel."""
    beat = ppq
    bar = 4 * beat
    drums, bass, chords = [], [], []
    for b in range(8):
        at = b * bar
        for step in range(8):  # eighths
            tick = at + step * beat // 2
            if step in (0, 4) or (b % 2 and step == 5):
                drums += note(9, 36, 110, tick, beat // 4)  # kick
            if step in (2, 6):
                drums += note(9, 38, 100, tick, beat // 4)  # snare
            if step == 7:
                drums += note(9, 46, 80, tick, beat // 2)  # open hat, choked by the next
            else:
                drums += note(9, 42, 70 + 20 * (step % 2 == 0), tick, beat // 4)
        if b % 4 == 3:
            drums += note(9, 39, 100, at + 3 * beat, beat // 4)  # clap

    progression = [(45, (57, 60, 64)), (41, (53, 57, 60)), (48, (55, 60, 64)), (43, (55, 59, 62))]
    for b in range(8):
        root, chord = progression[b % 4]
        at = b * bar
        for step in range(8):
            bass += note(1, root + (12 if step == 6 else 0), 96, at + step * beat // 2, beat // 2 - 20)
        for key in chord:
            chords += note(0, key, 80, at, bar - beat // 2)
        chords += [controller(0, 64, 127, at), controller(0, 64, 0, at + bar - 10)]
    for step in range(33):  # the mod wheel up over bars 5 to 8, then back
        chords.append(controller(0, 1, min(127, step * 4), 4 * bar + step * bar // 8))
    chords.append(controller(0, 1, 0, 8 * bar - 1))
    return drums, bass, chords


def write_midi_files(folder):
    ppq = 480  # not the app's 960: the import converts
    tempo = 60_000_000 // 110
    conductor = [meta(0, 0x03, b"Song"), meta(0, 0x51, struct.pack(">I", tempo)[1:]),
                 meta(0, 0x58, bytes([4, 2, 24, 8]))]
    drums, bass, chords = song_parts(ppq)
    named = [([meta(0, 0x03, b"Drums")] + drums), ([meta(0, 0x03, b"Bass")] + bass),
             ([meta(0, 0x03, b"Chords")] + chords)]
    write_smf(folder / "song-type1.mid", 1, ppq, [conductor] + named)
    write_smf(folder / "song-type0.mid", 0, ppq, [conductor + drums + bass + chords])

    # Every key from C-1 (24) to C7 (108), an eighth each at 120 BPM: tuning, and
    # aliasing on the high notes.
    scale = [meta(0, 0x51, struct.pack(">I", 500_000)[1:]), meta(0, 0x03, b"Scale")]
    for index, key in enumerate(range(24, 109)):
        scale += note(0, key, 100, index * ppq // 2, ppq // 2 - 10)
    write_smf(folder / "scale.mid", 0, ppq, [scale])

    # C3 held for 8 s: the pitch bend up 2 semitones and down 2, then the mod wheel
    # (vibrato) up and down; then four short notes held by the sustain pedal.
    moves = [meta(0, 0x51, struct.pack(">I", 500_000)[1:]), meta(0, 0x03, b"Controllers")]
    moves += note(0, 60, 100, 0, 16 * ppq)
    for step in range(65):
        phase = step / 64
        value = int(round(8192 + 8191 * math.sin(2 * math.pi * phase)))
        moves.append(bend(0, max(0, min(16383, value)), step * 8 * ppq // 64))
    moves.append(bend(0, 8192, 8 * ppq))
    for step in range(33):
        value = int(round(127 * math.sin(math.pi * step / 32)))
        moves.append(controller(0, 1, value, 8 * ppq + step * 8 * ppq // 32))
    moves.append(controller(0, 64, 127, 17 * ppq))
    for index, key in enumerate((60, 64, 67, 72)):
        moves += note(0, key, 90, 17 * ppq + index * ppq // 2, ppq // 4)
    moves.append(controller(0, 64, 0, 22 * ppq))
    write_smf(folder / "controllers.mid", 0, ppq, [moves])


def midi_clip(clip_id, name, position, length, notes_list, controllers=()):
    """notes_list: (start s, length s, key, velocity) relative to the clip."""
    return {"id": clip_id, "kind": "midi", "name": name, "position": ticks(position), "offset": 0,
            "length": ticks(length),
            "notes": [{"id": i + 1, "start": ticks(s), "length": ticks(l), "key": k, "velocity": v}
                      for i, (s, l, k, v) in enumerate(notes_list)],
            "controllers": [{"type": kind, "events": [{"position": ticks(at), "value": value}
                                                      for at, value in events]}
                            for kind, events in controllers]}


def midi_track(track_id, name, instrument, clips, effects=(), sends=(), lanes=(), volume=1.0,
               mute=False, channel=1):
    return {"id": track_id, "kind": "midi", "name": name, "volume": volume, "pan": 0.0,
            "mute": mute, "solo": False, "midiInput": {"device": "", "channel": 0},
            "midiChannel": channel, "instrument": instrument, "monitoring": "auto",
            "armed": False, "effects": list(effects), "sends": list(sends),
            "automation": automation(list(lanes)), "clips": clips}


def instrument(effect_id, type_name, parameters, files=None):
    result = effect(effect_id, type_name, parameters)
    if files:
        result["files"] = files
    return result


def write_phase6_files(out):
    """Phase 6: sounds for the sampler and the drum pads, MIDI files to import, and a
    project with every instrument (120 BPM, a bar is 2 s, 8 bars)."""
    folder = out / "phase6"
    folder.mkdir(parents=True, exist_ok=True)
    rate = 48000
    for name, samples in drum_sounds(rate).items():
        write_wav(folder / name, rate, [samples])
    write_wav(folder / "tone-c3.wav", rate, [sampler_tone(rate)])
    write_wav(folder / "a440.wav", rate, [faded(sine(440, 10 ** (-12 / 20), rate, 4), rate)])
    write_midi_files(folder)

    beat = 0.5
    bar = 2.0
    progression = [(45, (57, 60, 64)), (41, (53, 57, 60)), (48, (55, 60, 64)), (43, (55, 59, 62))]
    chords, bassline, beats = [], [], []
    for b in range(8):
        root, chord = progression[b % 4]
        chords += [(b * bar, bar - beat / 2, key, 84) for key in chord]
        # Bass: eighths, legato into the octave on the 7th (glide).
        bassline += [(b * bar + step * beat / 2, beat / 2 + (0.02 if step == 5 else -0.02),
                      root - 12 + (12 if step == 6 else 0), 100) for step in range(8)]
        for step in range(8):
            at = b * bar + step * beat / 2
            if step in (0, 4):
                beats.append((at, 0.1, 36, 110))
            if step in (2, 6):
                beats.append((at, 0.1, 38, 100))
            beats.append((at, 0.1, 46 if step == 7 else 42, 90 if step % 2 == 0 else 64))
        if b % 4 == 3:
            beats.append((b * bar + 3 * beat, 0.1, 39, 100))
    melody = [(0.0, 3.5, 60, 100), (4.0, 0.5, 64, 90), (4.5, 0.5, 67, 90), (5.0, 1.0, 72, 100),
              (6.0, 2.0, 48, 100), (8.0, 0.25, 84, 80), (8.5, 0.25, 79, 80), (9.0, 0.25, 76, 80),
              (9.5, 0.25, 72, 80), (10.0, 6.0, 55, 110)]

    cutoff = lambda hz: math.log(hz / 20) / math.log(1000)  # the cutoff's normalised value
    reverb = bus(900, "Reverb", [effect(901, "reverb", {"decay": 2.2, "mix": 100.0})])
    tracks = [
        midi_track(1, "Chords", instrument(2, "synth", {"cutoff": 500.0}),
                   [midi_clip(3, "Chords", 0.0, 8 * bar, chords,
                              [("cc:1", [(8.0, 0), (12.0, 100), (16.0, 0)])])],
                   sends=[send(4, 900, 0.5)],
                   lanes=[("effect:2:cutoff", [(8.0, cutoff(500), 0.0), (16.0, cutoff(8000), 0.0)])],
                   volume=0.25),
        midi_track(10, "Bass", instrument(11, "synth", {"voiceMode": 2.0, "glide": 80.0,
                                                         "osc2Octave": 2.0, "cutoff": 900.0}),
                   [midi_clip(12, "Bass", 0.0, 8 * bar, bassline)], volume=0.5, channel=2),
        midi_track(20, "Sampler", instrument(21, "sampler", {"loop": 1.0, "loopStart": 40.0,
                                                              "loopEnd": 90.0, "crossfade": 20.0},
                                             {"sample": "tone-c3.wav"}),
                   [midi_clip(22, "Melody", 0.0, 8 * bar, melody)], sends=[send(23, 900, 0.3)],
                   volume=0.6, channel=3),
        midi_track(30, "Drums", instrument(31, "drums", {}, {
            "pad1": "kick.wav", "pad3": "snare.wav", "pad4": "clap.wav",
            "pad7": "closed-hat.wav", "pad11": "open-hat.wav"}),
                   [midi_clip(32, "Beat", 0.0, 8 * bar, beats)], volume=0.5, channel=10),
        # Tuning: a sine at A3 (key 69, 440 Hz) and the 440 Hz file; solo both.
        midi_track(40, "Sine A3", instrument(41, "synth", {"osc1Wave": 0.0, "osc2Level": 0.0,
                                                            "cutoff": 20000.0, "resonance": 0.0,
                                                            "filterEnvelope": 0.0}),
                   [midi_clip(42, "A3", 0.0, 4.0, [(0.0, 4.0, 69, 100)])], mute=True),
        dict(mixer_track(50, "A440 file", [dict(clip(51, "a440.wav", 0.0), kind="audio")], mute=True),
             kind="audio"),
    ]
    project = project_v4([], [reverb], tracks)
    project["formatVersion"] = 5
    (folder / "phase6-instruments.dawproject").write_text(json.dumps(project, indent=2) + "\n")


def extended80(value):
    """IEEE 754 80-bit extended float, as AIFF stores its sample rate."""
    exponent = 16383 + int(math.floor(math.log2(value)))
    mantissa = int(value * 2 ** (63 - (exponent - 16383)))
    return struct.pack(">HQ", exponent, mantissa)


def write_aiff(path, rate, channels):
    frames = len(channels[0])
    data = bytearray()
    for samples in zip(*channels):
        for value in samples:
            data += struct.pack(">h", to_int16(value))
    comm = struct.pack(">hIh", len(channels), frames, 16) + extended80(rate)
    ssnd = struct.pack(">II", 0, 0) + bytes(data)
    body = b"AIFF" + b"COMM" + struct.pack(">I", len(comm)) + comm + b"SSND" + struct.pack(">I", len(ssnd)) + ssnd
    Path(path).write_bytes(b"FORM" + struct.pack(">I", len(body)) + body)


def main():
    parser = argparse.ArgumentParser(description="Writes the listening test files.")
    parser.add_argument("folder", nargs="?", default="build/listening-tests")
    parser.add_argument("--long", action="store_true", help="also write an hour-long file")
    arguments = parser.parse_args()
    out = Path(arguments.folder)
    out.mkdir(parents=True, exist_ok=True)
    minus6 = 10 ** (-6 / 20)

    # 1 kHz at -6 dBFS on both channels, 10 s: level meter and clean pause/stop/seek.
    tone = sine(1000, minus6, 48000, 10)
    write_wav(out / "01-sine-1k-minus6dB.wav", 48000, [tone, tone])

    # 2 s on the left only, then 2 s on the right only, twice: channel order.
    left = sine(440, minus6, 48000, 2) + [0.0] * 96000
    right = [0.0] * 96000 + sine(660, minus6, 48000, 2)
    write_wav(out / "02-left-then-right.wav", 48000, [left * 2, right * 2])

    # Exactly 480 cycles in 1 s: with Loop on, the seam must be inaudible.
    seam = sine(480, minus6, 48000, 1)
    write_wav(out / "03-loop-seam-480Hz.wav", 48000, [seam, seam])

    # 440 Hz (the A above middle C), mono, 96 kHz: converted to the device rate.
    write_wav(out / "04-a440-mono-96k.wav", 96000, [sine(440, minus6, 96000, 6)])

    # 220 Hz peaking at exactly 0 dBFS: lights the clip indicators.
    full = sine(220, 1.0, 44100, 4)
    write_wav(out / "05-full-scale-220Hz-44k.wav", 44100, [full, full])

    # The same tone as file 01, as AIFF at 44.1 kHz.
    aiff_tone = sine(1000, minus6, 44100, 10)
    write_aiff(out / "06-sine-1k-minus6dB.aiff", 44100, [aiff_tone, aiff_tone])

    write_phase2_files(out)
    write_phase3_files(out, arguments.long)
    write_phase4_files(out)
    write_phase6_files(out)

    print(f"Listening test files written to {out.resolve()}")


if __name__ == "__main__":
    main()
