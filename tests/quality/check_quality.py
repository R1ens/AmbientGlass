"""Quantitative image-quality regression checks for the glow renderer (standard library only).

    python tests\\quality\\check_quality.py [--exe build\\VlcAmbient.exe] [--work DIR] [--keep] [--self-test]

Renders synthetic pictures with `VlcAmbient.exe --render` (WARP, headless), pastes the picture into
the transparent hole of each output and measures the glow next to the picture. Prints one line per
measurement (value vs threshold) and exits 1 if any check fails, 2 on a usage/render error.

--self-test runs the same measurements on "ideal" outputs built in Python instead of the renderer's
own near field (see IDEAL below); every check must pass there, which shows the thresholds are
reachable. It still needs the exe for the far-field glow.

Geometry ("BIG"): a 1920x1080 picture centred in a 3840x1080 output, --opaque, --seed 0,
debanding=0, so outputs are deterministic and dither-free. Luminance is Rec.709 luma of the 8-bit
sRGB values ("levels", 0..255). k is the distance in px from the picture edge outward: k = 0 is the
first glow pixel, the "edge" is the last picture pixel on that row.
"""
import argparse
import math
import os
import shutil
import subprocess
import sys
import tempfile
import time

HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, HERE)
import inputs  # noqa: E402
from png import Image  # noqa: E402

# ---------------------------------------------------------------------------------------------
# Thresholds (levels = 8-bit sRGB code values; luminance = Rec.709 luma of them)
# ---------------------------------------------------------------------------------------------

# a) transition_monotonic -- guards defect 1 (near-field shelf / trough between picture and glow).
# Max rise of the profile above its running minimum, anywhere from the seam outward: the glow of a
# flat or edge-bright picture must never get brighter again after it has started to fall.
T_RISE = 2.0
# Kink: second difference of 6-px window means, D2 = A[i-1] - 2 A[i] + A[i+1] (A[i] = mean of
# L[6i..6i+5]), over the transition zone k < 1.25 T (T = transition width). The allowed curvature
# scales with the drop the transition has to make: a smoothstep covering a drop dL over T px needs
# |D2| <= 6 dL * 36 / T^2; we allow T_KINK_REL times that, but never less than T_KINK_ABS (8-bit
# rounding plus the far field's own curvature). 2.0 leaves room for a smooth C2 ease compounded with
# the picture's own gradient (measured up to 1.6x on lum_x blur=100,spread=60), while the slope breaks
# this check exists for measured 2.4x-11x before the stage-1 fix (lum_x default 8.62 vs 3.03 base).
T_KINK_ABS = 2.0
T_KINK_REL = 2.0
# Shelf: the descent per 6-px window, M[i] = A[i] - A[i+1], must be unimodal over the zone (rise
# to a peak, then fall). A shelf is a dip of M below both its earlier and its later maximum:
# shelf = max_j (min(max M[<j], max M[>j]) - M[j]), in levels per 6 px. A trough (M < 0) counts too.
T_SHELF = 1.5
# Seam step |first glow px - last picture px| on smooth pictures with edgeBlend >= 40.
T_SEAM = 4.0

# b) left_right_symmetry -- guards defect 2 (left/right asymmetry, dependence on the output width).
# Max per-channel |left glow - mirrored right glow| on a mirror-symmetric picture.
T_SYM = 3
# Max per-channel difference at equal offsets from the seam between a 3700- and a 3840-wide output.
T_WIDTH = 3

# c) edge_detail_fidelity -- guards defect 3 (first glow column ignores detail along the edge).
# RMS over y of L(first glow column) - L(last picture column), for bands of the given height.
# Current build (defaults): 32.3 for 16 px, 4.3 for 64 px; copying the edge into the glow gives ~0.
T_DETAIL_RMS = {16: 12.0, 64: 2.0}
# Hard 3-px-on/3-px-off stripes: reported only (INFO), not gating -- a fix may legitimately
# low-pass detail that fine.
INFO_DETAIL_6PX = True

