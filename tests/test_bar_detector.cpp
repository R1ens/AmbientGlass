// BarDetector on synthetic 320x180 LumaGrids of linear luma. Expected numbers are derived from
// bar_detector.cpp: a bar of k grid rows out of n reports Vertical() == k/n - 1/n once it is established
// (kept one grid px inside), growing needs kStableSamples *changing* frames that pass the symmetric /
// sharp / lit / kMaxBar rules, shrinking happens on the first frame with content inside the bars.
#include <algorithm>
#include <cmath>
#include <vector>

#include "bar_detector.h"
#include "luma_grid.h"
#include "test_framework.h"

namespace {

constexpr int W = 320, H = 180;
constexpr float kPx = 1.f / H;  // one grid row as a fraction of the height
constexpr float kPic = 0.2f;    // typical picture luma (linear)

LumaGrid MakeGrid(int w = W, int h = H, float fill = 0.f) {
  LumaGrid g;
  g.w = w;
  g.h = h;
  g.luma.assign((size_t)w * h, fill);
  return g;
}

void FillRows(LumaGrid& g, int from, int to, float luma) {  // rows [from, to)
  for (int y = std::max(0, from); y < std::min(g.h, to); y++)
    for (int x = 0; x < g.w; x++) g.luma[(size_t)y * g.w + x] = luma;
}

void FillRect(LumaGrid& g, int x0, int y0, int x1, int y1, float luma) {  // [x0,x1) x [y0,y1)
  for (int y = std::max(0, y0); y < std::min(g.h, y1); y++)
    for (int x = std::max(0, x0); x < std::min(g.w, x1); x++) g.luma[(size_t)y * g.w + x] = luma;
}

float MeanLuma(const LumaGrid& g) {
  double s = 0;
  for (float v : g.luma) s += v;
  return (float)(s / (double)g.luma.size());
}

// Picture luma alternating +-0.01 around kPic so consecutive frames differ in mean luma by more than
// kFrameChange (0.02 * 132/180 = 0.0147 for a 24-row letterbox).
float Var(int i) { return kPic + ((i & 1) ? 0.01f : -0.01f); }

// Black rows [0, top) and [H - bottom, H), picture elsewhere.
LumaGrid Letterbox(int top, int bottom, float picLuma) {
  LumaGrid g = MakeGrid();
  FillRows(g, top, H - bottom, picLuma);
  return g;
}

// Black columns [0, left) and [W - right, W), picture elsewhere.
LumaGrid Pillarbox(int left, int right, float picLuma) {
  LumaGrid g = MakeGrid();
  FillRect(g, left, 0, W - right, H, picLuma);
  return g;
}

// Feeds a clean 24/24 letterbox until the detector reports a change; returns the number of feeds.
int Establish24(BarDetector& d) {
  for (int i = 0; i < 4 * BarDetector::kStableSamples; i++)
    if (d.Feed(Letterbox(24, 24, Var(i)), true, false)) return i + 1;
  return -1;
}

}  // namespace

TEST(bars_thresholds_as_documented) {
  // Guards the numbers the scenarios below are derived from.
  CHECK_EQ(BarDetector::kStableSamples, 20);
  CHECK_NEAR(BarDetector::kBarMean, 0.0012f, 1e-9);
  CHECK_NEAR(BarDetector::kLitLuma, 0.004f, 1e-9);
  CHECK_NEAR(BarDetector::kStepMean, 0.0048f, 1e-9);
  CHECK_NEAR(BarDetector::kDarkFrame, 0.007f, 1e-9);
  CHECK_NEAR(BarDetector::kFrameChange, 0.003f, 1e-9);
  CHECK_NEAR(BarDetector::kMaxBar, 0.4f, 1e-9);
  // Picture frames used here are far above the dark-frame limit and change enough between feeds.
  CHECK(MeanLuma(Letterbox(24, 24, Var(0))) > BarDetector::kDarkFrame);
  CHECK(std::fabs(MeanLuma(Letterbox(24, 24, Var(0))) - MeanLuma(Letterbox(24, 24, Var(1)))) >
        BarDetector::kFrameChange);
}

