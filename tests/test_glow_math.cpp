// glow_math: glow geometry in px, reach monotonicity, fade exponent, vibrance easing.
#include <algorithm>
#include <cmath>

#include "glow_math.h"
#include "test_framework.h"

namespace {
// The documented formulas, in double, to compare the float implementation against.
double ExpectedMargin(int spread, double w, double h) { return spread / 100.0 * std::max(w, h) / 2.0; }
double ExpectedEdge(int edge, double w, double h) { return edge / 100.0 * std::max(w, h) / 2.0; }
double ExpectedSigma(int blur, double h) { return blur / 512.0 * 1.275 * h; }
double ExpectedTransition(int edgeBlend, double h) { return edgeBlend / 100.0 * 0.2 * h; }
}  // namespace

TEST(glow_metrics_defaults_1080p) {
  Settings s;  // spread 17, edge 12, blur 30, edgeBlend 40
  GlowMetrics g = ComputeGlowMetrics(s, 1920.f, 1080.f);
  CHECK_NEAR(g.marginPx, 163.2, 1e-3);       // 0.17 * 960
  CHECK_NEAR(g.edgePx, 115.2, 1e-3);         // 0.12 * 960
  CHECK_NEAR(g.sigmaPx, 80.68359375, 1e-3);  // 30/512 * 1.275 * 1080
  CHECK_NEAR(g.transitionPx, 86.4, 1e-3);    // 0.4 * 0.2 * 1080
}

TEST(glow_metrics_match_formulas_over_sizes_and_settings) {
  const float sizes[][2] = {{1920.f, 1080.f}, {1280.f, 720.f}, {3840.f, 2160.f},
                            {1080.f, 1920.f}, {640.f, 640.f},  {1.f, 1.f}};
  const int spreads[] = {0, 1, 17, 100, 400};
  const int edges[] = {2, 12, 50};
  const int blurs[] = {0, 1, 30, 100};
  const int blends[] = {0, 40, 100};
  for (const auto& sz : sizes)
    for (int sp : spreads)
      for (int e : edges)
        for (int b : blurs)
          for (int eb : blends) {
            Settings s;
            s.spread = sp;
            s.edge = e;
            s.blur = b;
            s.edgeBlend = eb;
            GlowMetrics g = ComputeGlowMetrics(s, sz[0], sz[1]);
            double w = sz[0], h = sz[1];
            CHECK_NEAR(g.marginPx, ExpectedMargin(sp, w, h), 1e-2);
            CHECK_NEAR(g.edgePx, ExpectedEdge(e, w, h), 1e-2);
            CHECK_NEAR(g.sigmaPx, ExpectedSigma(b, h), 1e-2);
            CHECK_NEAR(g.transitionPx, ExpectedTransition(eb, h), 1e-2);
          }
}

TEST(glow_metrics_scale_with_long_side) {
  Settings s;
  s.spread = 50;
  s.edge = 10;
  // Landscape and portrait pictures with the same long side share margin and edge (base = max(w, h)).
  GlowMetrics land = ComputeGlowMetrics(s, 1920.f, 1080.f);
  GlowMetrics port = ComputeGlowMetrics(s, 1080.f, 1920.f);
  CHECK_NEAR(land.marginPx, 480.f, 1e-3);  // 0.5 * 1920 / 2
  CHECK_NEAR(port.marginPx, land.marginPx, 1e-4);
  CHECK_NEAR(port.edgePx, land.edgePx, 1e-4);
  // ... but sigma and transition follow the height.
  CHECK_NEAR(port.sigmaPx, land.sigmaPx * 1920.f / 1080.f, 1e-3);
  CHECK_NEAR(port.transitionPx, land.transitionPx * 1920.f / 1080.f, 1e-3);
  // Doubling the picture doubles everything.
  GlowMetrics big = ComputeGlowMetrics(s, 3840.f, 2160.f);
  CHECK_NEAR(big.marginPx, 2 * land.marginPx, 1e-3);
  CHECK_NEAR(big.edgePx, 2 * land.edgePx, 1e-3);
  CHECK_NEAR(big.sigmaPx, 2 * land.sigmaPx, 1e-3);
  CHECK_NEAR(big.transitionPx, 2 * land.transitionPx, 1e-3);
}

TEST(glow_metrics_scale_linearly_with_sliders) {
  Settings a, b;
  a.spread = 20;
  b.spread = 40;
  a.edge = 10;
  b.edge = 20;
  a.blur = 25;
  b.blur = 50;
  a.edgeBlend = 30;
  b.edgeBlend = 60;
  GlowMetrics ga = ComputeGlowMetrics(a, 1920.f, 1080.f), gb = ComputeGlowMetrics(b, 1920.f, 1080.f);
  CHECK_NEAR(gb.marginPx, 2 * ga.marginPx, 1e-3);
  CHECK_NEAR(gb.edgePx, 2 * ga.edgePx, 1e-3);
  CHECK_NEAR(gb.sigmaPx, 2 * ga.sigmaPx, 1e-3);
  CHECK_NEAR(gb.transitionPx, 2 * ga.transitionPx, 1e-3);
  Settings z;
  z.spread = 0;
  z.blur = 0;
  z.edgeBlend = 0;
  GlowMetrics gz = ComputeGlowMetrics(z, 1920.f, 1080.f);
  CHECK_EQ(gz.marginPx, 0.f);
  CHECK_EQ(gz.sigmaPx, 0.f);
  CHECK_EQ(gz.transitionPx, 0.f);
}

