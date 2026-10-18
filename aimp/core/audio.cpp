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

void AudioAnalyzer::Feed(const float* s, float dtMs) {
  BeatFrame b = m_beat.Feed(s, dtMs);
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
  if (b.beat) m_frame.beats++;
}

void AudioAnalyzer::Silence(float dtMs) {
  std::lock_guard<std::mutex> lock(m_mu);
  float k = std::exp(-std::clamp(dtMs, 0.f, 200.f) / 180.f);
  for (float& v : m_frame.bars) v *= k;
  m_frame.bass *= k, m_frame.high *= k, m_frame.pulse *= k;
}

AudioFrame AudioAnalyzer::Snapshot() const {
  std::lock_guard<std::mutex> lock(m_mu);
  return m_frame;
}

}  // namespace glass
