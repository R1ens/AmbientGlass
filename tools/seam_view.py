"""Renders a picture with the real glow pipeline and looks closely at the seam between picture and glow.

    python tools/seam_view.py INPUT.png|synthetic:NAME OUTDIR [options]

Draws the picture centred in an ultrawide output (3840x1080 by default, like fullscreen VLC on a 32:9
monitor), pastes the picture into the transparent hole as VLC would show it, and writes:

  OUTDIR/<name>_comp.png   the whole composite
  OUTDIR/<name>_crop.png   a magnified crop around the chosen seam
and prints the luma of the pixels around the seam on a few rows.

Options:
  --exe PATH        VlcAmbient.exe (default: build/VlcAmbient.exe next to this folder)
  --size WxH        output size (default 3840x1080)
  --side L|R        which seam to crop and profile (default R)
  --y Y             crop centre row (default: picture middle)
  --crop W,H,S      crop width and height in px around the seam and magnification (default 160,240,2)
  --rows Y,Y,...    rows to print profiles for (default: 100, middle, height-100)
  --dark-rim PCT    darken the outermost picture column to PCT % (VLC's half-covered edge), e.g. 50
  --cover PX        make the hole PX narrower on each side (the overlay's seam cover), e.g. 1
  --set KEY=VALUE   setting override, repeatable (debanding=0 is always set for clean profiles)

synthetic:NAME uses a generator from tests/quality/inputs.py (lum_x, flatgrey, symx, diag, stripes16,
stripes64, hstripes6, flatcolor) at 1920x1080. Standard library only.
"""
import argparse
import os
import subprocess
import sys

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
sys.path.insert(0, os.path.join(ROOT, "tests", "quality"))
import inputs  # noqa: E402
from png import Image, write_png  # noqa: E402


def luma(c):
    return 0.2126 * c[0] + 0.7152 * c[1] + 0.0722 * c[2]


def load_input(spec, out_dir):
    if spec.startswith("synthetic:"):
        name = spec.split(":", 1)[1]
        if name not in inputs.ALL:
            sys.exit("unknown synthetic input %r; known: %s" % (name, ", ".join(sorted(inputs.ALL))))
        w, h = 1920, 1080
        path = os.path.join(out_dir, "%s_%dx%d.png" % (name, w, h))
        if not os.path.exists(path):
            write_png(path, w, h, inputs.ALL[name](w, h), 3)
        return name, path
    return os.path.splitext(os.path.basename(spec))[0], spec


def darken_rim(path, pct, out_dir, name):
    img = Image.load(path)
    rows = []
    for r in img.rows:
        r = bytearray(r)
        for x in (0, img.w - 1):
            for c in range(3):
                r[x * img.ch + c] = r[x * img.ch + c] * pct // 100
        rows.append(bytes(r))
    out = os.path.join(out_dir, "%s_rim%d.png" % (name, pct))
    write_png(out, img.w, img.h, rows, img.ch)
    return out


def main():
    ap = argparse.ArgumentParser(description=__doc__.split("\n")[0])
    ap.add_argument("input")
    ap.add_argument("out_dir")
    ap.add_argument("--exe", default=os.path.join(ROOT, "build", "VlcAmbient.exe"))
    ap.add_argument("--size", default="3840x1080")
    ap.add_argument("--side", choices=("L", "R"), default="R")
    ap.add_argument("--y", type=int, default=None)
    ap.add_argument("--crop", default="160,240,2")
    ap.add_argument("--rows", default=None)
    ap.add_argument("--dark-rim", type=int, default=None)
    ap.add_argument("--cover", type=int, default=0)
    ap.add_argument("--set", action="append", default=[])
    a = ap.parse_args()

    os.makedirs(a.out_dir, exist_ok=True)
    name, path = load_input(a.input, a.out_dir)
    if a.dark_rim is not None:
        path = darken_rim(path, a.dark_rim, a.out_dir, name)
        name += "_rim%d" % a.dark_rim
    pic = Image.load(path)
    W, H = (int(v) for v in a.size.lower().split("x"))
    if pic.w > W or pic.h > H:
        sys.exit("the picture (%dx%d) does not fit the output (%dx%d)" % (pic.w, pic.h, W, H))
    l, t = (W - pic.w) // 2, (H - pic.h) // 2
    frame = (l, t, l + pic.w, t + pic.h)
    c = a.cover
    hole = (l + c, t, l + pic.w - c, t + pic.h) if c else frame

    render = os.path.join(a.out_dir, name + "_glow.png")
    args = [a.exe, "--render", path, render, "--size", "%dx%d" % (W, H), "--frame", "%d,%d,%d,%d" % frame,
            "--hole", "%d,%d,%d,%d" % hole, "--opaque", "--seed", "0", "--set", "debanding=0"]
    for s in a.set:
        args += ["--set", s]
    r = subprocess.run(args)
    if r.returncode != 0:
        sys.exit("render failed (exit %d): %s" % (r.returncode, " ".join(args)))

    # Composite: the picture shows through the transparent hole, as on screen.
    g = Image.load(render)
    if g.ch != 4:
        sys.exit("expected an RGBA render, got %d channels" % g.ch)
    rows = []
    n = hole[2] - hole[0]
    for y in range(H):
        src = g.rows[y]
        row = bytearray(W * 3)
        row[0::3], row[1::3], row[2::3] = src[0::4], src[1::4], src[2::4]
        if hole[1] <= y < hole[3]:
            p = pic.rows[y - t]
            i = (hole[0] - l) * pic.ch
            seg = bytearray(n * 3)
            for k in range(3):
                seg[k::3] = p[i + k:i + n * pic.ch:pic.ch]
            row[hole[0] * 3:hole[2] * 3] = seg
        rows.append(bytes(row))
    comp = Image(W, H, 3, rows)
    comp_path = os.path.join(a.out_dir, name + "_comp.png")
    comp.save(comp_path)

    cw, ch, scale = (int(v) for v in a.crop.split(","))
    seam = frame[2] if a.side == "R" else frame[0]
    cy = a.y if a.y is not None else (t + frame[3]) // 2
    x0, y0 = max(0, seam - cw // 2), max(0, cy - ch // 2)
    x1, y1 = min(W, x0 + cw), min(H, y0 + ch)
    crop_rows = []
    for y in range(y0, y1):
        src = rows[y][x0 * 3:x1 * 3]
        line = bytearray()
        for x in range(x1 - x0):
            line += src[x * 3:x * 3 + 3] * scale
        crop_rows.extend([bytes(line)] * scale)
    crop_path = os.path.join(a.out_dir, name + "_crop.png")
    write_png(crop_path, (x1 - x0) * scale, (y1 - y0) * scale, crop_rows, 3)

    prof_rows = [int(v) for v in a.rows.split(",")] if a.rows else [t + 100, cy, frame[3] - 100]
    first = seam if a.side == "R" else seam - 1
    print("luma around the %s seam, from the picture outward (x = %d is the first glow px); "
          "px inside the picture rect are marked with |" % (a.side, first))
    for y in prof_rows:
        xs = range(seam - 6, seam + 6) if a.side == "R" else range(seam + 5, seam - 7, -1)
        vals = []
        for x in xs:
            v = "%3d" % round(luma(rows[y][x * 3:x * 3 + 3]))
            inside = l <= x < frame[2]
            vals.append(("|" if inside else " ") + v)
        print("  row %4d: %s" % (y, " ".join(vals)))
    print(comp_path)
    print(crop_path)


if __name__ == "__main__":
    main()