TEST(glow_reach_is_margin_plus_three_sigma_plus_two) {
  Settings s;
  for (int sp : {0, 17, 100, 400})
    for (int b : {0, 30, 100}) {
      s.spread = sp;
      s.blur = b;
      GlowMetrics g = ComputeGlowMetrics(s, 1920.f, 1080.f);
      CHECK_NEAR(GlowReach(s, 1920.f, 1080.f), g.marginPx + 3.f * g.sigmaPx + 2.f, 1e-3);
    }
  s.spread = 0;
  s.blur = 0;
  CHECK_NEAR(GlowReach(s, 1920.f, 1080.f), 2.f, 1e-6);  // never zero: a 2 px safety band
}

TEST(glow_reach_monotonic_in_spread) {
  // Property: for every blur, the reach never decreases as spread grows (continuous, not quantised).
  for (int blur : {0, 10, 30, 100}) {
    Settings s;
    s.blur = blur;
    s.spread = 0;
    float prev = GlowReach(s, 1920.f, 1080.f);
    int increases = 0;
    for (int sp = 1; sp <= 400; sp++) {
      s.spread = sp;
      float r = GlowReach(s, 1920.f, 1080.f);
      CHECK(r >= prev);
      if (r > prev) increases++;
      prev = r;
    }
    CHECK_EQ(increases, 400);  // strictly increasing: every step of the slider changes the reach
  }
}

TEST(glow_reach_monotonic_in_blur) {
  for (int spread : {0, 17, 100, 400}) {
    Settings s;
    s.spread = spread;
    s.blur = 0;
    float prev = GlowReach(s, 1920.f, 1080.f);
    int increases = 0;
    for (int b = 1; b <= 100; b++) {
      s.blur = b;
      float r = GlowReach(s, 1920.f, 1080.f);
      CHECK(r >= prev);
      if (r > prev) increases++;
      prev = r;
    }
    CHECK_EQ(increases, 100);
  }
}

TEST(glow_reach_independent_of_other_sliders) {
  Settings a, b;
  b.edge = 50;
  b.edgeBlend = 100;
  b.fadeStart = -50;
  b.fadeCurve = 1;
  b.brightness = 200;
  CHECK_EQ(GlowReach(a, 1920.f, 1080.f), GlowReach(b, 1920.f, 1080.f));
}

TEST(fade_exponent_endpoints) {
  // curve 1: half the light gone at 95% of the reach -> exponent log(0.5)/log(0.95) ~ 13.51
  CHECK_NEAR(FadeExponent(1), std::log(0.5) / std::log(0.95), 1e-3);
  // curve 100: half gone at 10% -> log(0.5)/log(0.1) ~ 0.301
  CHECK_NEAR(FadeExponent(100), std::log(0.5) / std::log(0.1), 1e-4);
  // Out-of-range sliders clamp to the ends.
  CHECK_EQ(FadeExponent(0), FadeExponent(1));
  CHECK_EQ(FadeExponent(-10), FadeExponent(1));
  CHECK_EQ(FadeExponent(101), FadeExponent(100));
  CHECK_EQ(FadeExponent(1000), FadeExponent(100));
}

TEST(fade_exponent_strictly_decreasing_positive_crosses_one) {
  float prev = FadeExponent(1);
  CHECK(prev > 1.f);
  bool crossed = false;
  int crossAt = 0;
  for (int c = 2; c <= 100; c++) {
    float e = FadeExponent(c);
    CHECK(e > 0.f);
    CHECK(e < prev);
    if (!crossed && e <= 1.f) {
      crossed = true;
      crossAt = c;
    }
    prev = e;
  }
  CHECK(prev < 1.f);
  CHECK(crossed);
  // half = 0.5 (exponent 1) when t = log(0.5/0.95)/log(0.1/0.95) ~ 0.285 -> curve ~ 29.2
  CHECK(crossAt >= 28 && crossAt <= 31);
  CHECK_NEAR(FadeExponent(29), 1.f, 0.03);
}

TEST(fade_exponent_geometric_half_point) {
  // The implementation's contract: pow(half, exponent) == 0.5 where half moves geometrically 0.95 -> 0.1.
  for (int c = 1; c <= 100; c++) {
    double t = (c - 1) / 99.0;
    double half = 0.95 * std::pow(0.1 / 0.95, t);
    CHECK_NEAR(std::pow(half, (double)FadeExponent(c)), 0.5, 1e-4);
  }
}

TEST(vibrance_strength_neutral_and_ends) {
  CHECK_EQ(VibranceStrength(100), 0.f);
  CHECK_NEAR(VibranceStrength(200), 1.f, 1e-6);
  CHECK_NEAR(VibranceStrength(0), -1.f, 1e-6);
  // Cubic ease: 1 - (1 - |v|)^3. At 150: 1 - 0.125 = 0.875.
  CHECK_NEAR(VibranceStrength(150), 0.875f, 1e-5);
  CHECK_NEAR(VibranceStrength(50), -0.875f, 1e-5);
}

TEST(vibrance_strength_odd_symmetric_bounded_monotonic) {
  for (int d = 1; d <= 100; d++) {
    CHECK_NEAR(VibranceStrength(100 + d), -VibranceStrength(100 - d), 1e-5);
    CHECK(VibranceStrength(100 + d) > 0.f);
    CHECK(VibranceStrength(100 - d) < 0.f);
  }
  float prev = VibranceStrength(0);
  for (int v = 0; v <= 200; v++) {
    float f = VibranceStrength(v);
    CHECK(std::fabs(f) <= 1.f + 1e-6f);
    CHECK(f >= prev);
    prev = f;
  }
}