TEST(bars_fresh_detector_reports_nothing) {
  BarDetector d;
  CHECK_EQ(d.Vertical(), 0.f);
  CHECK_EQ(d.Horizontal(), 0.f);
}

TEST(bars_rejects_tiny_or_malformed_grids) {
  BarDetector d;
  LumaGrid small = MakeGrid(7, 180, kPic);
  CHECK(!d.Feed(small, true, true));
  LumaGrid flat = MakeGrid(320, 7, kPic);
  CHECK(!d.Feed(flat, true, true));
  LumaGrid truncated = MakeGrid(320, 180, kPic);
  truncated.luma.resize(100);
  CHECK(!d.Feed(truncated, true, true));
  CHECK_EQ(d.Vertical(), 0.f);
  CHECK_EQ(d.Horizontal(), 0.f);
}

// (a) Clean letterbox with changing frames: established on exactly the kStableSamples-th feed.
TEST(bars_clean_letterbox_establishes_after_stable_samples) {
  BarDetector d;
  const int k = BarDetector::kStableSamples;
  for (int i = 0; i < k - 1; i++) {
    CHECK(!d.Feed(Letterbox(24, 24, Var(i)), true, false));
    CHECK_EQ(d.Vertical(), 0.f);
  }
  CHECK(d.Feed(Letterbox(24, 24, Var(k - 1)), true, false));  // the 20th changing frame
  CHECK_NEAR(d.Vertical(), 24.f / H - 1.f / H, 1e-4);
  CHECK_EQ(d.Horizontal(), 0.f);
  // Two more frames: steady, no further change reported, value unchanged.
  CHECK(!d.Feed(Letterbox(24, 24, Var(k)), true, false));
  CHECK(!d.Feed(Letterbox(24, 24, Var(k + 1)), true, false));
  CHECK_NEAR(d.Vertical(), 23.f / H, 1e-4);
  CHECK_EQ(d.Horizontal(), 0.f);
}

TEST(bars_clean_letterbox_k_plus_2_feeds) {
  // The scenario as specified: kStableSamples + 2 feeds with vertical=true.
  BarDetector d;
  for (int i = 0; i < BarDetector::kStableSamples + 2; i++) d.Feed(Letterbox(24, 24, Var(i)), true, false);
  CHECK_NEAR(d.Vertical(), 24.f / 180.f - 1.f / 180.f, 1e-4);
  CHECK_EQ(d.Horizontal(), 0.f);
}

TEST(bars_clean_pillarbox_horizontal_axis) {
  BarDetector d;
  const int k = BarDetector::kStableSamples;
  int changedAt = -1;
  for (int i = 0; i < k + 2; i++)
    if (d.Feed(Pillarbox(40, 40, Var(i)), true, true) && changedAt < 0) changedAt = i + 1;
  CHECK_EQ(changedAt, k);
  CHECK_NEAR(d.Horizontal(), 40.f / W - 1.f / W, 1e-4);
  CHECK_EQ(d.Vertical(), 0.f);  // every row has lit pixels: no vertical bar
}

TEST(bars_letterbox_and_pillarbox_together) {
  BarDetector d;
  const int k = BarDetector::kStableSamples;
  for (int i = 0; i < k + 2; i++) {
    LumaGrid g = MakeGrid();
    FillRect(g, 40, 24, W - 40, H - 24, Var(i));
    d.Feed(g, true, true);
  }
  CHECK_NEAR(d.Vertical(), 23.f / H, 1e-4);
  CHECK_NEAR(d.Horizontal(), 39.f / W, 1e-4);
}

