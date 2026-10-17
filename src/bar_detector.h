#pragma once
#include <vector>

#include "luma_grid.h"

// Black bars that are part of the video file itself (a 2.39:1 film stored as 16:9).
// A bar is a run of rows (columns) that are flat and near-black, ending in a sharp step to the picture.
// Bars shrink as soon as something appears inside them and grow only after the same measurement has
// held on a couple of seconds of changing frames; dark scenes, fades, vignettes and title cards do not count.
class BarDetector {
 public:
  // Linear luma thresholds. A bar row averages below ~4/255 sRGB with next to nothing above ~13/255;
  // the first picture rows must average above ~15/255; frames averaging below ~20/255 teach nothing.
  static constexpr float kBarMean = 0.0012f, kLitLuma = 0.004f, kStepMean = 0.0048f, kDarkFrame = 0.007f;
  static constexpr float kFrameChange = 0.003f;  // distinct frames differ by this much in mean luma
  static constexpr int kStableSamples = 20;      // at the 100 ms luma cadence: two seconds
  static constexpr float kMaxBar = 0.4f;         // of the picture, per side

  void Reset() { m_v = m_h = Axis{}; }

  // Returns true when the bars changed. Luma is linear, 1.0 = SDR white.
  bool Feed(const LumaGrid& g, bool vertical, bool horizontal);

  // Bar size as a fraction of the picture, kept slightly inside the bar so the glow never covers the picture.
  float Vertical() const;    // top and bottom
  float Horizontal() const;  // left and right

 private:
  struct Line {
    float sum = 0;
    int lit = 0, n = 0;
    float Mean() const { return sum / n; }
    bool Black() const;
  };

  struct Axis {
    float cur = 0, cand = -1, px = 0, lastLuma = -1;
    int stable = 0;
  };

  static bool FeedAxis(Axis& a, const std::vector<Line>& lines, bool enabled, float contentLuma);

  Axis m_v, m_h;
};
