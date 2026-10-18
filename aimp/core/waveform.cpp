#include "waveform.h"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cwchar>
#include <cwctype>

namespace glass {
namespace {

constexpr char kMagic[4] = {'A', 'G', 'W', 'V'};
constexpr uint8_t kVersion = 1;
constexpr int kMaxColumns = 1 << 16;

uint8_t Q(float v) { return (uint8_t)std::lround(std::clamp(v, 0.f, 1.f) * 255.f); }

}  // namespace

WaveformBuilder::WaveformBuilder(int columns, int64_t totalFrames)
    : m_columns(std::clamp(columns, 1, kMaxColumns)), m_total(totalFrames) {
  if (m_total > 0) {
    m_binFrames = std::max<int64_t>(1, (m_total + m_columns - 1) / m_columns);
    m_bins.reserve(m_columns + 1);
  } else {
    m_bins.reserve(4 * (size_t)m_columns);
  }
}

void WaveformBuilder::Push(float v) {
  float a = std::fabs(v);
  m_cur.peak = std::max(m_cur.peak, a);
  m_cur.sumSq += (double)v * v;
  m_cur.n++;
  if (m_cur.n < m_binFrames) return;
  m_bins.push_back(m_cur);
  m_cur = Acc{};
  // Unknown length: keep at most 4x the wanted columns by merging neighbours and doubling the bin size.
  if (m_total <= 0 && (int)m_bins.size() >= 4 * m_columns) {
    std::vector<Acc> merged;
    merged.reserve(m_bins.size() / 2 + 1);
    for (size_t i = 0; i + 1 < m_bins.size(); i += 2) {
      Acc pair = m_bins[i];
      pair.peak = std::max(pair.peak, m_bins[i + 1].peak);
      pair.sumSq += m_bins[i + 1].sumSq;
      pair.n += m_bins[i + 1].n;
      merged.push_back(pair);
    }
    m_bins.swap(merged);
    m_binFrames *= 2;
  }
}

void WaveformBuilder::Add(const float* s, int frames, int channels) {
  if (!s || frames <= 0 || channels <= 0) return;
  for (int f = 0; f < frames; f++) {
    // The column shows the loudest channel; the mix would hide a one-sided sound.
    float v = 0;
    for (int c = 0; c < channels; c++) {
      float x = s[(size_t)f * channels + c];
      if (std::fabs(x) > std::fabs(v)) v = x;
    }
    Push(v);
  }
  m_frames += frames;
}

float WaveformBuilder::Progress() const {
  return m_total > 0 ? (float)std::min<double>(1.0, (double)m_frames / (double)m_total) : 0.f;
}

std::vector<WaveColumn> WaveformBuilder::Finish() {
  if (m_cur.n > 0) {
    m_bins.push_back(m_cur);
    m_cur = Acc{};
  }
  std::vector<WaveColumn> cols;
  cols.reserve(m_bins.size());
  for (const Acc& a : m_bins)
    cols.push_back({std::min(1.f, a.peak), a.n ? (float)std::min(1.0, std::sqrt(a.sumSq / a.n)) : 0.f});
  if ((int)cols.size() != m_columns) cols = ResampleWave(cols, m_columns);
  return cols;
}

std::vector<WaveColumn> ResampleWave(const std::vector<WaveColumn>& in, int columns) {
  columns = std::clamp(columns, 1, kMaxColumns);
  std::vector<WaveColumn> out((size_t)columns);
  if (in.empty()) return out;
  const double step = (double)in.size() / columns;
  for (int c = 0; c < columns; c++) {
    double a = c * step, b = (c + 1) * step;
    size_t i0 = (size_t)std::floor(a), i1 = std::min(in.size(), (size_t)std::ceil(b));
    if (i1 <= i0) i1 = std::min(in.size(), i0 + 1);
    float peak = 0;
    double e = 0, w = 0;
    for (size_t i = i0; i < i1; i++) {
      // Fraction of source column i inside [a, b).
      double cover = std::min<double>(b, (double)i + 1) - std::max<double>(a, (double)i);
      if (cover <= 0) cover = 1e-9;
      peak = std::max(peak, in[i].peak);
      e += cover * (double)in[i].rms * in[i].rms;
      w += cover;
    }
    out[c] = {peak, w > 0 ? (float)std::sqrt(e / w) : 0.f};
  }
  return out;
}

std::wstring WaveCacheKey(const std::wstring& path, uint64_t size, uint64_t mtime) {
  // FNV-1a over the lower-cased path, the size and the time: stable and dependency-free.
  uint64_t h = 1469598103934665603ull;
  auto mix = [&](uint64_t v) {
    for (int i = 0; i < 8; i++) {
      h ^= (v >> (i * 8)) & 0xFF;
      h *= 1099511628211ull;
    }
  };
  for (wchar_t ch : path) mix((uint64_t)std::towlower(ch));
  mix(size);
  mix(mtime);
  wchar_t buf[24];
  std::swprintf(buf, 24, L"%016llx.wave", (unsigned long long)h);
  return buf;
}

bool SaveWave(const std::wstring& file, const std::vector<WaveColumn>& wave) {
  if (wave.empty() || (int)wave.size() > kMaxColumns) return false;
  FILE* f = nullptr;
  if (_wfopen_s(&f, file.c_str(), L"wb") != 0 || !f) return false;
  uint32_t n = (uint32_t)wave.size();
  bool ok = std::fwrite(kMagic, 1, 4, f) == 4 && std::fwrite(&kVersion, 1, 1, f) == 1 && std::fwrite(&n, 4, 1, f) == 1;
  for (size_t i = 0; ok && i < wave.size(); i++) {
    uint8_t v[2] = {Q(wave[i].peak), Q(wave[i].rms)};
    ok = std::fwrite(v, 1, 2, f) == 2;
  }
  ok = std::fclose(f) == 0 && ok;
  if (!ok) _wremove(file.c_str());
  return ok;
}

bool LoadWave(const std::wstring& file, std::vector<WaveColumn>& wave) {
  FILE* f = nullptr;
  if (_wfopen_s(&f, file.c_str(), L"rb") != 0 || !f) return false;
  char magic[4];
  uint8_t ver = 0;
  uint32_t n = 0;
  bool ok = std::fread(magic, 1, 4, f) == 4 && std::equal(magic, magic + 4, kMagic) && std::fread(&ver, 1, 1, f) == 1 &&
            ver == kVersion && std::fread(&n, 4, 1, f) == 1 && n > 0 && n <= (uint32_t)kMaxColumns;
  std::vector<WaveColumn> out;
  if (ok) {
    std::vector<uint8_t> raw((size_t)n * 2);
    ok = std::fread(raw.data(), 1, raw.size(), f) == raw.size();
    if (ok) {
      out.resize(n);
      for (uint32_t i = 0; i < n; i++) out[i] = {raw[i * 2] / 255.f, raw[i * 2 + 1] / 255.f};
    }
  }
  std::fclose(f);
  if (ok) wave.swap(out);
  return ok;
}

}  // namespace glass
