// What the background does, measured on WARP renders: rays reach the window edges, a beat brightens the mass
// around the cover, switching the background off leaves the plain dark base, the glass stays dark.
#include <algorithm>
#include <cstdio>
#include <vector>

#include "../../aimp/core/layout.h"
#include "../test_framework.h"
#include "demo_model.h"
#include "render_util.h"

namespace {

constexpr UINT kW = 960, kH = 600;

rt::Image Render(const glass::Model& m, bool ui, bool background = true, float rays = 1.f) {
  rt::Image img;
  img.w = kW, img.h = kH;
  glass::SceneOptions o;
  o.background = background;
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
  rt::Image img = Render(m, false, false);
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
