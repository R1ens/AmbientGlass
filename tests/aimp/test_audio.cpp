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
  AudioAnalyzer::BarBins(24, from, to);
  for (int k = from; k < to; k++) s[k] = 50.f;
  for (int f = 0; f < 30; f++) a.Feed(s.data(), 33.f);
  AudioFrame fr = a.Snapshot();
  CHECK(fr.bars[24] > 0.9f);
  CHECK(fr.bars[6] < 0.05f);
  CHECK(fr.bars[42] < 0.05f);
}

namespace {
// Kicks every periodMs for `seconds`, 30 frames per second; returns the last frame.
AudioFrame RunKicks(AudioAnalyzer& a, float periodMs, float seconds, float gain = 1.f) {
  std::vector<float> s(kSpectrumBins, 0.f);
  float t = 0;
  AudioFrame fr;
  for (int f = 0; f < (int)(seconds * 30); f++) {
    t += 33.3f;
    float env = std::exp(-std::fmod(t, periodMs) / 90.f);
    for (int k = 0; k < kSpectrumBins; k++) s[k] = gain * (k < BeatDetector::kBassEnd ? 0.05f + env : 0.1f);
    a.Feed(s.data(), 33.3f);
    fr = a.Snapshot();
  }
  return fr;
}
}  // namespace

TEST(audio_tempo_from_beats) {
  for (float bpm : {90.f, 120.f, 150.f}) {
    AudioAnalyzer a;
    AudioFrame fr = RunKicks(a, 60000.f / bpm, 12.f);
    CHECK_NEAR(fr.bpm, bpm, bpm * 0.04f);
  }
}

TEST(audio_tempo_folds_fast_kicks_and_forgets_after_silence) {
  AudioAnalyzer a;
  AudioFrame fr = RunKicks(a, 60000.f / 200.f, 12.f);  // 200 BPM reads as 100
  CHECK_NEAR(fr.bpm, 100.f, 5.f);
  std::vector<float> s(kSpectrumBins, 0.f);
  for (int f = 0; f < 240; f++) a.Feed(s.data(), 33.3f);  // 8 s of silence
  CHECK_EQ(a.Snapshot().bpm, 0.f);
}

TEST(audio_new_track_measures_its_own_tempo) {
  AudioAnalyzer a;
  CHECK_NEAR(RunKicks(a, 60000.f / 90.f, 12.f).bpm, 90.f, 4.f);
  a.NewTrack();
  CHECK_EQ(a.Snapshot().bpm, 0.f);
  // A few beats of the next track are enough: the previous track's intervals are gone.
  CHECK_NEAR(RunKicks(a, 60000.f / 150.f, 3.f).bpm, 150.f, 6.f);
}

TEST(audio_stop_forgets_the_tempo) {
  AudioAnalyzer a;
  CHECK(RunKicks(a, 500.f, 8.f).bpm > 0);
  a.Silence(200);
  CHECK_EQ(a.Snapshot().bpm, 0.f);
}

TEST(audio_relative_loudness) {
  AudioAnalyzer a;
  AudioFrame loud = RunKicks(a, 500.f, 6.f, 1.f);
  CHECK(loud.loud > 0.5f);
  AudioFrame quiet = RunKicks(a, 500.f, 3.f, 0.4f);  // the same music 60% quieter
  CHECK(quiet.loud < loud.loud * 0.75f);
  CHECK(quiet.loud > 0.1f);
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
