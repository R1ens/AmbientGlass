#pragma once
// Turns the player's spectrum frames (any thread, ~30 per second) into what the window animates: 18 light bars
// under the cover and the beat for the background. The window reads the latest snapshot every frame.

#include <mutex>

#include "beat.h"
#include "model.h"

namespace glass {

struct AudioFrame {
  float bars[SpectrumBars::kCount] = {};
  float bass = 0, high = 0, pulse = 0;
  unsigned beats = 0;  // counts beats, so a reader can tell a new one arrived
};

class AudioAnalyzer {
 public:
  // spectrum: 256 bins (see beat.h); dtMs since the previous frame.
  void Feed(const float* spectrum, float dtMs);
  // Silence (stopped player): everything decays to zero.
  void Silence(float dtMs);
  AudioFrame Snapshot() const;

  // First and one-past-last spectrum bin of bar i: log-spaced from ~40 Hz to ~16 kHz.
  static void BarBins(int bar, int& from, int& to);

 private:
  mutable std::mutex m_mu;
  BeatDetector m_beat;
  AudioFrame m_frame;
  float m_peak[SpectrumBars::kCount] = {};
};

}  // namespace glass
