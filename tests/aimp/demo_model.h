#pragma once
// A filled-in model for rendering tests: a synthetic cover, a short playlist, a waveform and audio levels.

#include <cmath>
#include <memory>

#include "../../aimp/core/model.h"
#include "../../aimp/core/ui_state.h"
#include "../../aimp/core/palette.h"
#include "../../aimp/core/search.h"
#include "../../aimp/core/ui_text.h"

namespace demo {

// A small landscape: a dusk sky, a sun, two ridges of hills. `hue` shifts the sky between variants.
inline std::shared_ptr<glass::Cover> MakeCover(int side, int variant) {
  auto c = std::make_shared<glass::Cover>();
  c->w = c->h = side;
  c->id = 1000 + variant;
  c->rgba.resize((size_t)side * side * 4);
  const float skyTop[3][3] = {{0.20f, 0.10f, 0.45f}, {0.05f, 0.25f, 0.40f}, {0.35f, 0.08f, 0.20f}};
  const float skyLow[3][3] = {{0.98f, 0.55f, 0.35f}, {0.30f, 0.85f, 0.75f}, {0.98f, 0.75f, 0.30f}};
  const float* top = skyTop[variant % 3];
  const float* low = skyLow[variant % 3];
  for (int y = 0; y < side; y++)
    for (int x = 0; x < side; x++) {
      float u = (float)x / side, v = (float)y / side;
      float col[3];
      for (int k = 0; k < 3; k++) col[k] = top[k] + (low[k] - top[k]) * std::pow(v, 1.3f);
      float d = std::hypot(u - 0.62f, v - 0.52f);
      if (d < 0.14f) col[0] = 1.0f, col[1] = 0.93f, col[2] = 0.75f;
      else {
        float glow = std::exp(-d * d * 18.f) * 0.35f;
        for (int k = 0; k < 3; k++) col[k] += glow;
      }
      float h1 = 0.66f + 0.06f * std::sin(u * 7.0f) + 0.03f * std::sin(u * 19.0f + 1.0f);
      float h2 = 0.80f + 0.05f * std::sin(u * 5.0f + 2.0f);
      if (v > h1) col[0] = 0.30f, col[1] = 0.14f, col[2] = 0.28f;
      if (v > h2) col[0] = 0.12f, col[1] = 0.06f, col[2] = 0.15f;
      uint8_t* p = &c->rgba[((size_t)y * side + x) * 4];
      for (int k = 0; k < 3; k++) p[k] = (uint8_t)std::lround(std::fmin(1.f, std::fmax(0.f, col[k])) * 255);
      p[3] = 255;
    }
  return c;
}

inline glass::Model MakeModel(int variant = 0) {
  using namespace glass;
  Model m;
  m.track.title = variant == 1 ? L"Северное сияние над городом, расширенная версия" : L"Стеклянный горизонт";
  m.track.artist = L"Lumen Coast";
  m.track.album = L"Ночные огни";
  m.track.duration = 227;
  m.track.format = L"FLAC", m.track.sampleRate = 44100, m.track.bits = 16, m.track.bitrate = 867;
  m.track.favorite = variant == 2;
  m.state = variant == 3 ? PlayState::Paused : PlayState::Playing;
  m.position = 84;
  m.volume = 0.68f;
  m.repeat = Repeat::All;
  {
    auto cover = MakeCover(512, variant);
    if (variant == 6 || variant == 11 || variant == 12) {  // a black-and-white cover
      for (size_t i = 0; i + 3 < cover->rgba.size(); i += 4) {
        uint8_t g = (uint8_t)((cover->rgba[i] * 54 + cover->rgba[i + 1] * 183 + cover->rgba[i + 2] * 19) >> 8);
        cover->rgba[i] = cover->rgba[i + 1] = cover->rgba[i + 2] = g;
      }
      cover->id = 2000;
    }
    m.cover = cover;
  }
  m.palette = ExtractPalette(m.cover->rgba.data(), m.cover->w, m.cover->h, m.cover->w * 4);
  m.bass = 0.6f, m.high = 0.4f, m.pulse = variant == 0 ? 0.8f : 0.2f;
  m.time = 12.5f;
  // A music-like spectrum: strong lows falling off towards the highs, with a few uneven bumps.
  for (int i = 0; i < SpectrumBars::kCount; i++) {
    float x = (float)i / SpectrumBars::kCount;
    float bumps = 0.14f * std::pow(std::sin(i * 0.73f + 0.5f), 2.f) + 0.09f * std::sin(i * 1.91f + 1.3f) +
                  0.06f * std::sin(i * 3.1f);
    m.spectrum.level[i] = std::fmin(1.f, std::fmax(0.f, 0.20f + 0.60f * std::exp(-x * 2.4f) + bumps * (1.2f - x)));
  }
  for (int i = 0; i < 600; i++) {
    float t = i / 600.f;
    float env = 0.35f + 0.5f * std::fabs(std::sin(t * 9.f)) * (0.6f + 0.4f * std::sin(t * 2.3f));
    m.wave.columns.push_back({std::fmin(1.f, env * 1.2f), env * 0.65f});
  }
  m.nav = Nav::Playlists;
  m.navCount[0] = 3, m.navCount[1] = 12, m.navCount[3] = 47;
  m.listTitle = L"ПЛЕЙЛИСТ";
  const wchar_t* titles[] = {L"Стеклянный горизонт", L"Неоновое послесвечение", L"Медленный свет", L"Полярная лента",
                             L"Жидкие сны", L"Тёмный прибой", L"Огни над водой", L"Последний поезд"};
  const wchar_t* artists[] = {L"Lumen Coast", L"Kaito & Mira", L"Elliot Vale", L"Nordic Bloom",
                              L"Aeva", L"Sable", L"Lumen Coast", L"Harbor Lights"};
  const double durs[] = {227, 242, 305, 234, 258, 192, 211, 276};
  for (int i = 0; i < 8; i++) {
    ListRow r;
    r.title = titles[i];
    r.artist = artists[i];
    r.album = i % 2 ? L"Ambient City" : L"Ночные огни";
    r.duration = durs[i];
    r.playing = i == 0;
    r.search = NormalizeForSearch(r.title + L" " + r.artist + L" " + r.album);
    m.rows.push_back(r);
  }
  ApplyQuery(m);
  m.aimpFading = true;  // AIMP's own setting, shown in quick settings
  m.quickSettingsOpen = variant == 4;
  m.theme = variant == 5 || variant == 10 ? Theme::Light : Theme::Dark;
  if (variant == 10) m.settings.mono = 100;  // light theme in black and white: the negative
  if (variant == 7 || variant == 9 || variant == 11 || variant == 12) m.settings.frost = 0;   // clear glass
  if (variant == 8 || variant == 9) m.settings.mono = 100;  // black and silver
  m.bpm = 124, m.loud = 0.8f;
  if (variant == 12) m.time = 3600;  // an hour after the window opened
  return m;
}

// The "⋯" menu open, as the window builds it: for the demo and golden shots.
inline glass::UiState MoreMenuOpen(unsigned w, unsigned h, float scale) {
  glass::Layout L = glass::ComputeLayout(w / scale, h / scale);
  glass::UiState s;
  s.menu.kind = glass::MenuKind::More;
  s.menu.anchor = L.btnMore;
  s.menu.alignRight = true;
  for (const wchar_t* t : {L"Показать файл в папке", L"Все настройки AIMP\u2026", L"", L"Вернуть классический AIMP"}) {
    glass::PopupItem it;
    it.text = t;
    it.separator = !*t;
    s.menu.items.push_back(it);
  }
  s.menu.hot = 1;
  return s;
}

}  // namespace demo