// (b) A single feed never grows bars.
TEST(bars_single_feed_never_grows) {
  BarDetector d;
  CHECK(!d.Feed(Letterbox(24, 24, kPic), true, true));
  CHECK_EQ(d.Vertical(), 0.f);
  CHECK_EQ(d.Horizontal(), 0.f);
  BarDetector d2;
  CHECK(!d2.Feed(Pillarbox(40, 40, kPic), true, true));
  CHECK_EQ(d2.Horizontal(), 0.f);
}

// (c) Title cards.
TEST(bars_title_card_off_centre_text_rejected_by_symmetry) {
  // A bright text block in rows 40..55: near side 40 rows, far side 124 rows -> asymmetric.
  // The frame is lit enough (mean 0.03 > kDarkFrame) and the step is sharp, so only the symmetry rule
  // stands between this frame and a false bar.
  LumaGrid probe = MakeGrid();
  FillRect(probe, 100, 40, 220, 56, 0.9f);
  CHECK(MeanLuma(probe) > BarDetector::kDarkFrame);
  CHECK(std::fabs(40.f * kPx - 124.f * kPx) > 0.01f + 2 * kPx);  // the symmetry rule fails
  CHECK(40.f * kPx <= BarDetector::kMaxBar);                       // kMaxBar would not reject it

  BarDetector d;
  for (int i = 0; i < 3 * BarDetector::kStableSamples; i++) {
    LumaGrid g = MakeGrid();
    FillRect(g, 100, 40, 220, 56, (i & 1) ? 0.9f : 0.7f);  // changing frames
    CHECK(!d.Feed(g, true, false));
  }
  CHECK_EQ(d.Vertical(), 0.f);
  CHECK_EQ(d.Horizontal(), 0.f);
}

TEST(bars_title_card_centred_block_is_learnt_as_side_bars) {
  // Current behaviour, documented rather than desired: the same block is horizontally centred (100
  // black columns each side), the step into it is sharp and the frame is lit, so with horizontal
  // detection on and the block's luma changing every frame the detector cannot tell it from a
  // pillarboxed video and learns 100/320 side bars after kStableSamples frames. In practice a title
  // card is static, so the "changing frames" rule (see bars_static_frames_never_accumulate) is what
  // protects against it.
  BarDetector d;
  int changedAt = -1;
  for (int i = 0; i < BarDetector::kStableSamples + 2; i++) {
    LumaGrid g = MakeGrid();
    FillRect(g, 100, 40, 220, 56, (i & 1) ? 0.9f : 0.7f);
    if (d.Feed(g, true, true) && changedAt < 0) changedAt = i + 1;
  }
  CHECK_EQ(changedAt, BarDetector::kStableSamples);
  CHECK_NEAR(d.Horizontal(), 100.f / W - 1.f / W, 1e-4);  // current behaviour
  CHECK_EQ(d.Vertical(), 0.f);
  // The static version of the same card is never learnt.
  BarDetector d2;
  for (int i = 0; i < 3 * BarDetector::kStableSamples; i++) {
    LumaGrid g = MakeGrid();
    FillRect(g, 100, 40, 220, 56, 0.9f);
    CHECK(!d2.Feed(g, true, true));
  }
  CHECK_EQ(d2.Horizontal(), 0.f);
}

