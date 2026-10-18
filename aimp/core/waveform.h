#pragma once
// Whole-track waveform for the seek bar: decoded samples are folded into a fixed number of columns, each
// with its peak and RMS level, and cached on disk so a track is decoded once.

#include <cstdint>
#include <string>
#include <vector>

namespace glass {

struct WaveColumn {
  float peak = 0;  // max |sample| over the column, 0..1
  float rms = 0;   // root mean square over the column, 0..1
};

class WaveformBuilder {
 public:
  // columns: output width; totalFrames: frames in the track, or <= 0 when unknown (then the columns adapt
  // while reading and are resampled at the end).
  WaveformBuilder(int columns, int64_t totalFrames);
  // Interleaved float samples, `frames` frames of `channels` channels.
  void Add(const float* samples, int frames, int channels);
  float Progress() const;  // 0..1, or 0 when the length is unknown
  std::vector<WaveColumn> Finish();

 private:
  struct Acc {
    float peak = 0;
    double sumSq = 0;
    int64_t n = 0;
  };
  void Push(float v);
  int m_columns;
  int64_t m_total, m_frames = 0;
  int64_t m_binFrames = 1;  // frames per accumulator bin
  std::vector<Acc> m_bins;
  Acc m_cur;
};

// Resamples columns to a new count (for a narrower or wider seek bar): each output column takes the max
// peak and the energy-weighted RMS of the source columns it covers.
std::vector<WaveColumn> ResampleWave(const std::vector<WaveColumn>& in, int columns);

// Cache file name for a track: stable for the same path, size and modification time.
std::wstring WaveCacheKey(const std::wstring& path, uint64_t size, uint64_t mtime);
// Quantised to 8 bits per value; false on a missing, foreign or damaged file.
bool SaveWave(const std::wstring& file, const std::vector<WaveColumn>& wave);
bool LoadWave(const std::wstring& file, std::vector<WaveColumn>& wave);

}  // namespace glass
