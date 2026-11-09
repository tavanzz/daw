#!/usr/bin/env python3
"""Draws the app icon: app/resources/AppIcon.png, 1024 x 1024, in Neon Grid's colours.

A dark rounded square (the macOS icon grid: 824 px with a transparent margin) holding a
waveform of rounded bars that fade from cyan to magenta, with a yellow playhead. Only the
standard library: shapes are signed distance functions, anti-aliased over one pixel, so
the result is exact and the same on every machine. JUCE turns the PNG into the .icns and
the .ico; the Linux AppImage uses it as it is.

    python3 tools/make_app_icon.py [output.png]
"""

import math
import pathlib
import struct
import sys
import zlib

SIZE = 1024
SQUARE = 824            # macOS Big Sur icon grid
RADIUS = 186
CENTRE = SIZE / 2

BACKGROUND_TOP = (0x17, 0x12, 0x2C)
BACKGROUND_BOTTOM = (0x07, 0x06, 0x0B)
RIM = (0x22, 0x1E, 0x36)
CYAN = (0x00, 0xF0, 0xFF)
MAGENTA = (0xFF, 0x2B, 0xD6)
YELLOW = (0xF8, 0xF3, 0x2B)

# The waveform: one height per bar (fraction of the largest), symmetric about the centre.
HEIGHTS = [0.16, 0.30, 0.52, 0.36, 0.72, 0.95, 0.62, 0.86, 0.44, 0.68, 0.30, 0.42, 0.18]
BAR_WIDTH = 38.0
BAR_GAP = 17.0
MAX_HALF_HEIGHT = 250.0
PLAYHEAD_X = CENTRE + 3.5 * (BAR_WIDTH + BAR_GAP)  # in the gap after the tenth bar
PLAYHEAD_HALF_HEIGHT = 300.0


def clamp(x, lo=0.0, hi=1.0):
    return lo if x < lo else hi if x > hi else x


def coverage(distance):
    """Fraction of a pixel inside a shape, from the signed distance at its centre."""
    return clamp(0.5 - distance)


def rounded_square(x, y):
    half = SQUARE / 2 - RADIUS
    qx = abs(x - CENTRE) - half
    qy = abs(y - CENTRE) - half
    outside = math.hypot(max(qx, 0.0), max(qy, 0.0))
    return outside + min(max(qx, qy), 0.0) - RADIUS


def capsule(x, y, cx, half_height, half_width):
    """Vertical bar with round ends, centred on (cx, CENTRE)."""
    dy = max(abs(y - CENTRE) - (half_height - half_width), 0.0)
    return math.hypot(x - cx, dy) - half_width


def mix(a, b, t):
    return tuple(a[i] + (b[i] - a[i]) * t for i in range(3))


def over(base, colour, alpha):
    return tuple(base[i] + (colour[i] - base[i]) * alpha for i in range(3))


BAR_PITCH = BAR_WIDTH + BAR_GAP
FIRST_BAR = CENTRE - (len(HEIGHTS) - 1) / 2 * BAR_PITCH


def bars_distance(x, y):
    """Distance to the nearest bar, and that bar's centre."""
    index = round((x - FIRST_BAR) / BAR_PITCH)
    best = (1e9, FIRST_BAR)
    for i in (index - 1, index, index + 1):
        if 0 <= i < len(HEIGHTS):
            cx = FIRST_BAR + i * BAR_PITCH
            d = capsule(x, y, cx, max(HEIGHTS[i] * MAX_HALF_HEIGHT, BAR_WIDTH / 2), BAR_WIDTH / 2)
            if d < best[0]:
                best = (d, cx)
    return best


def pixel(x, y):
    inside = coverage(rounded_square(x, y))
    if inside <= 0.0:
        return (0, 0, 0, 0)

    colour = mix(BACKGROUND_TOP, BACKGROUND_BOTTOM, clamp((y - (CENTRE - SQUARE / 2)) / SQUARE))

    # A faint grid, as on the timeline.
    if (x - CENTRE) % 82 < 1.5 or (y - CENTRE) % 82 < 1.5:
        colour = over(colour, RIM, 0.55)

    # The bars: a soft glow, then the bar itself.
    distance, bar_centre = bars_distance(x, y)
    bar_colour = mix(CYAN, MAGENTA, clamp((bar_centre - FIRST_BAR) / (len(HEIGHTS) - 1) / BAR_PITCH))
    if distance > 0:
        colour = over(colour, bar_colour, 0.42 * math.exp(-distance / 22.0))
    colour = over(colour, bar_colour, coverage(distance))

    # The playhead.
    playhead = capsule(x, y, PLAYHEAD_X, PLAYHEAD_HALF_HEIGHT, 4.0)
    if playhead > 0:
        colour = over(colour, YELLOW, 0.35 * math.exp(-playhead / 14.0))
    colour = over(colour, YELLOW, coverage(playhead))

    # A thin rim along the edge.
    rim = abs(rounded_square(x, y) + 3.0) - 1.5
    colour = over(colour, mix(RIM, CYAN, 0.25), coverage(rim))

    return tuple(int(round(clamp(c / 255.0) * 255)) for c in colour) + (int(round(inside * 255)),)


def write_png(path, rows):
    def chunk(kind, data):
        body = kind + data
        return struct.pack(">I", len(data)) + body + struct.pack(">I", zlib.crc32(body) & 0xFFFFFFFF)

    raw = b"".join(b"\x00" + bytes(row) for row in rows)
    header = struct.pack(">IIBBBBB", SIZE, SIZE, 8, 6, 0, 0, 0)  # 8-bit RGBA
    png = b"\x89PNG\r\n\x1a\n" + chunk(b"IHDR", header) + chunk(b"IDAT", zlib.compress(raw, 9)) + chunk(b"IEND", b"")
    path.write_bytes(png)


def main():
    root = pathlib.Path(__file__).resolve().parent.parent
    output = pathlib.Path(sys.argv[1]) if len(sys.argv) > 1 else root / "app" / "resources" / "AppIcon.png"
    output.parent.mkdir(parents=True, exist_ok=True)

    rows = []
    for j in range(SIZE):
        row = []
        y = j + 0.5
        for i in range(SIZE):
            row.extend(pixel(i + 0.5, y))
        rows.append(row)
    write_png(output, rows)
    print(f"Wrote {output}")


if __name__ == "__main__":
    main()
