"""Generate src/app.icon.ico for VlcAmbient (no third-party dependencies).

Design: dark rounded square, a smaller dark "screen" rectangle in the middle,
and a warm amber glow spilling out around the screen.
Sizes: 16, 32, 48 (32-bit BMP entries) and 256 (PNG entry).

Usage:  python tools/make_icon.py [output.ico]
"""
import math
import os
import struct
import sys
import zlib

SIZES = (16, 32, 48, 256)
SUPERSAMPLE = 4

BG = (24, 26, 32)          # rounded square
SCREEN = (10, 11, 15)      # the "video" rectangle
GLOW_NEAR = (255, 196, 92)  # hot edge of the glow
GLOW_FAR = (255, 122, 24)   # outer amber


def rounded_box_sdf(px, py, cx, cy, hw, hh, r):
    """Signed distance to a rounded rectangle (negative inside)."""
    qx = abs(px - cx) - (hw - r)
    qy = abs(py - cy) - (hh - r)
    outside = math.hypot(max(qx, 0.0), max(qy, 0.0))
    inside = min(max(qx, qy), 0.0)
    return outside + inside - r


def lerp(a, b, t):
    return tuple(x + (y - x) * t for x, y in zip(a, b))


def shade(x, y, s):
    """Colour and alpha of one sample at (x, y) in a canvas of size s."""
    c = s / 2.0
    # Outer rounded square (fills the whole icon, small inset for AA).
    d_bg = rounded_box_sdf(x, y, c, c, c - 0.25, c - 0.25, s * 0.22)
    if d_bg > 0:
        return (0, 0, 0), 0.0
    # Screen: 16:10-ish, slightly above centre would look odd at 16 px, keep centred.
    d_scr = rounded_box_sdf(x, y, c, c, s * 0.27, s * 0.18, s * 0.035)
    if d_scr <= 0:
        return SCREEN, 1.0
    # Glow: exponential falloff from the screen edge.
    t = math.exp(-d_scr / (s * 0.085))
    glow = lerp(GLOW_FAR, GLOW_NEAR, t ** 1.5)
    return lerp(BG, glow, min(1.0, 1.05 * t)), 1.0


def render(s):
    """Return RGBA bytes (top-down rows) for a size-s icon."""
    n = SUPERSAMPLE
    out = bytearray()
    for py in range(s):
        for px in range(s):
            r = g = b = a = 0.0
            for sy in range(n):
                for sx in range(n):
                    col, al = shade(px + (sx + 0.5) / n, py + (sy + 0.5) / n, s)
                    r += col[0] * al
                    g += col[1] * al
                    b += col[2] * al
                    a += al
            if a > 0:
                r, g, b = r / a, g / a, b / a  # un-premultiply
            a /= n * n
            out += bytes((round(r), round(g), round(b), round(a * 255)))
    return bytes(out)


def png_bytes(rgba, s):
    def chunk(tag, data):
        body = tag + data
        return struct.pack(">I", len(data)) + body + struct.pack(">I", zlib.crc32(body))

    raw = b"".join(b"\x00" + rgba[y * s * 4:(y + 1) * s * 4] for y in range(s))
    ihdr = struct.pack(">IIBBBBB", s, s, 8, 6, 0, 0, 0)  # 8-bit RGBA
    return (b"\x89PNG\r\n\x1a\n" + chunk(b"IHDR", ihdr)
            + chunk(b"IDAT", zlib.compress(raw, 9)) + chunk(b"IEND", b""))


def bmp_bytes(rgba, s):
    """ICO-style DIB: BITMAPINFOHEADER (height doubled), BGRA bottom-up, AND mask."""
    header = struct.pack("<IiiHHIIiiII", 40, s, s * 2, 1, 32, 0, 0, 0, 0, 0, 0)
    pixels = bytearray()
    for y in range(s - 1, -1, -1):
        row = rgba[y * s * 4:(y + 1) * s * 4]
        for i in range(0, len(row), 4):
            r, g, b, a = row[i:i + 4]
            pixels += bytes((b, g, r, a))
    mask_stride = ((s + 31) // 32) * 4
    mask = bytearray()
    for y in range(s - 1, -1, -1):
        bits = bytearray(mask_stride)
        for x in range(s):
            if rgba[(y * s + x) * 4 + 3] == 0:
                bits[x // 8] |= 0x80 >> (x % 8)
        mask += bits
    return header + bytes(pixels) + bytes(mask)


def build_ico(sizes=SIZES):
    images = []
    for s in sizes:
        rgba = render(s)
        images.append((s, png_bytes(rgba, s) if s >= 256 else bmp_bytes(rgba, s)))
    out = struct.pack("<HHH", 0, 1, len(images))
    offset = 6 + 16 * len(images)
    for s, data in images:
        dim = 0 if s >= 256 else s
        out += struct.pack("<BBBBHHII", dim, dim, 0, 0, 1, 32, len(data), offset)
        offset += len(data)
    return out + b"".join(data for _, data in images)


def main():
    root = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
    target = sys.argv[1] if len(sys.argv) > 1 else os.path.join(root, "src", "app.icon.ico")
    data = build_ico()
    with open(target, "wb") as f:
        f.write(data)
    print(f"wrote {target} ({len(data)} bytes, sizes {', '.join(map(str, SIZES))})")


if __name__ == "__main__":
    main()
