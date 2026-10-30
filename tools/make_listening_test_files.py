#!/usr/bin/env python3
"""Writes the reference files used by docs/LISTENING_TESTS.md.

Usage: python3 tools/make_listening_test_files.py [output folder]
(default: build/listening-tests). Needs only the Python standard library.
"""

import math
import struct
import sys
import wave
from pathlib import Path


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
    out = Path(sys.argv[1] if len(sys.argv) > 1 else "build/listening-tests")
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

    print(f"Listening test files written to {out.resolve()}")


if __name__ == "__main__":
    main()