TEST(bars_title_card_centred_thin_lines_rejected_by_dark_frame) {
  // Two thin bright lines at rows 60 and 119 (title and subtitle), 200 of 320 columns wide, on black.
  // The geometry passes symmetry (60 rows each side), kMaxBar (60/180 = 0.333) and the sharp rule
  // (mean3(60) = 0.6*200/320/3 = 0.125 > kStepMean), so the dark-frame rule alone must reject it:
  // the whole frame averages 2*200*luma/57600 < 0.007 for every luma used here.
  for (float lineLuma : {0.3f, 0.9f}) {
    LumaGrid probe = MakeGrid();
    FillRect(probe, 60, 60, 260, 61, lineLuma);
    FillRect(probe, 60, 119, 260, 120, lineLuma);
    CHECK(MeanLuma(probe) < BarDetector::kDarkFrame);
    float rowMean = lineLuma * 200.f / W;
    CHECK(rowMean / 3.f > BarDetector::kStepMean);  // the sharp rule would pass
  }
  CHECK(60.f * kPx <= BarDetector::kMaxBar);
  // Alternating 0.3/0.9 moves the frame mean by 2*200*0.6/57600 = 0.0042 > kFrameChange, so the
  // frames count as changing: without the dark-frame rule the bar would be learnt.
  CHECK(2.f * 200.f * 0.6f / (W * H) > BarDetector::kFrameChange);

  BarDetector d;
  for (int i = 0; i < 3 * BarDetector::kStableSamples; i++) {
    LumaGrid g = MakeGrid();
    float lineLuma = (i & 1) ? 0.9f : 0.3f;
    FillRect(g, 60, 60, 260, 61, lineLuma);
    FillRect(g, 60, 119, 260, 120, lineLuma);
    CHECK(!d.Feed(g, true, true));
  }
  CHECK_EQ(d.Vertical(), 0.f);
  CHECK_EQ(d.Horizontal(), 0.f);
}

TEST(bars_title_card_single_centred_line_rejected) {
  // One bright full-width line in the middle (row 90): mean 1.0*320/57600 = 0.0056 < kDarkFrame, and
  // also 89/180 = 0.494 > kMaxBar. Rejected twice over.
  LumaGrid g = MakeGrid();
  FillRows(g, 90, 91, 1.0f);
  CHECK(MeanLuma(g) < BarDetector::kDarkFrame);
  BarDetector d;
  for (int i = 0; i < 3 * BarDetector::kStableSamples; i++) {
    FillRows(g, 90, 91, (i & 1) ? 1.0f : 0.5f);
    CHECK(!d.Feed(g, true, false));
  }
  CHECK_EQ(d.Vertical(), 0.f);
}

// (d) Vignettes and gradients.
TEST(bars_linear_vignette_0_to_02_in_40_rows_stays_zero) {
  // The scenario as specified: luma ramps 0 -> 0.2 over rows 0..40 and back down at the bottom.
  // With the code's thresholds only row 0 (luma 0) is "black": row 1 is already 0.005 > kLitLuma, so
  // near == far == 1 px < 2 px and the measurement is 0 before the sharp rule is even consulted.
  BarDetector d;
  for (int i = 0; i < 3 * BarDetector::kStableSamples; i++) {
    LumaGrid g = MakeGrid();
    float pic = Var(i);
    for (int y = 0; y < H; y++) {
      int edgeDist = std::min(y, H - 1 - y);
      float l = edgeDist >= 40 ? pic : pic * (float)edgeDist / 40.f;
      FillRows(g, y, y + 1, l);
    }
    CHECK(!d.Feed(g, true, false));
  }
  CHECK_EQ(d.Vertical(), 0.f);
}

