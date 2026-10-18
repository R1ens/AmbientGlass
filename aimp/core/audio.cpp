#include "audio.h"

#include <algorithm>
#include <cmath>
#include <vector>

namespace glass {

void AudioAnalyzer::BarBins(int bar, int& from, int& to) {
  // Bins are roughly logarithmic already (0 ~ 20 Hz, 255 ~ 20 kHz): spread the bars from bin 2 to 230 on a
  // gentle power curve so the lows do not take all the bars.
  const int n = SpectrumBars::kCount;
  auto edge = [&](int i) { return (int)std::lround(2 + 228 * std::pow((float)i / n, 1.25f)); };
  from = edge(bar);
  to = std::max(from + 1, edge(bar + 1));
}

namespace {

// Tempo from beat intervals: the median (robust to a missed or an extra beat), folded into 70..180 BPM so a
// half- or double-time reading lands on the same tempo.
float TempoFromIntervals(const float* iv, int n) {
  if (n < 4) return 0;
  float v[16];
  std::copy(iv, iv + n, v);
  std::sort(v, v + n);
  float med = n % 2 ? v[n / 2] : 0.5f * (v[n / 2 - 1] + v[n / 2]);
  float bpm = 60000.f / med;
  while (bpm < 70) bpm *= 2;
  while (bpm >= 180) bpm /= 2;
  return bpm;
}

}  // namespace

void AudioAnalyzer::Feed(const float* s, float dtMs) {
  BeatFrame b = m_beat.Feed(s, dtMs);
  m_now += std::clamp(dtMs, 0.f, 200.f);
  float level[SpectrumBars::kCount];
  const float decay = std::exp(-std::clamp(dtMs, 0.f, 200.f) / 3000.f);
  std::lock_guard<std::mutex> lock(m_mu);
  for (int i = 0; i < SpectrumBars::kCount; i++) {
    int from, to;
    BarBins(i, from, to);
    float e = 0;
    for (int k = from; k < to; k++) e = std::max(e, std::max(0.f, s[k]));
    // Each bar relative to its own recent loudest: highs are much weaker than lows in absolute terms.
    m_peak[i] = std::max(e, std::max(1e-6f, m_peak[i] * decay));
    level[i] = std::clamp(e / m_peak[i], 0.f, 1.f);
    // Fast rise, slower fall, like a light that flares and fades.
    float cur = m_frame.bars[i];
    float tau = level[i] > cur ? 30.f : 180.f;
    m_frame.bars[i] = cur + (level[i] - cur) * (1.f - std::exp(-dtMs / tau));
  }
  m_frame.bass = b.bass;
  m_frame.high = b.high;
  m_frame.pulse = b.pulse;
  if (b.beat) {
    m_frame.beats++;
    float iv = m_now - m_lastBeat;
    if (iv >= 250 && iv <= 1500) {
      m_intervals[m_intervalHead] = iv;
      m_intervalHead = (m_intervalHead + 1) % 16;
      m_intervalCount = std::min(m_intervalCount + 1, 16);
      float t = TempoFromIntervals(m_intervals, m_intervalCount);
      if (t > 0) m_frame.bpm = m_frame.bpm > 0 ? m_frame.bpm + (t - m_frame.bpm) * 0.3f : t;
    }
    m_lastBeat = m_now;
  }
  if (m_now - m_lastBeat > 6000) {  // no beat for a while: the tempo is no longer known
    m_frame.bpm = 0;
    m_intervalCount = m_intervalHead = 0;
  }

  // Loudness relative to the loudest moment of the last ~10 s; slow to follow so it never twitches.
  float e = 0;
  for (int i = 0; i < kSpectrumBins; i++) e += std::max(0.f, s[i]);
  m_loudPeak = std::max(e, std::max(1e-6f, m_loudPeak * std::exp(-dtMs / 10000.f)));
  float rel = std::clamp(e / m_loudPeak, 0.f, 1.f);
  float tau = rel > m_frame.loud ? 120.f : 600.f;
  m_frame.loud += (rel - m_frame.loud) * (1.f - std::exp(-dtMs / tau));
}

void AudioAnalyzer::Silence(float dtMs) {
  std::lock_guard<std::mutex> lock(m_mu);
  float k = std::exp(-std::clamp(dtMs, 0.f, 200.f) / 180.f);
  for (float& v : m_frame.bars) v *= k;
  m_frame.bass *= k, m_frame.high *= k, m_frame.pulse *= k, m_frame.loud *= k;
}

AudioFrame AudioAnalyzer::Snapshot() const {
  std::lock_guard<std::mutex> lock(m_mu);
  return m_frame;
}

}  // namespace glass
