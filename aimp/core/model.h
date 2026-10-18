#pragma once
// Everything the window shows, as plain data. The plugin fills it from AIMP; tests fill it by hand.

#include <cstdint>
#include <memory>
#include <string>
#include <vector>

#include "glass_settings.h"
#include "palette.h"
#include "waveform.h"

namespace glass {

enum class PlayState { Stopped, Paused, Playing };
enum class Repeat { Off, All, One };
enum class Nav { Playlists, Folders, Recent, Favorites };
enum class Theme { Dark, Light, Amoled };

struct Cover {
  int w = 0, h = 0;
  std::vector<uint8_t> rgba;  // 8-bit sRGB, R G B A, rows w * 4 bytes
  uint64_t id = 0;            // changes whenever the picture changes
  bool Empty() const { return w <= 0 || h <= 0 || rgba.empty(); }
};

struct TrackInfo {
  std::wstring title, artist, album, path;
  double duration = 0;  // seconds
  bool favorite = false;
};

struct ListRow {
  std::wstring title, artist, album;
  double duration = 0;
  std::wstring search;  // NormalizeForSearch(title + artist + album + file name), filled once
  bool playing = false;
  bool folder = false;  // a folder in the Folders view (opens instead of playing)
};

struct Wave {
  std::vector<WaveColumn> columns;  // empty while not built
  float progress = 0;               // 0..1 while building
};

struct SpectrumBars {
  static constexpr int kCount = 18;
  float level[kCount] = {};  // 0..1, smoothed
};

struct Model {
  // Player
  TrackInfo track;
  PlayState state = PlayState::Stopped;
  double position = 0;  // seconds
  float volume = 1;     // 0..1
  bool shuffle = false;
  Repeat repeat = Repeat::Off;
  std::shared_ptr<const Cover> cover;
  Palette palette = DefaultPalette();
  Wave wave;
  SpectrumBars spectrum;
  float bass = 0, high = 0, pulse = 0;  // from the beat detector

  // Lists
  Nav nav = Nav::Playlists;
  int navCount[4] = {};
  std::wstring listTitle;     // "ПЛЕЙЛИСТ", a playlist name, a folder name...
  std::vector<ListRow> rows;  // the current list, unfiltered
  std::wstring query;         // search box text
  std::vector<int> visible;   // indices into rows that match the query, in order

  // Settings: the shell's own, and AIMP's playback switches shown in quick settings
  GlassSettings settings;
  bool aimpFading = false;

  // Window
  bool quickSettingsOpen = false;
  Theme theme = Theme::Dark;
  float time = 0;  // seconds since start, drives the background animation
};

}  // namespace glass
