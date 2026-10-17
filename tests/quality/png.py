"""Minimal standard-library PNG reader/writer for the quality checks.

read_png() handles 8-bit greyscale, grey+alpha, RGB and RGBA, non-interlaced, with all five
filter types. The common rows (filter None/Up, which is what WIC writes) are reconstructed with
whole-row big-integer arithmetic, so a 3840x1080 RGBA image decodes in well under a second.
write_png() writes 8-bit RGB or RGBA with filter None.
"""
import struct
import zlib

_SIG = b"\x89PNG\r\n\x1a\n"
_CHANNELS = {0: 1, 4: 2, 2: 3, 6: 4}


def _chunk(kind, data):
    body = kind + data
    return struct.pack(">I", len(data)) + body + struct.pack(">I", zlib.crc32(body) & 0xFFFFFFFF)


def write_png(path, w, h, rows, channels=3):
    """rows: sequence of h bytes-like rows, each w*channels bytes (8-bit RGB or RGBA)."""
    ct = {1: 0, 2: 4, 3: 2, 4: 6}[channels]
    stride = w * channels
    raw = bytearray()
    for r in rows:
        assert len(r) == stride, (len(r), stride)
        raw.append(0)
        raw += r
    with open(path, "wb") as f:
        f.write(_SIG)
        f.write(_chunk(b"IHDR", struct.pack(">IIBBBBB", w, h, 8, ct, 0, 0, 0)))
        f.write(_chunk(b"IDAT", zlib.compress(bytes(raw), 6)))
        f.write(_chunk(b"IEND", b""))


def _add_bytes(a, b, n, lo, hi):
    """Bytewise (a + b) mod 256 of two n-byte rows (SWAR on Python ints)."""
    x = int.from_bytes(a, "little")
    y = int.from_bytes(b, "little")
    s = ((x & lo) + (y & lo)) ^ ((x ^ y) & hi)
    return bytearray(s.to_bytes(n, "little"))


def read_png(path):
    """Returns (w, h, channels, rows) with rows a list of bytearray (8-bit samples)."""
    with open(path, "rb") as f:
        data = f.read()
    if data[:8] != _SIG:
        raise ValueError("not a PNG: %s" % path)
    pos = 8
    idat = []
    w = h = ct = None
    while pos < len(data):
        n, = struct.unpack(">I", data[pos:pos + 4])
        kind = data[pos + 4:pos + 8]
        body = data[pos + 8:pos + 8 + n]
        pos += 12 + n
        if kind == b"IHDR":
            w, h, depth, ct, _, _, interlace = struct.unpack(">IIBBBBB", body)
            if depth != 8 or interlace != 0 or ct not in _CHANNELS:
                raise ValueError("unsupported PNG (depth %d, colour type %d, interlace %d)" % (depth, ct, interlace))
        elif kind == b"IDAT":
            idat.append(body)
        elif kind == b"IEND":
            break
    ch = _CHANNELS[ct]
    raw = zlib.decompress(b"".join(idat))
    stride = w * ch
    lo = int.from_bytes(b"\x7f" * stride, "little")
    hi = int.from_bytes(b"\x80" * stride, "little")
    prev = bytearray(stride)
    rows = []
    p = 0
    for _ in range(h):
        ft = raw[p]
        cur = bytearray(raw[p + 1:p + 1 + stride])
        p += 1 + stride
        if ft == 0:
            pass
        elif ft == 2:
            cur = _add_bytes(cur, prev, stride, lo, hi)
        elif ft == 1:
            for i in range(ch, stride):
                cur[i] = (cur[i] + cur[i - ch]) & 255
        elif ft == 3:
            for i in range(stride):
                a = cur[i - ch] if i >= ch else 0
                cur[i] = (cur[i] + ((a + prev[i]) >> 1)) & 255
        elif ft == 4:
            for i in range(stride):
                a = cur[i - ch] if i >= ch else 0
                b = prev[i]
                c = prev[i - ch] if i >= ch else 0
                pp = a + b - c
                pa, pb, pc = abs(pp - a), abs(pp - b), abs(pp - c)
                pr = a if pa <= pb and pa <= pc else (b if pb <= pc else c)
                cur[i] = (cur[i] + pr) & 255
        else:
            raise ValueError("bad PNG filter type %d" % ft)
        rows.append(cur)
        prev = cur
    return w, h, ch, rows


class Image:
    """Decoded 8-bit image; px() returns (r, g, b) or (r, g, b, a)."""

    def __init__(self, w, h, ch, rows):
        self.w, self.h, self.ch, self.rows = w, h, ch, rows

    @classmethod
    def load(cls, path):
        return cls(*read_png(path))

    def px(self, x, y):
        i = x * self.ch
        return tuple(self.rows[y][i:i + self.ch])

    def rgb(self, x, y):
        i = x * self.ch
        r = self.rows[y]
        if self.ch >= 3:
            return r[i], r[i + 1], r[i + 2]
        return r[i], r[i], r[i]

    def alpha(self, x, y):
        if self.ch == 4:
            return self.rows[y][x * 4 + 3]
        if self.ch == 2:
            return self.rows[y][x * 2 + 1]
        return 255

    def save(self, path):
        write_png(path, self.w, self.h, self.rows, self.ch)
