// Spectrum bars and beat counting from synthetic spectra.
#include <cmath>
#include <vector>

#include "../../aimp/core/audio.h"
#include "../test_framework.h"

using namespace glass;

TEST(audio_bars_cover_the_spectrum_in_order) {
  int prevTo = 0;
  for (int i = 0; i < SpectrumBars::kCount; i++) {
    int from, to;
    AudioAnalyzer::BarBins(i, from, to);
    CHECK(from >= prevTo - 0);
    CHECK(to > from);
    CHECK(to <= kSpectrumBins);
    prevTo = to;
  }
}

TEST(audio_tone_lights_its_bar) {
  AudioAnalyzer a;
  std::vector<float> s(kSpectrumBins, 0.f);
  int from, to;
  AudioAnalyzer::BarBins(9, from, to);
  for (int k = from; k < to; k++) s[k] = 50.f;
  for (int f = 0; f < 30; f++) a.Feed(s.data(), 33.f);
  AudioFrame fr = a.Snapshot();
  CHECK(fr.bars[9] > 0.9f);
  CHECK(fr.bars[2] < 0.05f);
  CHECK(fr.bars[16] < 0.05f);
}

TEST(audio_counts_beats_and_silences) {
  AudioAnalyzer a;
  std::vector<float> s(kSpectrumBins, 0.f);
  float t = 0;
  for (int f = 0; f < 300; f++) {  // 10 s at 30 frames per second, kick every 500 ms
    t += 33.3f;
    float env = std::exp(-std::fmod(t, 500.f) / 90.f);
    for (int k = 0; k < BeatDetector::kBassEnd; k++) s[k] = 0.05f + env;
    a.Feed(s.data(), 33.3f);
  }
  AudioFrame fr = a.Snapshot();
  CHECK(fr.beats >= 18);
  CHECK(fr.beats <= 20);
  for (int f = 0; f < 60; f++) a.Silence(33.3f);
  fr = a.Snapshot();
  CHECK(fr.pulse < 0.01f);
  CHECK(fr.bars[0] < 0.01f);
}
