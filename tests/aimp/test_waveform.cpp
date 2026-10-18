// Waveform folding, resampling and the on-disk cache.
#include <windows.h>

#include <cmath>
#include <string>
#include <vector>

#include "../../aimp/core/waveform.h"
#include "../test_framework.h"

using namespace glass;

namespace {

std::vector<float> Sine(int frames, int channels, float amp, float periodFrames) {
  std::vector<float> s((size_t)frames * channels);
  for (int f = 0; f < frames; f++)
    for (int c = 0; c < channels; c++) s[(size_t)f * channels + c] = amp * std::sin(6.2831853f * f / periodFrames);
  return s;
}

std::wstring TempFile(const wchar_t* name) {
  wchar_t dir[MAX_PATH];
  GetTempPathW(MAX_PATH, dir);
  return std::wstring(dir) + name + std::to_wstring(GetCurrentProcessId());
}

}  // namespace

TEST(wave_sine_has_flat_peak_and_rms) {
  const int frames = 44100 * 3;
  std::vector<float> s = Sine(frames, 2, 0.5f, 100.f);
  WaveformBuilder b(256, frames);
  for (int off = 0; off < frames; off += 4096) b.Add(s.data() + (size_t)off * 2, std::min(4096, frames - off), 2);
  CHECK_NEAR(b.Progress(), 1.f, 1e-6);
  std::vector<WaveColumn> w = b.Finish();
  CHECK_EQ(w.size(), (size_t)256);
  for (const WaveColumn& c : w) {
    CHECK_NEAR(c.peak, 0.5f, 0.01);
    CHECK_NEAR(c.rms, 0.5f / std::sqrt(2.f), 0.01);
  }
}

TEST(wave_silence_then_loud) {
  const int frames = 100000;
  std::vector<float> s((size_t)frames, 0.f);
  for (int f = frames / 2; f < frames; f++) s[f] = (f % 2) ? 0.9f : -0.9f;
  WaveformBuilder b(100, frames);
  b.Add(s.data(), frames, 1);
  std::vector<WaveColumn> w = b.Finish();
  CHECK_NEAR(w[10].peak, 0.f, 1e-6);
  CHECK_NEAR(w[90].peak, 0.9f, 1e-5);
  CHECK_NEAR(w[90].rms, 0.9f, 1e-4);
}

TEST(wave_loudest_channel_wins) {
  // Left silent, right loud: the column must not average the sound away.
  std::vector<float> s(2000 * 2, 0.f);
  for (int f = 0; f < 2000; f++) s[(size_t)f * 2 + 1] = -0.7f;
  WaveformBuilder b(10, 2000);
  b.Add(s.data(), 2000, 2);
  for (const WaveColumn& c : b.Finish()) CHECK_NEAR(c.peak, 0.7f, 1e-5);
}

TEST(wave_unknown_length_matches_known_length) {
  const int frames = 200000;
  std::vector<float> s((size_t)frames);
  for (int f = 0; f < frames; f++) s[f] = 0.8f * (float)f / frames * ((f % 2) ? 1.f : -1.f);  // rising ramp
  WaveformBuilder known(64, frames), unknown(64, 0);
  known.Add(s.data(), frames, 1);
  for (int off = 0; off < frames; off += 1000) unknown.Add(s.data() + off, 1000, 1);
  CHECK_NEAR(unknown.Progress(), 0.f, 1e-6);
  std::vector<WaveColumn> a = known.Finish(), b = unknown.Finish();
  CHECK_EQ(a.size(), b.size());
  for (size_t i = 0; i < a.size(); i++) CHECK_NEAR(a[i].peak, b[i].peak, 0.03);
}

TEST(wave_resample_keeps_peaks) {
  std::vector<WaveColumn> in(100, WaveColumn{0.1f, 0.05f});
  in[37] = {1.f, 0.6f};
  std::vector<WaveColumn> out = ResampleWave(in, 10);
  CHECK_EQ(out.size(), (size_t)10);
  CHECK_NEAR(out[3].peak, 1.f, 1e-6);  // the spike survives in the column covering it
  CHECK_NEAR(out[0].peak, 0.1f, 1e-6);
  std::vector<WaveColumn> wide = ResampleWave(in, 400);
  CHECK_EQ(wide.size(), (size_t)400);
  CHECK_NEAR(wide[37 * 4 + 1].peak, 1.f, 1e-6);
}

TEST(wave_empty_input_gives_silent_columns) {
  WaveformBuilder b(32, 0);
  std::vector<WaveColumn> w = b.Finish();
  CHECK_EQ(w.size(), (size_t)32);
  CHECK_NEAR(w[5].peak, 0.f, 1e-6);
}

TEST(wave_cache_round_trip_and_rejects_garbage) {
  std::vector<WaveColumn> w;
  for (int i = 0; i < 300; i++) w.push_back({(i % 17) / 16.f, (i % 5) / 8.f});
  std::wstring file = TempFile(L"agwv_test_");
  CHECK(SaveWave(file, w));
  std::vector<WaveColumn> r;
  CHECK(LoadWave(file, r));
  CHECK_EQ(r.size(), w.size());
  for (size_t i = 0; i < w.size(); i++) {
    CHECK_NEAR(r[i].peak, w[i].peak, 1.f / 255);
    CHECK_NEAR(r[i].rms, w[i].rms, 1.f / 255);
  }
  // A file that is not ours.
  FILE* f = nullptr;
  CHECK(_wfopen_s(&f, file.c_str(), L"wb") == 0 && f);
  if (f) {
    std::fputs("not a wave", f);
    std::fclose(f);
  }
  std::vector<WaveColumn> keep = {{0.5f, 0.5f}};
  CHECK(!LoadWave(file, keep));
  CHECK_EQ(keep.size(), (size_t)1);  // untouched on failure
  _wremove(file.c_str());
  CHECK(!LoadWave(file, keep));
}

TEST(wave_cache_key_is_stable_and_specific) {
  std::wstring a = WaveCacheKey(L"D:\\Music\\Track.flac", 1000, 42);
  CHECK(a == WaveCacheKey(L"d:\\music\\track.FLAC", 1000, 42));  // case of the path does not matter
  CHECK(a != WaveCacheKey(L"D:\\Music\\Track.flac", 1001, 42));
  CHECK(a != WaveCacheKey(L"D:\\Music\\Track.flac", 1000, 43));
  CHECK(a.size() == 21);  // 16 hex digits + ".wave"
}
