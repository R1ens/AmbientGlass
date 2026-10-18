// What the background does, measured on WARP renders: rays reach the window edges, a beat brightens the mass
// around the cover, switching the background off leaves the plain dark base, the glass stays dark.
#include <algorithm>
#include <cmath>
#include <cstdio>
#include <vector>

#include "../../aimp/core/layout.h"
#include "../../aimp/core/ui_state.h"
#include "../test_framework.h"
#include "demo_model.h"
#include "render_util.h"

namespace {

constexpr UINT kW = 960, kH = 600;

rt::Image Render(const glass::Model& m, bool ui, bool background = true, float rays = 1.f, bool coverGlow = true) {
  rt::Image img;
  img.w = kW, img.h = kH;
  glass::SceneOptions o;
  o.background = background;
  o.coverGlow = coverGlow;
  o.rays = rays;
  o.dither = 0;  // exact comparisons
  if (rt::Warp()) rt::Warp()->Render(m, kW, kH, 1.f, o, ui, img.bgra);
  return img;
}

// Mean luminance of the background between the panels (gaps and margins), where nothing covers the ribbons.
float GapLuma(const rt::Image& img, const glass::Layout& L) {
  double sum = 0;
  int n = 0;
  const glass::Rc* panels[] = {&L.sidebar, &L.card, &L.list, &L.bottom, &L.titlebar};
  for (UINT y = 0; y < img.h; y += 2)
    for (UINT x = 0; x < img.w; x += 2) {
      bool covered = false;
      for (const glass::Rc* p : panels) covered = covered || p->Contains((float)x, (float)y);
      if (covered) continue;
      sum += img.Luma(x, y);
      n++;
    }
  return n ? (float)(sum / n) : 0.f;
}

}  // namespace

TEST(render_warp_available) { CHECK(rt::Warp() != nullptr); }

TEST(render_background_off_is_flat_dark) {
  if (!rt::Warp()) return;
  glass::Model m = demo::MakeModel(0);
  rt::Image img = Render(m, false, false, 1.f, false);  // the cover's own glow is a separate switch
  glass::Layout L = glass::ComputeLayout(kW, kH);
  // The gaps show only the base colour (10, 10, 14) darkened by the vignette and panel shadows.
  CHECK(GapLuma(img, L) < 14.f);
}

TEST(render_rays_reach_the_window_edges) {
  if (!rt::Warp()) return;
  glass::Model m = demo::MakeModel(0);
  m.pulse = 1.f;
  rt::Image off = Render(m, false, false);
  // The right edge is the far side from the cover (about six cover half-sides away): the mass and the
  // filaments fade out long before it, so light there can only come from the rays.
  auto farEdgeGain = [&](const rt::Image& on) {
    float best = 0;
    for (UINT y = 0; y < kH; y++)
      for (UINT x : {kW - 6, kW - 4, kW - 2}) best = std::max(best, on.Luma(x, y) - off.Luma(x, y));
    return best;
  };
  float withRays = farEdgeGain(Render(m, false, true)), noRays = farEdgeGain(Render(m, false, true, 0.f));
  std::printf("  rays at the far edge: +%.1f luma, without rays +%.1f\n", withRays, noRays);
  CHECK(withRays > 6.f);
  CHECK(withRays > noRays * 2.f + 3.f);
}

TEST(render_beat_brightens_the_mass) {
  if (!rt::Warp()) return;
  glass::Model calm = demo::MakeModel(0), beat = demo::MakeModel(0);
  calm.pulse = 0, calm.bass = 0.2f;
  beat.pulse = 1, beat.bass = 0.9f;
  rt::Image a = Render(calm, false), b = Render(beat, false);
  glass::Layout L = glass::ComputeLayout(kW, kH);
  // Inside the card around the cover (the glass shows the mass behind it).
  double sa = 0, sb = 0;
  int n = 0;
  for (UINT y = (UINT)L.card.t + 4; y < (UINT)L.card.b - 4; y += 2)
    for (UINT x = (UINT)L.card.l + 4; x < (UINT)L.info.l; x += 2) {
      sa += a.Luma(x, y), sb += b.Luma(x, y), n++;
    }
  CHECK(n > 0);
  CHECK(sb / n > sa / n * 1.10);  // at least 10% brighter on the beat
}

