// Beat detection on synthetic spectra: one beat per kick, none for steady sound or silence.
#include <cmath>
#include <cstdint>
#include <vector>

#include "../../aimp/core/beat.h"
#include "../test_framework.h"

using namespace glass;

namespace {

// Deterministic pseudo-random numbers in [-1, 1).
struct Rng {
  uint32_t s;
  float Next() {
    s = s * 1664525u + 1013904223u;
    return (float)((s >> 8) & 0xFFFF) / 32768.f - 1.f;
  }
};

struct Result {
  std::vector<float> beatsMs;
  float lastPulse = 0;
};

// Kick drum every `periodMs`: bass bins jump to `kick` and decay with a 90 ms time constant on top of a
// steady `floor`; mids and highs hold steady with a little jitter. Frames come every `frameMs` (+- jitter).
Result RunKicks(float periodMs, float seconds, float frameMs, float frameJitterMs, float floor, float kick,
                uint32_t seed = 1) {
  BeatDetector det;
  Rng rng{seed};
  Result r;
  std::vector<float> s(kSpectrumBins);
  float t = 0;
  while (t < seconds * 1000.f) {
    float dt = frameMs + frameJitterMs * rng.Next();
    t += dt;
    float sinceKick = std::fmod(t, periodMs);
    float env = kick * std::exp(-sinceKick / 90.f);
    for (int i = 0; i < kSpectrumBins; i++) {
      float base = i < BeatDetector::kBassEnd ? floor + env : 0.25f * std::exp(-i / 120.f);
      s[i] = base * (1.f + 0.03f * rng.Next());
    }
    BeatFrame f = det.Feed(s.data(), dt);
    if (f.beat) r.beatsMs.push_back(t);
    r.lastPulse = f.pulse;
  }
  return r;
}

// Every detected beat is within `tolMs` after a kick, and no kick gets two.
bool OnePerKick(const std::vector<float>& beats, float periodMs, float tolMs) {
  long lastKick = -1;
  for (float b : beats) {
    long kickIdx = (long)std::floor(b / periodMs);
    if (b - kickIdx * periodMs > tolMs) return false;
    if (kickIdx == lastKick) return false;
    lastKick = kickIdx;
  }
  return true;
}

}  // namespace

TEST(beat_120bpm_one_beat_per_kick) {
  Result r = RunKicks(500.f, 10.f, 16.7f, 0.f, 0.05f, 1.f);
  // The first kick may fall inside the warm-up (no history yet); every other one must be found.
  CHECK(r.beatsMs.size() >= 19);
  CHECK(r.beatsMs.size() <= 20);
  CHECK(OnePerKick(r.beatsMs, 500.f, 40.f));
}

TEST(beat_uneven_frame_rate) {
  Result r = RunKicks(500.f, 10.f, 18.f, 8.f, 0.05f, 1.f, 7);
  CHECK(r.beatsMs.size() >= 19);
  CHECK(r.beatsMs.size() <= 20);
  CHECK(OnePerKick(r.beatsMs, 500.f, 45.f));
}

TEST(beat_fast_tempo_174bpm) {
  float period = 60000.f / 174.f;
  Result r = RunKicks(period, 10.f, 16.7f, 0.f, 0.05f, 1.f, 3);
  int expected = (int)(10000.f / period);
  CHECK((int)r.beatsMs.size() >= expected - 1);
  CHECK((int)r.beatsMs.size() <= expected);
  CHECK(OnePerKick(r.beatsMs, period, 40.f));
}

TEST(beat_kicks_over_held_bass_note) {
  // A loud sustained bass under the kicks must not hide them.
  Result r = RunKicks(500.f, 10.f, 16.7f, 0.f, 0.6f, 1.f, 5);
  CHECK(r.beatsMs.size() >= 19);
  CHECK(OnePerKick(r.beatsMs, 500.f, 40.f));
}

TEST(beat_steady_noise_never_triggers) {
  BeatDetector det;
  Rng rng{42};
  std::vector<float> s(kSpectrumBins);
  int beats = 0;
  for (int f = 0; f < 600; f++) {  // 10 s at 60 fps
    for (int i = 0; i < kSpectrumBins; i++) s[i] = 0.5f * (1.f + 0.1f * rng.Next());
    beats += det.Feed(s.data(), 16.7f).beat ? 1 : 0;
  }
  CHECK_EQ(beats, 0);
}

TEST(beat_silence_never_triggers_and_levels_stay_zero) {
  BeatDetector det;
  std::vector<float> s(kSpectrumBins, 0.f);
  BeatFrame f;
  int beats = 0;
  for (int i = 0; i < 300; i++) {
    f = det.Feed(s.data(), 16.7f);
    beats += f.beat ? 1 : 0;
  }
  CHECK_EQ(beats, 0);
  CHECK_NEAR(f.bass, 0.f, 1e-6);
  CHECK_NEAR(f.high, 0.f, 1e-6);
  CHECK_NEAR(f.pulse, 0.f, 1e-6);
}

TEST(beat_pulse_decays_within_350ms) {
  BeatDetector det;
  std::vector<float> s(kSpectrumBins, 0.f);
  // One second of quiet with a faint floor, then a kick, then quiet again.
  for (int i = 0; i < 60; i++) {
    for (int b = 0; b < BeatDetector::kBassEnd; b++) s[b] = 0.05f;
    det.Feed(s.data(), 16.7f);
  }
  for (int b = 0; b < BeatDetector::kBassEnd; b++) s[b] = 1.f;
  BeatFrame f = det.Feed(s.data(), 16.7f);
  CHECK(f.beat);
  CHECK_NEAR(f.pulse, 1.f, 1e-6);
  for (int b = 0; b < BeatDetector::kBassEnd; b++) s[b] = 0.05f;
  float elapsed = 0;
  while (elapsed < 350.f) {
    f = det.Feed(s.data(), 16.7f);
    elapsed += 16.7f;
  }
  CHECK(f.pulse < 0.06f);
  CHECK(f.pulse > 0.f);
}

TEST(beat_levels_follow_the_bands) {
  BeatDetector det;
  std::vector<float> s(kSpectrumBins, 0.f);
  for (int i = BeatDetector::kMidEnd; i < kSpectrumBins; i++) s[i] = 0.4f;  // only highs
  BeatFrame f;
  for (int i = 0; i < 120; i++) f = det.Feed(s.data(), 16.7f);
  CHECK(f.high > 0.9f);
  CHECK(f.bass < 0.01f);
}
