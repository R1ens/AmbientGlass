#include "beat.h"

#include <algorithm>
#include <cmath>

namespace glass {
namespace {

float Smooth(float cur, float target, float dtMs, float attackMs, float releaseMs) {
  float tau = target > cur ? attackMs : releaseMs;
  float a = tau > 0 ? 1.f - std::exp(-dtMs / tau) : 1.f;
  return cur + (target - cur) * a;
}

float Sum(const float* s, int from, int to) {
  float e = 0;
  for (int i = from; i < to; i++) e += std::max(0.f, s[i]);
  return e;
}

}  // namespace

BeatDetector::BeatDetector(BeatConfig cfg) : m_cfg(cfg) {}

void BeatDetector::Reset() { *this = BeatDetector(m_cfg); }

BeatFrame BeatDetector::Feed(const float* s, float dtMs) {
  dtMs = std::clamp(dtMs, 0.f, 200.f);
  m_now += dtMs;

  // Band energies and the bass flux (sum of rises per bin, so a kick shows even under a held bass note).
  const float bass = Sum(s, 0, kBassEnd), mid = Sum(s, kBassEnd, kMidEnd), high = Sum(s, kMidEnd, kSpectrumBins);
  float flux = 0;
  if (m_hasPrev)
    for (int i = 0; i < kBassEnd; i++) flux += std::max(0.f, std::max(0.f, s[i]) - m_prevBass[i]);
  for (int i = 0; i < kBassEnd; i++) m_prevBass[i] = std::max(0.f, s[i]);
  m_hasPrev = true;

  // Statistics of the last second (before this frame).
  float mean = 0, sq = 0, eMean = 0;
  int n = 0;
  for (size_t k = 0; k < m_count; k++) {
    const Entry& e = m_hist[(m_head + kHistory - 1 - k) % kHistory];
    if (m_now - e.ms > m_cfg.historyMs) break;
    mean += e.flux, sq += e.flux * e.flux, eMean += e.energy, n++;
  }
  bool beat = false;
  if (n >= 8) {
    mean /= n, eMean /= n;
    float sd = std::sqrt(std::max(0.f, sq / n - mean * mean));
    // Both conditions: an outlier rise, and a real step up in bass (flat noise has outliers but no step).
    bool onset = flux > mean + m_cfg.fluxSigmas * sd && flux > 1e-6f && bass > m_cfg.energyRise * eMean;
    beat = onset && m_now - m_lastBeat >= m_cfg.refractoryMs;
  }
  if (beat) m_lastBeat = m_now;

  m_hist[m_head] = {flux, bass, m_now};
  m_head = (m_head + 1) % kHistory;
  m_count = std::min(m_count + 1, kHistory);

  // Levels relative to the loudest of the last few seconds, so quiet and loud tracks both fill 0..1.
  const float bands[3] = {bass, mid, high};
  const float decay = std::exp(-dtMs / 4000.f);
  float level[3];
  for (int i = 0; i < 3; i++) {
    m_peak[i] = std::max(bands[i], std::max(1e-6f, m_peak[i] * decay));
    level[i] = bands[i] / m_peak[i];
  }
  m_out.bass = Smooth(m_out.bass, level[0], dtMs, m_cfg.attackMs, m_cfg.releaseMs);
  m_out.mid = Smooth(m_out.mid, level[1], dtMs, m_cfg.attackMs, m_cfg.releaseMs);
  m_out.high = Smooth(m_out.high, level[2], dtMs, m_cfg.attackMs, m_cfg.releaseMs);
  m_out.pulse = beat ? 1.f : m_out.pulse * std::exp(-dtMs / m_cfg.pulseDecayMs);
  m_out.beat = beat;
  return m_out;
}

}  // namespace glass
