"""Deterministic synthetic input pictures for the glow quality checks (standard library only).

    python tests/quality/inputs.py <out_dir> [name ...] [--size WxH]

Every generator is a function of (w, h) returning a list of h RGB rows (bytes, 3*w long). The
pictures are built row-wise (most of them vary along one axis only) so that 1920x1080 inputs take
a fraction of a second. Values are 8-bit sRGB.
"""
import colorsys
import os
import random
import sys

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from png import write_png  # noqa: E402


def _clamp8(v):
    return max(0, min(255, int(round(v))))


def _row_from_cols(cols):
    """cols: list of (r, g, b) per x -> one RGB row."""
    return bytes(_clamp8(c) for px in cols for c in px)


def lum_x(w, h):
    """Smooth horizontal luminance ramp: bright warm edge column (230) ramping darker over the
    outer 15% of the width, constant along y. Probes the near-field transition shape."""
    cols = []
    for x in range(w):
        v = 230 - 200 * min(1.0, x / (0.15 * w))
        cols.append((v, v * 0.8, v * 0.5))
    row = _row_from_cols(cols)
    return [row] * h


def flatgrey(w, h):
    """Flat mid grey (128): glow profile reflects only the falloff curve."""
    row = bytes([128, 128, 128]) * w
    return [row] * h


def symx(w, h):
    """Mirror-symmetric in x (column x equals column w-1-x): bright edges ramping darker inwards,
    multiplied by a hue that changes along y, so both edges carry distinct colours."""
    v = [230 - 200 * min(1.0, min(x, w - 1 - x) / (0.06 * w)) for x in range(w)]
    rows = []
    for y in range(h):
        r, g, b = colorsys.hsv_to_rgb(y / h * 0.8, 0.6, 1.0)
        rows.append(bytes(_clamp8(c) for vx in v for c in (vx * r, vx * g, vx * b)))
    return rows


def _const_row_image(w, h, val_of_y):
    rows = []
    for y in range(h):
        v = _clamp8(val_of_y(y))
        rows.append(bytes([v, v, v]) * w)
    return rows


def hstripes6(w, h):
    """Hard horizontal stripes with a 6 px period (3 px at 230, 3 px at 20), constant along x."""
    return _const_row_image(w, h, lambda y: 230 if (y // 3) % 2 == 0 else 20)


def _value_noise(cell, seed):
    def make(w, h):
        rnd = random.Random(seed)
        vals = [rnd.randint(20, 235) for _ in range(h // cell + 2)]

        def f(y):
            t = y / cell
            i = int(t)
            a = t - i
            a = a * a * (3 - 2 * a)
            return int(vals[i] * (1 - a) + vals[i + 1] * a)
        return _const_row_image(w, h, f)
    make.__doc__ = ("Grey horizontal bands about %d px tall (smoothstep value noise along y, seed %d), "
                    "constant along x: detail that varies along the left/right edges." % (cell, seed))
    return make


stripes16 = _value_noise(16, 3)
stripes64 = _value_noise(64, 3)


def flatcolor(w, h):
    """Flat saturated colour field (teal)."""
    row = bytes([40, 150, 170]) * w
    return [row] * h


def diag(w, h):
    """Paper white above, sky blue below a boundary that slopes up toward the middle (1 px up per
    2 px in) and meets both side edges at mid height, unaliased over 1 px. A sharp edge reaching the
    border diagonally: the glow must spread it as one soft cone, not as a fan of copies (rays)."""
    top, bot = (236, 234, 228), (110, 175, 205)
    rows = []
    for y in range(h):
        cols = []
        for x in range(w):
            yb = h / 2 - 0.5 * min(x + 0.5, w - x - 0.5)
            t = max(0.0, min(1.0, y + 0.5 - yb + 0.5))  # 0 above, 1 below, linear over 1 px
            cols.append(tuple(a + (b - a) * t for a, b in zip(top, bot)))
        rows.append(_row_from_cols(cols))
    return rows


ALL = {
    "diag": diag,
    "lum_x": lum_x,
    "flatgrey": flatgrey,
    "symx": symx,
    "hstripes6": hstripes6,
    "stripes16": stripes16,
    "stripes64": stripes64,
    "flatcolor": flatcolor,
}


def make(name, out_dir, w, h):
    """Writes <out_dir>/<name>_<w>x<h>.png if missing; returns (path, rows)."""
    os.makedirs(out_dir, exist_ok=True)
    rows = ALL[name](w, h)
    path = os.path.join(out_dir, "%s_%dx%d.png" % (name, w, h))
    if not os.path.exists(path):
        write_png(path, w, h, rows, 3)
    return path, rows


if __name__ == "__main__":
    args = sys.argv[1:]
    size = (1920, 1080)
    if "--size" in args:
        i = args.index("--size")
        size = tuple(int(v) for v in args[i + 1].lower().split("x"))
        del args[i:i + 2]
    if not args:
        print(__doc__)
        sys.exit(2)
    out = args[0]
    for n in args[1:] or list(ALL):
        p, _ = make(n, out, *size)
        print("wrote", p)