namespace {
// Mean absolute difference between horizontally neighbouring pixels: how much detail a region shows.
double Detail(const rt::Image& img, const glass::Rc& r) {
  double s = 0;
  int n = 0;
  for (UINT y = (UINT)r.t; y < (UINT)r.b; y += 2)
    for (UINT x = (UINT)r.l; x + 2 < (UINT)r.r; x += 2) s += std::fabs(img.Luma(x + 2, y) - img.Luma(x, y)), n++;
  return n ? s / n : 0;
}
}  // namespace

TEST(render_clear_glass_shows_the_background_sharp) {
  if (!rt::Warp()) return;
  glass::Model frosted = demo::MakeModel(0), clear = demo::MakeModel(0);
  clear.settings.frost = 0;
  rt::Image a = Render(frosted, false), b = Render(clear, false);
  glass::Layout L = glass::ComputeLayout(kW, kH);
  // Deep inside the card (away from the frosted rim) the clear glass shows the rays' detail.
  glass::Rc in{L.card.l + 30, L.card.t + 30, L.card.r - 30, L.card.b - 30};
  CHECK(Detail(b, in) > Detail(a, in) * 1.5 + 0.2);
  // The rim stays frosted: right at the edge both look alike.
  glass::Rc rim{L.card.l + 2, L.card.t + 40, L.card.l + 5, L.card.b - 40};
  double d = 0;
  int n = 0;
  for (UINT y = (UINT)rim.t; y < (UINT)rim.b; y += 3) d += std::fabs(a.Luma((UINT)rim.l, y) - b.Luma((UINT)rim.l, y)), n++;
  CHECK(d / n < 6);
}

TEST(render_black_and_silver_has_no_colour_in_the_background) {
  if (!rt::Warp()) return;
  glass::Model m = demo::MakeModel(0);
  m.settings.mono = 100;
  rt::Image img = Render(m, false);
  double chroma = 0;
  int n = 0;
  for (UINT y = 0; y < kH; y += 4)
    for (UINT x = 0; x < kW; x += 4) {
      const uint8_t* p = &img.bgra[((size_t)y * kW + x) * 4];
      int mx = std::max({p[0], p[1], p[2]}), mn = std::min({p[0], p[1], p[2]});
      chroma += mx - mn, n++;
    }
  CHECK(chroma / n < 4);  // only the faint cool cast of silver
}

TEST(render_menu_is_drawn_inside_the_window_without_dimming_it) {
  if (!rt::Warp()) return;
  glass::Model m = demo::MakeModel(0);
  glass::Layout L = glass::ComputeLayout(kW, kH);
  glass::UiState s;
  s.menu.kind = glass::MenuKind::More;
  s.menu.anchor = L.btnMore;
  s.menu.alignRight = true;
  for (const wchar_t* t : {L"Показать файл в папке", L"Все настройки AIMP", L"Вернуть классический AIMP"}) {
    glass::PopupItem it;
    it.text = t;
    s.menu.items.push_back(it);
  }
  rt::Image plain, open;
  plain.w = open.w = kW, plain.h = open.h = kH;
  glass::SceneOptions o;
  o.dither = 0;
  rt::Warp()->Render(m, kW, kH, 1.f, o, true, plain.bgra);
  rt::Warp()->Render(m, kW, kH, 1.f, o, true, open.bgra, &s);
  auto diff = [&](const glass::Rc& r) {
    double d = 0;
    int n = 0;
    for (UINT y = (UINT)std::max(0.f, r.t); y < (UINT)std::min((float)kH, r.b); y += 2)
      for (UINT x = (UINT)std::max(0.f, r.l); x < (UINT)std::min((float)kW, r.r); x += 2)
        d += std::fabs(open.Luma(x, y) - plain.Luma(x, y)), n++;
    return n ? d / n : 0;
  };
  glass::Rc menu = glass::MenuRect(L, s.menu);
  CHECK(diff(menu) > 4);                            // the menu is there
  CHECK(diff(glass::Rc{0, 0, 200, 200}) < 0.5);      // the rest of the window keeps its brightness
}

