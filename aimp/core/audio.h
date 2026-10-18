#pragma once
// Turns the player's spectrum frames (any thread, ~30 per second) into what the window animates: the 48 bands
// of the spectrum next to the cover and the beat for the background. The window reads the latest snapshot every frame.

#include <mutex>

#include "beat.h"
#include "model.h"

namespace glass {

struct AudioFrame {
  float bars[SpectrumBars::kCount] = {};
  float bass = 0, high = 0, pulse = 0;
  unsigned beats = 0;  // counts beats, so a reader can tell a new one arrived
  float bpm = 0;       // tempo from the beats, 70..180; 0 while unknown (too few beats, or none for a while)
  float loud = 0;      // loudness relative to the loudest of the last ~10 s, 0..1, smoothed
};

class AudioAnalyzer {
 public:
  // spectrum: 256 bins (see beat.h); dtMs since the previous frame.
  void Feed(const float* spectrum, float dtMs);
  // Silence (stopped player): everything decays to zero, and the tempo is forgotten.
  void Silence(float dtMs);
  // A new track: its tempo and loudness are measured afresh, not mixed with the previous track's.
  void NewTrack();
  AudioFrame Snapshot() const;

  // First and one-past-last spectrum bin of bar i: log-spaced from ~40 Hz to ~16 kHz.
  static void BarBins(int bar, int& from, int& to);

 private:
  mutable std::mutex m_mu;
  BeatDetector m_beat;
  AudioFrame m_frame;
  float m_peak[SpectrumBars::kCount] = {};
  void ForgetTempo();  // m_mu held

  double m_now = 0, m_lastBeat = -1e12;  // ms of fed audio (double: hours of playback keep ms precision)
  float m_loudPeak = 1e-6f;
  float m_intervals[16] = {};
  int m_intervalCount = 0, m_intervalHead = 0;
};

}  // namespace glass