# d) negative_fade_start -- guards defect 4 (fadeStart < 0 cuts a dark step into the seam).
T_NEGFADE_SEAM = 4.0
# f) edge_rays: across a sharp diagonal boundary that meets the border, the glow k px out must change
# like one gaussian blur of the boundary along the border (sigma = max(4, k), as the shader intends).
# Sparse sharp taps instead repeat the boundary as a fan of copies (rays): the profile becomes a
# staircase whose single-row jumps far exceed a gaussian's steepest slope, total / (sqrt(2 pi) sigma).
# Measured before the fix: 2.5-5.9; after: <= 1.4. blur=0 is left out: the user asked for sharpness.
T_RAY_JUMP = 2.0
RAY_K = (2, 4, 6, 9, 12, 16)              # px outside the picture (the full-res zone is ~13 px wide)
RAY_CASES = [("default", ()), ("edgeBlend=100", ("edgeBlend=100",))]
RAY_HALF = 120                            # rows each side of mid height

# ---------------------------------------------------------------------------------------------
# Geometry and cases
# ---------------------------------------------------------------------------------------------

PW, PH = 1920, 1080                       # picture
OUT_W, OUT_H = 3840, 1080                 # output
FRAME = (960, 0, 2880, 1080)              # picture rect inside the output
ALT_W = 3700                              # second output width for the width-independence check
ALT_FRAME = ((ALT_W - PW) // 2, 0, (ALT_W - PW) // 2 + PW, PH)
MID_Y = PH // 2
K_MAX = 600                               # px of glow compared per side (default reach ~407 px)
SYM_ROW_STEP = 8                          # rows sampled for symmetry/width comparisons
DETAIL_Y = (100, PH - 100)                # rows used for the seam RMS (away from the corners)
BASE_SETS = ("debanding=0",)
RENDER_TIMEOUT_S = 120

DEFAULTS = {"blur": 30, "edge": 12, "spread": 17, "fadeStart": 15, "fadeCurve": 35, "edgeBlend": 40}

TRANSITION_CASES = [  # (label, sets)
    ("default", ()),
    ("blur=100", ("blur=100",)),
    ("blur=100,spread=60", ("blur=100", "spread=60")),
    ("edgeBlend=100", ("edgeBlend=100",)),
    ("spread=5,edgeBlend=100", ("spread=5", "edgeBlend=100")),
]
TRANSITION_INPUTS = ("lum_x", "flatgrey")
SYMMETRY_CASES = [
    ("default", ()),
    ("edgeBlend=0", ("edgeBlend=0",)),
    ("edgeBlend=0,blur=0", ("edgeBlend=0", "blur=0")),
    ("edge=2", ("edge=2",)),
]
WIDTH_CASES = [("default", ()), ("edgeBlend=0", ("edgeBlend=0",))]
DETAIL_CASES = [("default", ()), ("edgeBlend=100", ("edgeBlend=100",)), ("blur=100", ("blur=100",))]
NEGFADE_CASES = [("fadeStart=-50", ("fadeStart=-50",))]   # edgeBlend stays at its default 40
NEGFADE_INPUTS = ("flatgrey", "lum_x")


def setting(sets, key):
    for s in sets:
        k, _, v = s.partition("=")
        if k == key:
            return int(v)
    return DEFAULTS[key]


def transition_px(sets):
    """Mirror of glow_math.cpp: transitionPx = edgeBlend / 100 * 0.2 * picture height."""
    return setting(sets, "edgeBlend") / 100.0 * 0.2 * PH


def case_name(inp, sets, width=OUT_W):
    name = inp + ("" if not sets else "__" + "_".join(s.replace("=", "") for s in sets))
    return name + ("" if width == OUT_W else "__w%d" % width)


# ---------------------------------------------------------------------------------------------
# Rendering, compositing
# ---------------------------------------------------------------------------------------------

class RenderError(Exception):
    pass


class Renderer:
    """Renders cases with the exe; caches by case name (in memory and in <work>/renders) per run."""

    def __init__(self, exe, work):
        self.exe = exe
        self.work = work
        self.in_dir = os.path.join(work, "inputs")
        self.out_dir = os.path.join(work, "renders")
        shutil.rmtree(self.out_dir, ignore_errors=True)  # never measure renders of an older exe
        os.makedirs(self.out_dir, exist_ok=True)
        self.pics = {}
        self.cache = {}
        self.count = 0
        self.seconds = 0.0

    def picture(self, inp):
        if inp not in self.pics:
            rows = inputs.ALL[inp](PW, PH)
            path = os.path.join(self.in_dir, "%s_%dx%d.png" % (inp, PW, PH))
            os.makedirs(self.in_dir, exist_ok=True)
            Image(PW, PH, 3, rows).save(path)
            self.pics[inp] = (path, Image(PW, PH, 3, rows))
        return self.pics[inp]

    def render(self, inp, sets=(), width=OUT_W):
        """Returns the composite (picture pasted into the hole) as an RGBA Image."""
        name = case_name(inp, sets, width)
        if name in self.cache:
            return self.cache[name]
        path, pic = self.picture(inp)
        frame = FRAME if width == OUT_W else ALT_FRAME
        out = os.path.join(self.out_dir, name + ".png")
        args = [self.exe, "--render", path, out, "--size", "%dx%d" % (width, OUT_H),
                "--frame", "%d,%d,%d,%d" % frame, "--opaque", "--seed", "0"]
        for s in tuple(BASE_SETS) + tuple(sets):
            args += ["--set", s]
        t = time.time()
        try:
            r = subprocess.run(args, capture_output=True, timeout=RENDER_TIMEOUT_S)
        except subprocess.TimeoutExpired:
            raise RenderError("render timed out after %d s: %s" % (RENDER_TIMEOUT_S, " ".join(args)))
        self.seconds += time.time() - t
        self.count += 1
        if r.returncode != 0 or not os.path.exists(out):
            raise RenderError("render failed (exit %d): %s" % (r.returncode, " ".join(args)))
        img = Image.load(out)
        if (img.w, img.h, img.ch) != (width, OUT_H, 4):
            raise RenderError("%s: unexpected output %dx%d with %d channels" % (out, img.w, img.h, img.ch))
        comp = composite(img, pic, frame)
        self.cache[name] = comp
        return comp


def composite(img, pic, frame):
    """Pastes the RGB picture into the frame of an RGBA render (whose hole must be transparent)."""
    l, t, r, b = frame
    y = (t + b) // 2
    if img.alpha(l, y) != 0 or img.alpha(l - 1, y) != 255 or img.alpha(r, y) != 255:
        raise RenderError("hole not where expected: alpha %d/%d/%d at x=%d/%d/%d"
                          % (img.alpha(l - 1, y), img.alpha(l, y), img.alpha(r, y), l - 1, l, r))
    rows = []
    n = r - l
    for yy in range(img.h):
        row = bytearray(img.rows[yy])
        if t <= yy < b:
            src = pic.rows[yy - t]
            seg = bytearray(n * 4)
            seg[0::4] = src[0::3]
            seg[1::4] = src[1::3]
            seg[2::4] = src[2::3]
            seg[3::4] = b"\xff" * n
            row[l * 4:r * 4] = seg
        rows.append(row)
    return Image(img.w, img.h, 4, rows)


# ---------------------------------------------------------------------------------------------
# Measurements (work on composites)
# ---------------------------------------------------------------------------------------------

def luma(c):
    return 0.2126 * c[0] + 0.7152 * c[1] + 0.0722 * c[2]


def glow_x(frame, side, k):
    return frame[0] - 1 - k if side == "L" else frame[2] + k


def edge_x(frame, side):
    return frame[0] if side == "L" else frame[2] - 1


def profile(comp, frame, y, side, n):
    """(edge luminance, [glow luminance for k = 0..n-1]) along row y, outward from the seam."""
    n = min(n, frame[0] if side == "L" else comp.w - frame[2])
    e = luma(comp.rgb(edge_x(frame, side), y))
    return e, [luma(comp.rgb(glow_x(frame, side, k), y)) for k in range(n)]


def max_rise(G):
    """max over k of G[k] - min(G[0..k-1]), and where."""
    best, at, lo = 0.0, 0, G[0]
    for k in range(1, len(G)):
        if G[k] - lo > best:
            best, at = G[k] - lo, k
        lo = min(lo, G[k])
    return best, at


def max_kink(G, T):
    """max |D2| of 6-px window means over the zone k < 1.25 T; returns (value, k centre)."""
    A = [sum(G[6 * i:6 * i + 6]) / 6.0 for i in range(len(G) // 6)]
    last = min(len(A) - 2, int(math.ceil(1.25 * T / 6.0)))
    best, at = 0.0, 0
    for i in range(1, last + 1):
        d = A[i - 1] - 2 * A[i] + A[i + 1]
        if abs(d) > abs(best):
            best, at = d, 6 * i + 3
    return best, at


def max_shelf(G, T):
    """Slope-dip metric over the zone k < 1.25 T (see T_SHELF); returns (value, k)."""
    A = [sum(G[6 * i:6 * i + 6]) / 6.0 for i in range(len(G) // 6)]
    n = min(len(A) - 1, int(math.ceil(1.25 * T / 6.0)) + 1)
    M = [A[i] - A[i + 1] for i in range(n)]
    if n < 3:
        return 0.0, 0
    suffix = [0.0] * n
    suffix[n - 1] = M[n - 1]
    for i in range(n - 2, -1, -1):
        suffix[i] = max(M[i], suffix[i + 1])
    best, at, run_max = 0.0, 0, M[0]
    for j in range(1, n - 1):
        v = min(run_max, suffix[j + 1]) - M[j]
        if v > best:
            best, at = v, 6 * j + 6
        run_max = max(run_max, M[j])
    return best, at


def kink_threshold(E, G, T):
    drop = abs(E - G[min(len(G) - 1, int(T))])
    return max(T_KINK_ABS, T_KINK_REL * 6.0 * drop * 36.0 / (T * T))


def mirror_diff(comp, frame, rows, k_max):
    """Max per-channel |left glow - mirrored right glow| over rows and k < k_max; (diff, k, y)."""
    l, r = frame[0], frame[2]
    k_max = min(k_max, l, comp.w - r)
    best = (0, 0, 0)
    for y in rows:
        row = comp.rows[y]
        for k in range(k_max):
            i, j = (l - 1 - k) * 4, (r + k) * 4
            d = max(abs(row[i] - row[j]), abs(row[i + 1] - row[j + 1]), abs(row[i + 2] - row[j + 2]))
            if d > best[0]:
                best = (d, k, y)
    return best


def offset_diff(a, fa, b, fb, rows, k_max):
    """Max per-channel difference between two composites at equal offsets from the seam."""
    k_max = min(k_max, fa[0], fb[0], a.w - fa[2], b.w - fb[2])
    best = (0, 0, 0, "")
    for y in rows:
        ra, rb = a.rows[y], b.rows[y]
        for side in ("L", "R"):
            for k in range(k_max):
                i, j = glow_x(fa, side, k) * 4, glow_x(fb, side, k) * 4
                d = max(abs(ra[i] - rb[j]), abs(ra[i + 1] - rb[j + 1]), abs(ra[i + 2] - rb[j + 2]))
                if d > best[0]:
                    best = (d, k, y, side)
    return best


def seam_rms(comp, frame, side, y0, y1):
    e = [luma(comp.rgb(glow_x(frame, side, 0), y)) - luma(comp.rgb(edge_x(frame, side), y)) for y in range(y0, y1)]
    return math.sqrt(sum(v * v for v in e) / len(e)), max(abs(v) for v in e)


# ---------------------------------------------------------------------------------------------
# Checks
# ---------------------------------------------------------------------------------------------

class Results:
    def __init__(self):
        self.rows = []

    def add(self, check, case, metric, value, limit, where="", gating=True):
        value = round(value, 4)
        status = ("PASS" if value <= limit + 1e-9 else "FAIL") if gating else "INFO"
        self.rows.append((check, case, metric, value, limit, status, where))
        lim = "%6.2f" % limit if gating else "     -"
        print("%-4s %-21s %-36s %-20s %8.2f <= %s  %s" % (status, check, case, metric, value, lim, where), flush=True)

    def failed(self):
        return [r for r in self.rows if r[5] == "FAIL"]


def transition_monotonic(src, res):
    """Defect 1: profile from the seam outward must not dip and come back (trough) or kink (shelf)."""
    for inp in TRANSITION_INPUTS:
        for label, sets in TRANSITION_CASES:
            comp = src.get(inp, sets)
            T = transition_px(sets)
            case = "%s %s" % (inp, label)
            worst_rise = worst_kink = worst_shelf = worst_seam = None
            for side in ("L", "R"):
                E, G = profile(comp, FRAME, MID_Y, side, FRAME[0] - 60)
                rise, rk = max_rise(G)
                kink, kk = max_kink(G, T)
                lim = kink_threshold(E, G, T)
                shelf, sk = max_shelf(G, T)
                seam = abs(G[0] - E)
                if worst_rise is None or rise > worst_rise[0]:
                    worst_rise = (rise, "%s k=%d" % (side, rk))
                if worst_kink is None or abs(kink) / lim > abs(worst_kink[0]) / worst_kink[1]:
                    worst_kink = (kink, lim, "%s k~%d (%+.2f)" % (side, kk, kink))
                if worst_shelf is None or shelf > worst_shelf[0]:
                    worst_shelf = (shelf, "%s k~%d" % (side, sk))
                if worst_seam is None or seam > worst_seam[0]:
                    worst_seam = (seam, "%s edge %.0f glow %.0f" % (side, E, G[0]))
            res.add("transition_monotonic", case, "rise after fall", worst_rise[0], T_RISE, worst_rise[1])
            res.add("transition_monotonic", case, "kink |D2|", abs(worst_kink[0]), worst_kink[1], worst_kink[2])
            res.add("transition_monotonic", case, "shelf (slope dip)", worst_shelf[0], T_SHELF, worst_shelf[1])
            if setting(sets, "edgeBlend") >= 40:
                res.add("transition_monotonic", case, "seam step", worst_seam[0], T_SEAM, worst_seam[1])


def left_right_symmetry(src, res):
    """Defect 2: mirror-symmetric picture -> mirror-symmetric glow, independent of output width."""
    rows = range(SYM_ROW_STEP // 2, OUT_H, SYM_ROW_STEP)
    for label, sets in SYMMETRY_CASES:
        comp = src.get("symx", sets)
        d, k, y = mirror_diff(comp, FRAME, rows, K_MAX)
        res.add("left_right_symmetry", "symx " + label, "max |L - mirror R|", d, T_SYM, "k=%d y=%d" % (k, y))
    for label, sets in WIDTH_CASES:
        a = src.get("symx", sets)
        b = src.get("symx", sets, ALT_W)
        d, k, y, side = offset_diff(a, FRAME, b, ALT_FRAME, rows, K_MAX)
        res.add("left_right_symmetry", "symx %s %d vs %d" % (label, ALT_W, OUT_W), "max |w%d - w%d|" % (ALT_W, OUT_W),
                d, T_WIDTH, "%s k=%d y=%d" % (side, k, y))


def edge_detail_fidelity(src, res):
    """Defect 3: the first glow column must follow detail along the picture edge (edgeBlend >= 40)."""
    for size, inp in ((16, "stripes16"), (64, "stripes64")):
        for label, sets in DETAIL_CASES:
            if setting(sets, "edgeBlend") < 40:
                continue
            comp = src.get(inp, sets)
            worst = max((seam_rms(comp, FRAME, side, *DETAIL_Y) + (side,) for side in ("L", "R")))
            lim = T_DETAIL_RMS[size]
            res.add("edge_detail_fidelity", "%s %s" % (inp, label), "seam RMS", worst[0], lim,
                    "%s max %.0f" % (worst[2], worst[1]))
    if INFO_DETAIL_6PX:
        comp = src.get("hstripes6", ())
        worst = max((seam_rms(comp, FRAME, side, *DETAIL_Y) + (side,) for side in ("L", "R")))
        res.add("edge_detail_fidelity", "hstripes6 default", "seam RMS", worst[0], 0.0,
                "%s max %.0f (not gating)" % (worst[2], worst[1]), gating=False)


def negative_fade_start(src, res):
    """Defect 4: fadeStart < 0 with the transition on must not cut a step into the seam."""
    for inp in NEGFADE_INPUTS:
        for label, sets in NEGFADE_CASES:
            comp = src.get(inp, sets)
            worst = None
            for side in ("L", "R"):
                E, G = profile(comp, FRAME, MID_Y, side, 8)
                step = G[0] - E
                if worst is None or abs(step) > abs(worst[0]):
                    worst = (step, "%s edge %.0f glow %.0f" % (side, E, G[0]))
            res.add("negative_fade_start", "%s %s,edgeBlend=%d" % (inp, label, setting(sets, "edgeBlend")),
                    "seam step", abs(worst[0]), T_NEGFADE_SEAM, worst[1])


def jump_ratio(vals, sigma):
    """Steepest single-row change over the steepest slope of a gaussian step of the same height."""
    total = abs(vals[-1] - vals[0])
    if total < 8:
        return 0.0
    jump = max(abs(b - a) for a, b in zip(vals, vals[1:]))
    return jump / (total / (math.sqrt(2 * math.pi) * sigma))


def edge_rays(src, res):
    """Defect 5: a sharp boundary meeting the border must spread as one soft cone, not as rays."""
    if not isinstance(src, RenderSource):
        return  # the ideal outputs blur the edge by a fixed 1.5 px, not by the distance
    for label, sets in RAY_CASES:
        comp = src.get("diag", sets)
        for k in RAY_K:
            worst = (-1.0, "")
            for side in ("L", "R"):
                x = glow_x(FRAME, side, k)
                vals = [luma(comp.rows[y][x * 4:x * 4 + 3]) for y in range(MID_Y - RAY_HALF, MID_Y + RAY_HALF)]
                worst = max(worst, (jump_ratio(vals, max(4.0, k)), side))
            res.add("edge_rays", "diag %s, %d px out" % (label, k), "jump / gaussian", worst[0], T_RAY_JUMP,
                    worst[1])


# e) temporal_response -- the glow over time, rendered with --prev/--steps (renderer mode only).
# Ordinary changes must not lag; a scene change (cut, seek) must ease in instead of flashing, and finish.
T_STEP_LAG = 0.05        # small change (128 -> 136 grey), one 60 Hz frame later: 1 - progress
T_CUT_FIRST = 0.15       # red -> blue cut, one frame later: progress (more = a flash)
T_CUT_DONE = 0.05        # same cut 300 ms later: 1 - progress
T_CUT_OFF_LAG = 0.02     # cutFadeMs=0: one frame later: 1 - progress (instant, as before the fade existed)


def temporal_response(src, res):
    if not isinstance(src, RenderSource):
        return  # the ideal outputs are single frames
    r = src.r
    w, h, out_w = 320, 180, 640
    probe = (out_w - w) // 2 - 10, h // 2  # 10 px left of the picture

    def flat(name, rgb):
        path = os.path.join(r.in_dir, "flat_%s.png" % name)
        os.makedirs(r.in_dir, exist_ok=True)
        Image(w, h, 3, [bytearray(bytes(rgb) * w) for _ in range(h)]).save(path)
        return path

    def glow(prev, cur, steps, sets=()):
        out = os.path.join(r.out_dir, "temporal.png")
        args = [r.exe, "--render", cur, out, "--size", "%dx%d" % (out_w, h), "--opaque", "--seed", "0",
                "--prev", prev, "--steps", str(steps), "--dt", "16.7"]
        for s in tuple(BASE_SETS) + tuple(sets):
            args += ["--set", s]
        t = time.time()
        p = subprocess.run(args, capture_output=True, timeout=RENDER_TIMEOUT_S)
        r.seconds += time.time() - t
        r.count += 1
        if p.returncode != 0:
            raise RenderError("render failed (exit %d): %s" % (p.returncode, " ".join(args)))
        img = Image.load(out)
        i = probe[0] * 4
        return list(img.rows[probe[1]][i:i + 3])

    def progress(a, b, c):
        num = sum((cc - aa) * (bb - aa) for aa, bb, cc in zip(a, b, c))
        return num / max(1e-9, sum((bb - aa) ** 2 for aa, bb in zip(a, b)))

    grey, grey2 = flat("grey", (128, 128, 128)), flat("grey2", (136, 136, 136))
    red, blue = flat("red", (200, 30, 30)), flat("blue", (30, 30, 200))
    g0, g1 = glow(grey, grey, 1), glow(grey2, grey2, 1)
    r0, b0 = glow(red, red, 1), glow(blue, blue, 1)
    res.add("temporal_response", "grey 128->136, 1 frame", "lag (1-progress)",
            1 - progress(g0, g1, glow(grey, grey2, 1)), T_STEP_LAG)
    res.add("temporal_response", "cut red->blue, 1 frame", "progress (flash)",
            progress(r0, b0, glow(red, blue, 1)), T_CUT_FIRST)
    res.add("temporal_response", "cut red->blue, 300 ms", "1-progress",
            1 - progress(r0, b0, glow(red, blue, 18)), T_CUT_DONE)
    res.add("temporal_response", "cut, cutFadeMs=0, 1 frame", "lag (1-progress)",
            1 - progress(r0, b0, glow(red, blue, 1, ("cutFadeMs=0",))), T_CUT_OFF_LAG)


CHECKS = (transition_monotonic, left_right_symmetry, edge_detail_fidelity, negative_fade_start, edge_rays,
          temporal_response)


# ---------------------------------------------------------------------------------------------
# Sources: the renderer itself, or ideal outputs built from its far field (--self-test)
# ---------------------------------------------------------------------------------------------

class RenderSource:
    def __init__(self, renderer):
        self.r = renderer

    def get(self, inp, sets, width=OUT_W):
        return self.r.render(inp, sets, width)


IDEAL_EDGE_SIGMA = 1.5  # px, vertical blur of the edge colour in the ideal outputs


def smoothstep(t):
    t = min(1.0, max(0.0, t))
    return t * t * (3 - 2 * t)


class IdealSource:
    """IDEAL: what a seamless renderer could output. Far field F = the renderer's own output for the
    same settings but edgeBlend=0 (no transition). Within the transition width T the glow is
    w(k) * edge colour of that row + (1 - w(k)) * F(k), w = 1 - smoothstep(k / T), per channel, so it
    starts at the picture edge and eases into the far field with zero slope at both ends. The edge
    colour is the last picture column lightly blurred along y (gaussian, IDEAL_EDGE_SIGMA px), as a
    real renderer reading a low mip would, so the seam checks are not met only by an exact copy.
    Mirror-symmetric pictures get the left glow mirrored to the right; the alternative output width
    reuses the main output at equal offsets from the seam (an ideal renderer depends on neither)."""

    def __init__(self, renderer):
        self.r = renderer
        self.cache = {}

    def get(self, inp, sets, width=OUT_W):
        key = case_name(inp, sets, width)
        if key in self.cache:
            return self.cache[key]
        if width != OUT_W:
            img = self.shift(self.get(inp, sets), width)
        else:
            far_sets = tuple(s for s in sets if not s.startswith("edgeBlend=")) + ("edgeBlend=0",)
            img = self.blend(self.r.render(inp, far_sets), transition_px(sets))
            if inp == "symx":
                img = self.mirror(img)
        self.cache[key] = img
        return img

    @staticmethod
    def blurred_edge(img, side):
        """Edge column colours per frame row, gaussian-blurred along y (clamped at the frame)."""
        t, b = FRAME[1], FRAME[3]
        ei = edge_x(FRAME, side) * 4
        col = [img.rows[y][ei:ei + 3] for y in range(t, b)]
        r = int(math.ceil(3 * IDEAL_EDGE_SIGMA))
        ker = [math.exp(-0.5 * (d / IDEAL_EDGE_SIGMA) ** 2) for d in range(-r, r + 1)]
        ks = sum(ker)
        out = []
        for i in range(len(col)):
            acc = [0.0, 0.0, 0.0]
            for d, kw in zip(range(-r, r + 1), ker):
                c = col[min(len(col) - 1, max(0, i + d))]
                for ch in range(3):
                    acc[ch] += kw * c[ch]
            out.append([v / ks for v in acc])
        return out

    @classmethod
    def blend(cls, far, T):
        rows = [bytearray(r) for r in far.rows]
        if T <= 0:
            return Image(far.w, far.h, 4, rows)
        w = [1.0 - smoothstep(k / T) for k in range(int(math.ceil(T)) + 1)]
        edges = {side: cls.blurred_edge(far, side) for side in ("L", "R")}
        for y in range(FRAME[1], FRAME[3]):
            row = rows[y]
            for side in ("L", "R"):
                edge = edges[side][y - FRAME[1]]
                for k, wk in enumerate(w):
                    if wk <= 0:
                        break
                    gi = glow_x(FRAME, side, k) * 4
                    for c in range(3):
                        row[gi + c] = int(round(wk * edge[c] + (1 - wk) * row[gi + c]))
        return Image(far.w, far.h, 4, rows)

    @staticmethod
    def mirror(img):
        rows = []
        l, r = FRAME[0], FRAME[2]
        for src in img.rows:
            row = bytearray(src)
            for k in range(min(l, img.w - r)):
                i, j = (l - 1 - k) * 4, (r + k) * 4
                row[j:j + 4] = row[i:i + 4]
            rows.append(row)
        return Image(img.w, img.h, 4, rows)

    @staticmethod
    def shift(img, width):
        x0 = FRAME[0] - ALT_FRAME[0]  # output column x of the narrow image = column x + x0 of the wide one
        assert 0 <= x0 and x0 + width <= img.w
        rows = [bytearray(src[x0 * 4:(x0 + width) * 4]) for src in img.rows]
        return Image(width, img.h, 4, rows)


# ---------------------------------------------------------------------------------------------

def main(argv=None):
    ap = argparse.ArgumentParser(description=__doc__.split("\n")[0])
    ap.add_argument("--exe", default=r"D:\VlcAmbient\build\VlcAmbient.exe", help="VlcAmbient.exe to test")
    ap.add_argument("--work", default=None, help="work dir for inputs and renders (default: a new temp dir)")
    ap.add_argument("--keep", action="store_true", help="keep the work dir (always kept when --work is given)")
    ap.add_argument("--self-test", action="store_true",
                    help="measure ideal outputs built from the far field instead of the renderer's near field")
    a = ap.parse_args(argv)
    if not os.path.isfile(a.exe):
        print("exe not found: %s" % a.exe)
        return 2
    work = a.work or tempfile.mkdtemp(prefix="vlcambient_quality_")
    os.makedirs(work, exist_ok=True)
    t0 = time.time()
    renderer = Renderer(os.path.abspath(a.exe), work)
    src = IdealSource(renderer) if a.self_test else RenderSource(renderer)
    print("exe:  %s\nwork: %s%s\nmode: %s\n" % (a.exe, work, "" if a.work or a.keep else " (deleted at exit; --keep keeps it)",
                                               "self-test (ideal outputs)" if a.self_test else "renderer"))
    print("%-4s %-21s %-36s %-20s %8s    %6s  %s" % ("", "check", "case", "metric", "value", "limit", "where"))
    res = Results()
    try:
        for check in CHECKS:
            check(src, res)
    except RenderError as e:
        print("ERROR: %s" % e)
        return 2
    finally:
        if not a.work and not a.keep:
            shutil.rmtree(work, ignore_errors=True)
    bad = res.failed()
    gating = [r for r in res.rows if r[5] != "INFO"]
    print("\n%d renders (%.1f s), total %.1f s. %d of %d measurements failed."
          % (renderer.count, renderer.seconds, time.time() - t0, len(bad), len(gating)))
    if bad:
        by_check = {}
        for r in bad:
            by_check.setdefault(r[0], []).append(r)
        for c, rs in by_check.items():
            worst = max(rs, key=lambda r: r[3] / max(r[4], 1e-9))
            print("  %-21s %d failing; worst %s / %s: %.2f vs %.2f (%.1fx)"
                  % (c, len(rs), worst[1], worst[2], worst[3], worst[4], worst[3] / max(worst[4], 1e-9)))
    return 1 if bad else 0


if __name__ == "__main__":
    sys.exit(main())