TEST(bars_slow_gradient_rejected_by_sharp_rule) {
  // A gradient that does produce a bar-sized black run: rows 0..19 black, rows 20..59 ramp by 0.0005 per
  // row (row 22 = 0.001 is still below kBarMean and kLitLuma; row 23 = 0.0015 is the first non-black row),
  // rows 60..119 picture, mirrored below. near == far == 23 rows (symmetric, 0.128 < kMaxBar), the frame
  // is lit (mean ~ 0.071), but mean3(23) = (0.0015 + 0.002 + 0.0025)/3 = 0.002 < kStepMean: not sharp.
  auto gradient = [](float pic) {
    LumaGrid g = MakeGrid();
    for (int y = 0; y < H; y++) {
      int m = std::min(y, H - 1 - y);  // mirrored row index
      float l = 0.f;
      if (m >= 60)
        l = pic;
      else if (m >= 20)
        l = 0.0005f * (float)(m - 20);
      FillRows(g, y, y + 1, l);
    }
    return g;
  };
  LumaGrid probe = gradient(kPic);
  CHECK(MeanLuma(probe) > BarDetector::kDarkFrame);
  CHECK(probe.luma[22 * W] < BarDetector::kBarMean && probe.luma[22 * W] <= BarDetector::kLitLuma);
  CHECK(probe.luma[23 * W] > BarDetector::kBarMean);
  float mean3 = (probe.luma[23 * W] + probe.luma[24 * W] + probe.luma[25 * W]) / 3.f;
  CHECK(mean3 < BarDetector::kStepMean);  // the sharp rule is what rejects this frame
  CHECK(23.f * kPx <= BarDetector::kMaxBar);

  BarDetector d;
  for (int i = 0; i < 3 * BarDetector::kStableSamples; i++) CHECK(!d.Feed(gradient(Var(i)), true, false));
  CHECK_EQ(d.Vertical(), 0.f);
}

// (e) Asymmetric bars never grow.
TEST(bars_asymmetric_letterbox_stays_zero) {
  CHECK(std::fabs(24.f * kPx - 40.f * kPx) > 0.01f + 2 * kPx);
  BarDetector d;
  for (int i = 0; i < 3 * BarDetector::kStableSamples; i++) CHECK(!d.Feed(Letterbox(24, 40, Var(i)), true, false));
  CHECK_EQ(d.Vertical(), 0.f);
  // Within the tolerance (0.01 + 2 px ~ 2.9 rows): 24 vs 26 rows is still "symmetric", min side wins.
  BarDetector d2;
  for (int i = 0; i < BarDetector::kStableSamples + 2; i++) d2.Feed(Letterbox(24, 26, Var(i)), true, false);
  CHECK_NEAR(d2.Vertical(), 23.f / H, 1e-4);
}

// Bars above kMaxBar (40% per side) never grow.
TEST(bars_over_max_bar_stays_zero) {
  CHECK(80.f * kPx > BarDetector::kMaxBar);
  BarDetector d;
  for (int i = 0; i < 3 * BarDetector::kStableSamples; i++) CHECK(!d.Feed(Letterbox(80, 80, Var(i)), true, false));
  CHECK_EQ(d.Vertical(), 0.f);
  // 70 rows (0.389) is still allowed.
  BarDetector d2;
  for (int i = 0; i < BarDetector::kStableSamples + 2; i++) d2.Feed(Letterbox(70, 70, Var(i)), true, false);
  CHECK_NEAR(d2.Vertical(), 69.f / H, 1e-4);
}

// Bars thinner than 2 grid px are treated as none.
TEST(bars_one_row_is_ignored_two_rows_count) {
  BarDetector d;
  for (int i = 0; i < 3 * BarDetector::kStableSamples; i++) CHECK(!d.Feed(Letterbox(1, 1, Var(i)), true, false));
  CHECK_EQ(d.Vertical(), 0.f);
  BarDetector d2;
  for (int i = 0; i < BarDetector::kStableSamples + 2; i++) d2.Feed(Letterbox(2, 2, Var(i)), true, false);
  CHECK_NEAR(d2.Vertical(), 1.f / H, 1e-4);  // 2 px bar, reported 1 px inside
}

// (f) Shrink: content inside the bar is honoured on the very next feed.
TEST(bars_shrink_immediately_on_content_in_bar) {
  BarDetector d;
  CHECK_EQ(Establish24(d), BarDetector::kStableSamples);
  CHECK_NEAR(d.Vertical(), 23.f / H, 1e-4);

  // Content in row 5: near = 5 rows, far = 24 rows -> m = 5/180 (>= 2 px, so not zeroed);
  // 5/180 < cur - px = 23/180 -> shrink at once. Vertical() = 5/180 - 1/180 = 4/180.
  LumaGrid g = Letterbox(24, 24, kPic);
  FillRows(g, 5, 6, kPic);
  CHECK(d.Feed(g, true, false));
  CHECK_NEAR(d.Vertical(), 5.f / 180.f - 1.f / 180.f, 1e-4);

  // A clean 24/24 frame afterwards does not restore the bar: growing needs kStableSamples frames again.
  CHECK(!d.Feed(Letterbox(24, 24, Var(0)), true, false));
  CHECK_NEAR(d.Vertical(), 4.f / H, 1e-4);
}