TEST(render_pattern_keeps_its_look_over_hours) {
  if (!rt::Warp()) return;
  // How fine the background is: how many light threads a circle around the cover crosses (local brightness
  // peaks along it), averaged over a few radii and moments. Seen through clear glass, so nothing blurs it.
  auto threads = [](float time) {
    glass::Layout L = glass::ComputeLayout(kW, kH);
    double total = 0;
    int runs = 0;
    for (float dt : {0.f, 1.f, 2.f}) {
      glass::Model m = demo::MakeModel(0);
      m.time = time + dt;
      m.pulse = m.bass = m.high = 0;
      m.settings.frost = 0;
      rt::Image img = Render(m, false);
      for (float k : {0.75f, 0.95f, 1.15f}) {
        const float r = L.cover.W() * k;
        std::vector<double> ring;
        for (int i = 0; i < 1440; i++) {
          float a = i * 6.2831853f / 1440;
          int x = (int)(L.cover.Cx() + r * std::cos(a)), y = (int)(L.cover.Cy() + r * std::sin(a));
          if (x >= 0 && y >= 0 && x < (int)kW && y < (int)kH) ring.push_back(img.Luma((UINT)x, (UINT)y));
        }
        int peaks = 0;
        for (size_t i = 2; i + 2 < ring.size(); i++)
          if (ring[i] > ring[i - 2] + 1.5 && ring[i] > ring[i + 2] + 1.5 && ring[i] >= ring[i - 1] && ring[i] >= ring[i + 1]) peaks++;
        total += peaks, runs++;
      }
    }
    return total / runs;
  };
  const double start = threads(12.5f), hour = threads(3600.f), long_ = threads(36000.f);
  std::printf("  threads around the cover: %.1f at 12 s, %.1f after an hour, %.1f after 10 hours\n", start, hour, long_);
  CHECK(hour < start * 1.4 && hour > start * 0.7);
  CHECK(long_ < start * 1.4 && long_ > start * 0.7);
}

TEST(render_cover_glow_lights_the_card_around_the_cover) {
  if (!rt::Warp()) return;
  glass::Model m = demo::MakeModel(0);
  glass::SceneOptions on, off;
  on.dither = off.dither = 0;
  off.coverGlow = false;
  rt::Image a, b;
  a.w = b.w = kW, a.h = b.h = kH;
  rt::Warp()->Render(m, kW, kH, 1.f, on, false, a.bgra);
  rt::Warp()->Render(m, kW, kH, 1.f, off, false, b.bgra);
  glass::Layout L = glass::ComputeLayout(kW, kH);
  // A ring around the cover, inside the card: where the glow should show.
  double d = 0, peak = 0;
  int n = 0;
  for (UINT y = (UINT)L.card.t + 4; y < (UINT)L.card.b - 4; y += 2)
    for (UINT x = (UINT)L.card.l + 4; x < (UINT)L.info.l; x += 2) {
      if (L.cover.Contains((float)x, (float)y)) continue;
      double v = a.Luma(x, y) - b.Luma(x, y);
      d += v, n++, peak = std::max(peak, v);
    }
  std::printf("  cover glow around the cover: +%.1f luma on average, +%.1f at most\n", d / n, peak);
  CHECK(d / n > 12);  // a halo you can see, not a hint
}

TEST(render_light_theme_in_black_and_white_is_a_negative) {
  if (!rt::Warp()) return;
  glass::Model m = demo::MakeModel(5);  // light theme
  m.settings.mono = 100;
  m.settings.frost = 0;  // clear glass: the background shows everywhere
  rt::Image img = Render(m, false);
  // White background, the light drawn in black, no colour.
  std::vector<double> luma;
  double chroma = 0;
  for (UINT y = 0; y < kH; y += 4)
    for (UINT x = 0; x < kW; x += 4) {
      luma.push_back(img.Luma(x, y));
      const uint8_t* p = &img.bgra[((size_t)y * kW + x) * 4];
      chroma += std::max({p[0], p[1], p[2]}) - std::min({p[0], p[1], p[2]});
    }
  std::sort(luma.begin(), luma.end());
  const double median = luma[luma.size() / 2], dark = luma[luma.size() / 50];
  std::printf("  light negative: median luma %.0f, darkest 2%% %.0f\n", median, dark);
  CHECK(median > 170);
  CHECK(dark < median - 60);  // the light shows as dark marks
  CHECK(chroma / luma.size() < 6);
}

TEST(render_glass_stays_dark) {
  if (!rt::Warp()) return;
  glass::Model m = demo::MakeModel(0);
  m.pulse = 1, m.bass = 1;
  rt::Image img = Render(m, false);
  glass::Layout L = glass::ComputeLayout(kW, kH);
  // The playlist panel is far from the cover: dark glass, readable text on it.
  double s = 0;
  int n = 0;
  for (UINT y = (UINT)L.list.t + 30; y < (UINT)L.list.b - 10; y += 3)
    for (UINT x = (UINT)L.list.l + 10; x < (UINT)L.list.r - 10; x += 3) s += img.Luma(x, y), n++;
  CHECK(s / n < 70);
}
