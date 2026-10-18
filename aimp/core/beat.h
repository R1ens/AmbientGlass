#pragma once
// Beat and band levels from the player's spectrum, for the background ribbons and the spectrum bars.
//
// Input: the 256-bin spectrum the player hands to visualisations (bin 0 ~ 20 Hz, 10 ~ 200 Hz, 50 ~ 1.1 kHz,
// 100 ~ 3 kHz, 200 ~ 11 kHz, 255 ~ 20 kHz), any linear magnitude scale. A beat is an onset in the bass:
// the positive change of bass energy (spectral flux) standing out of its own recent history, while the bass
// itself rises clearly above its recent average. Steady sounds, however loud or noisy, never trigger it.

#include <array>
#include <cstddef>

namespace glass {

constexpr int kSpectrumBins = 256;

struct BeatFrame {
  float bass = 0, mid = 0, high = 0;  // smoothed band levels, 0..1 (relative to the recent loudest)
  float pulse = 0;                    // 1 on a beat, then decays to 0 over ~350 ms
  bool beat = false;                  // a beat started in this frame
};

struct BeatConfig {
  float fluxSigmas = 2.0f;        // flux must exceed mean + this * std of the last second
  float energyRise = 1.25f;       // and the bass must be this many times its recent average
  float refractoryMs = 120.f;     // no second beat sooner than this
  float pulseDecayMs = 120.f;     // time constant of the pulse (about 5% left after 350 ms)
  float attackMs = 25.f, releaseMs = 220.f;  // band level smoothing
  float historyMs = 1000.f;
};

class BeatDetector {
 public:
  explicit BeatDetector(BeatConfig cfg = {});
  // One spectrum frame; dtMs = time since the previous one (frames may come at any rate).
  BeatFrame Feed(const float* spectrum, float dtMs);
  void Reset();

  // Bins of each band (exposed for tests and the spectrum bars).
  static constexpr int kBassEnd = 9;    // 20..~170 Hz
  static constexpr int kMidEnd = 80;    // ..~2 kHz
 private:
  struct Entry {
    float flux, energy, ms;
  };
  BeatConfig m_cfg;
  std::array<float, kBassEnd> m_prevBass{};
  bool m_hasPrev = false;
  static constexpr size_t kHistory = 512;
  std::array<Entry, kHistory> m_hist{};
  size_t m_head = 0, m_count = 0;
  float m_now = 0, m_lastBeat = -1e9f;
  float m_peak[3] = {1e-6f, 1e-6f, 1e-6f};  // slowly decaying loudest band energies, for 0..1 levels
  BeatFrame m_out;
};

}  // namespace glass