TEST(bars_shrink_to_zero_on_content_at_edge) {
  BarDetector d;
  CHECK_EQ(Establish24(d), BarDetector::kStableSamples);
  // Content in row 1: near = 1 px < 2 px -> m = 0 -> bars drop to nothing in one feed.
  LumaGrid g = Letterbox(24, 24, kPic);
  FillRows(g, 1, 2, kPic);
  CHECK(d.Feed(g, true, false));
  CHECK_EQ(d.Vertical(), 0.f);
}

TEST(bars_shrink_ignores_the_grow_rules) {
  // Shrinking does not need symmetry, a sharp step or a lit frame: a dim asymmetric speck inside the
  // top bar is enough.
  BarDetector d;
  CHECK_EQ(Establish24(d), BarDetector::kStableSamples);
  LumaGrid g = MakeGrid();                  // otherwise black frame: mean far below kDarkFrame
  FillRect(g, 150, 10, 170, 11, 0.02f);     // 20 dim pixels in row 10
  CHECK(MeanLuma(g) < BarDetector::kDarkFrame);
  CHECK(d.Feed(g, true, false));
  CHECK_NEAR(d.Vertical(), 10.f / H - 1.f / H, 1e-4);  // near 10 rows, far = 169 rows -> m = 10/180
}

TEST(bars_fully_black_frame_teaches_nothing) {
  BarDetector d;
  CHECK_EQ(Establish24(d), BarDetector::kStableSamples);
  CHECK(!d.Feed(MakeGrid(), true, true));  // all zero: neither grows nor shrinks
  CHECK_NEAR(d.Vertical(), 23.f / H, 1e-4);
  BarDetector d2;
  for (int i = 0; i < 3 * BarDetector::kStableSamples; i++) CHECK(!d2.Feed(MakeGrid(), true, true));
  CHECK_EQ(d2.Vertical(), 0.f);
  CHECK_EQ(d2.Horizontal(), 0.f);
}

// (g) Static frames (identical mean luma) never accumulate.
TEST(bars_static_frames_never_accumulate) {
  BarDetector d;
  for (int i = 0; i < 3 * BarDetector::kStableSamples; i++) CHECK(!d.Feed(Letterbox(24, 24, kPic), true, true));
  CHECK_EQ(d.Vertical(), 0.f);
  CHECK_EQ(d.Horizontal(), 0.f);
  // Changes below kFrameChange do not count either: 0.2 vs 0.2025 moves the mean by 0.0018.
  BarDetector d2;
  for (int i = 0; i < 3 * BarDetector::kStableSamples; i++)
    CHECK(!d2.Feed(Letterbox(24, 24, (i & 1) ? 0.2025f : 0.2f), true, false));
  CHECK_EQ(d2.Vertical(), 0.f);
}

// (h) Disabling an axis resets it and reports the change once.
TEST(bars_disable_resets_and_reports_once) {
  BarDetector d;
  CHECK_EQ(Establish24(d), BarDetector::kStableSamples);
  CHECK(d.Vertical() > 0.f);
  CHECK(d.Feed(Letterbox(24, 24, kPic), false, false));  // changed: bars went away
  CHECK_EQ(d.Vertical(), 0.f);
  CHECK_EQ(d.Horizontal(), 0.f);
  CHECK(!d.Feed(Letterbox(24, 24, kPic), false, false));  // nothing left to change
  CHECK_EQ(d.Vertical(), 0.f);
  // Disabling an axis that never had bars is not a change.
  BarDetector d2;
  CHECK(!d2.Feed(Letterbox(24, 24, kPic), false, false));
}

