#include "bar_detector.h"

#include <algorithm>
#include <cmath>

bool BarDetector::Line::Black() const { return Mean() < kBarMean && lit <= std::max(1, n / 200); }

bool BarDetector::Feed(const LumaGrid& g, bool vertical, bool horizontal) {
  if (g.w < 8 || g.h < 8 || g.luma.size() < (size_t)g.w * g.h) return false;
  std::vector<Line> rows(g.h), cols(g.w);
  for (int y = 0; y < g.h; y++) {
    const float* row = &g.luma[(size_t)y * g.w];
    for (int x = 0; x < g.w; x++) {
      float l = row[x];
      rows[y].sum += l;
      cols[x].sum += l;
      bool lit = l > kLitLuma;
      rows[y].lit += lit;
      cols[x].lit += lit;
    }
  }
  for (auto& r : rows) r.n = g.w;
  for (auto& c : cols) c.n = g.h;
  float contentLuma = 0;
  for (const auto& r : rows) contentLuma += r.sum;
  contentLuma /= (float)g.w * g.h;

  bool changed = false;
  changed |= FeedAxis(m_v, rows, vertical, contentLuma);
  changed |= FeedAxis(m_h, cols, horizontal, contentLuma);
  return changed;
}

float BarDetector::Vertical() const { return m_v.cur > 0 ? std::max(0.f, m_v.cur - m_v.px) : 0.f; }
float BarDetector::Horizontal() const { return m_h.cur > 0 ? std::max(0.f, m_h.cur - m_h.px) : 0.f; }

bool BarDetector::FeedAxis(Axis& a, const std::vector<Line>& lines, bool enabled, float contentLuma) {
  if (!enabled) {
    bool changed = a.cur != 0;
    a = Axis{};
    return changed;
  }
  int n = (int)lines.size();
  int t = 0;
  while (t < n && lines[t].Black()) t++;
  if (t >= n) return false;  // a fully black frame says nothing
  int b = n - 1;
  while (b > t && lines[b].Black()) b--;
  float px = 1.f / n;
  a.px = px;
  float nearSide = t * px, farSide = (n - 1 - b) * px;
  float m = std::min(nearSide, farSide);
  if (m < 2 * px) m = 0;

  // Content inside the current bars: shrink at once, whatever else the frame looks like.
  if (m < a.cur - px) {
    a.cur = m;
    a.cand = -1;
    a.stable = 0;
    return true;
  }
  if (m <= a.cur + px) {
    a.cand = -1;
    a.stable = 0;
    return false;
  }

  // Growing needs a trustworthy measurement: symmetric, a sharp step into the picture, a lit frame.
  auto mean3 = [&](int from) {
    float s = 0;
    for (int i = from; i < from + 3 && i < n; i++) s += lines[i].Mean();
    return s / 3.f;
  };
  bool symmetric = std::abs(nearSide - farSide) <= 0.01f + 2 * px;
  bool sharp = mean3(t) > kStepMean && mean3(std::max(0, b - 2)) > kStepMean;
  if (!symmetric || !sharp || contentLuma < kDarkFrame || m > kMaxBar) return false;
  if (a.cand >= 0 && std::abs(m - a.cand) <= 2 * px) {
    a.cand = std::min(a.cand, m);
    if (std::abs(contentLuma - a.lastLuma) > kFrameChange) {  // only changing frames count
      a.lastLuma = contentLuma;
      if (++a.stable >= kStableSamples) {
        a.cur = a.cand;
        a.cand = -1;
        a.stable = 0;
        return true;
      }
    }
  } else {
    a.cand = m;
    a.stable = 1;
    a.lastLuma = contentLuma;
  }
  return false;
}
