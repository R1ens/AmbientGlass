"""Generates the synthetic input pictures for the golden tests (standard library only).

Run from anywhere: python tests/golden/make_inputs.py
Writes 8-bit RGB PNGs into tests/golden/inputs/.
"""
import colorsys
import math
import os
import struct
import zlib

HERE = os.path.dirname(os.path.abspath(__file__))
OUT = os.path.join(HERE, "inputs")


def write_png(path, w, h, pixel):
    """pixel(x, y) -> (r, g, b) floats 0..1. Rows use the Sub filter, which suits gradients."""
    rows = bytearray()
    for y in range(h):
        raw = bytearray()
        for x in range(w):
            r, g, b = pixel(x, y)
            raw += bytes(max(0, min(255, round(c * 255))) for c in (r, g, b))
        rows.append(1)
        for i in range(len(raw)):
            rows.append((raw[i] - (raw[i - 3] if i >= 3 else 0)) & 0xFF)

    def chunk(kind, data):
        body = kind + data
        return struct.pack(">I", len(data)) + body + struct.pack(">I", zlib.crc32(body) & 0xFFFFFFFF)

    png = b"\x89PNG\r\n\x1a\n"
    png += chunk(b"IHDR", struct.pack(">IIBBBBB", w, h, 8, 2, 0, 0, 0))
    png += chunk(b"IDAT", zlib.compress(bytes(rows), 9))
    png += chunk(b"IEND", b"")
    with open(path, "wb") as f:
        f.write(png)
    print(f"{os.path.relpath(path, HERE)}: {w}x{h}, {len(png)} bytes")


BARS = [(1, 1, 1), (1, 1, 0), (0, 1, 1), (0, 1, 0), (1, 0, 1), (1, 0, 0), (0, 0, 1)]


def bars(w, h):
    """Colour bars over the top two thirds, a grey ramp with a hue tint below: distinct colours on every edge."""
    def px(x, y):
        if y < h * 2 // 3:
            c = BARS[min(6, x * 7 // w)]
            k = 0.75 if y < h // 3 else 1.0
            return tuple(v * k for v in c)
        t = x / (w - 1)
        return (t, t * 0.9 + 0.05 * (1 - t), t * 0.8 + 0.2 * (1 - t))
    return px


def radial(w, h):
    """Hue by angle, bright at the edges and dark in the middle, with a lobed ring for some detail."""
    cx, cy = (w - 1) / 2, (h - 1) / 2

    def px(x, y):
        dx, dy = (x - cx) / cx, (y - cy) / cy
        r = min(1.0, math.hypot(dx, dy) / math.sqrt(2))
        hue = (math.atan2(dy, dx) / (2 * math.pi)) % 1.0
        ring = 0.15 * math.exp(-((r - 0.55) * 8) ** 2) * (1 + math.cos(6 * hue * 2 * math.pi))
        v = min(1.0, 0.08 + 0.9 * r ** 1.5 + ring)
        return colorsys.hsv_to_rgb(hue, 0.9, v)
    return px


def dark(w, h):
    """A dark scene: a faint vertical gradient, a bright orange window at the right edge and a thin cyan
    line along the top edge. The glow should pick up those edge details and little else."""
    def px(x, y):
        v = 0.03 + 0.04 * y / (h - 1)
        r, g, b = v, v, v * 1.2
        if w - 26 <= x < w - 6 and h // 3 <= y < h // 3 + 50:
            r, g, b = 1.0, 0.55, 0.1
        if y < 3 and w // 4 <= x < w * 3 // 4:
            r, g, b = 0.1, 0.9, 1.0
        return (r, g, b)
    return px


def letterbox(w, h, band):
    """Burned-in black bands with a diagonal teal-to-orange picture between them and a white
    subtitle block inside the lower band, as a film with hard-coded subtitles would look."""
    def px(x, y):
        if y < band or y >= h - band:
            if h - band + 4 <= y < h - band + 10 and w // 3 <= x < w * 2 // 3:
                return (0.95, 0.95, 0.95)
            return (0, 0, 0)
        t = (x / (w - 1) + (y - band) / (h - 2 * band - 1)) / 2
        return (0.1 + 0.9 * t, 0.5 + 0.1 * t, 0.7 - 0.6 * t)
    return px


def main():
    os.makedirs(OUT, exist_ok=True)
    write_png(os.path.join(OUT, "bars.png"), 320, 180, bars(320, 180))
    write_png(os.path.join(OUT, "radial.png"), 240, 180, radial(240, 180))
    write_png(os.path.join(OUT, "dark.png"), 320, 180, dark(320, 180))
    write_png(os.path.join(OUT, "letterbox.png"), 320, 180, letterbox(320, 180, 22))


if __name__ == "__main__":
    main()
