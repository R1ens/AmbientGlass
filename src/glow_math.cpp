#include "glow_math.h"

#include <algorithm>
#include <cmath>

GlowMetrics ComputeGlowMetrics(const Settings& s, float pw, float ph) {
  GlowMetrics g;
  float base = std::max(pw, ph);
  g.marginPx = s.spread / 100.f * base / 2.f;
  g.edgePx = s.edge / 100.f * base / 2.f;
  g.sigmaPx = s.blur / 512.f * 1.275f * ph;
  g.transitionPx = s.edgeBlend / 100.f * 0.2f * ph;
  return g;
}

float GlowReach(const Settings& s, float pw, float ph) {
  GlowMetrics g = ComputeGlowMetrics(s, pw, ph);
  return g.marginPx + 3.f * g.sigmaPx + 2.f;
}

float FadeExponent(int curve) {
  float t = std::clamp(curve - 1, 0, 99) / 99.f;
  float half = 0.95f * std::pow(0.1f / 0.95f, t);
  return std::log(0.5f) / std::log(half);
}

float VibranceStrength(int vibrance) {
  if (vibrance == 100) return 0.f;
  float v = vibrance / 100.f - 1.f;
  return (v < 0 ? -1.f : 1.f) * (1.f - std::pow(1.f - std::abs(v), 3.f));
}