TEST(bars_disabled_axis_is_not_learnt) {
  BarDetector d;
  for (int i = 0; i < 3 * BarDetector::kStableSamples; i++) CHECK(!d.Feed(Letterbox(24, 24, Var(i)), false, true));
  CHECK_EQ(d.Vertical(), 0.f);
  CHECK_EQ(d.Horizontal(), 0.f);
}

TEST(bars_reset_clears_everything) {
  BarDetector d;
  CHECK_EQ(Establish24(d), BarDetector::kStableSamples);
  d.Reset();
  CHECK_EQ(d.Vertical(), 0.f);
  CHECK_EQ(d.Horizontal(), 0.f);
  // After a reset the bars have to be learnt from scratch.
  CHECK_EQ(Establish24(d), BarDetector::kStableSamples);
}

TEST(bars_candidate_restarts_when_measurement_jumps) {
  // 19 frames of 24-row bars, then one of 30 rows (more than 2 px away from the candidate): the
  // candidate restarts, so the 20th 24-row frame does not establish anything yet.
  BarDetector d;
  const int k = BarDetector::kStableSamples;
  for (int i = 0; i < k - 1; i++) CHECK(!d.Feed(Letterbox(24, 24, Var(i)), true, false));
  CHECK(!d.Feed(Letterbox(30, 30, Var(k - 1)), true, false));
  CHECK(!d.Feed(Letterbox(24, 24, Var(k)), true, false));
  CHECK_EQ(d.Vertical(), 0.f);
  // The smaller of nearby measurements is kept: 24 and 25 rows alternating establishes 24.
  BarDetector d2;
  int changedAt = -1;
  for (int i = 0; i < k + 2; i++)
    if (d2.Feed(Letterbox((i & 1) ? 25 : 24, (i & 1) ? 25 : 24, Var(i)), true, false) && changedAt < 0)
      changedAt = i + 1;
  CHECK_EQ(changedAt, k);
  CHECK_NEAR(d2.Vertical(), 23.f / H, 1e-4);
}

TEST(bars_dim_bar_rows_below_thresholds_still_count_as_black) {
  // Real bars are not exactly 0: luma 0.001 per pixel (mean < kBarMean, no pixel above kLitLuma) is black.
  BarDetector d;
  for (int i = 0; i < BarDetector::kStableSamples + 2; i++) {
    LumaGrid g = MakeGrid(W, H, 0.001f);
    FillRows(g, 24, H - 24, Var(i));
    d.Feed(g, true, false);
  }
  CHECK_NEAR(d.Vertical(), 23.f / H, 1e-4);
  // One lit pixel per bar row is tolerated (lit <= max(1, n/200) = 1 for n = 320)...
  BarDetector d2;
  for (int i = 0; i < BarDetector::kStableSamples + 2; i++) {
    LumaGrid g = Letterbox(24, 24, Var(i));
    for (int y = 0; y < 24; y++) g.luma[(size_t)y * W + 7] = 0.01f;
    d2.Feed(g, true, false);
  }
  CHECK_NEAR(d2.Vertical(), 23.f / H, 1e-4);
  // ...two are not: those rows are no longer black, so the near side is 0 rows and nothing is learnt.
  BarDetector d3;
  for (int i = 0; i < 3 * BarDetector::kStableSamples; i++) {
    LumaGrid g = Letterbox(24, 24, Var(i));
    for (int y = 0; y < 24; y++) {
      g.luma[(size_t)y * W + 7] = 0.01f;
      g.luma[(size_t)y * W + 300] = 0.01f;
    }
    CHECK(!d3.Feed(g, true, false));
  }
  CHECK_EQ(d3.Vertical(), 0.f);
}
