// ComputeLayouts / CaptureBox on hand-built VlcState values (no windows involved: HWNDs are fake).
#include <windows.h>

#include <cmath>
#include <cstdint>
#include <string>

#include "common.h"
#include "glow_math.h"
#include "layout.h"
#include "test_framework.h"

namespace {

HWND FakeHwnd(intptr_t v) { return reinterpret_cast<HWND>(v); }

std::string RectStr(const RECT& r) {
  return "{" + std::to_string(r.left) + "," + std::to_string(r.top) + "," + std::to_string(r.right) + "," +
         std::to_string(r.bottom) + "}";
}

bool CheckRect(const char* file, int line, const char* what, const RECT& got, const RECT& want) {
  if (SameRect(got, want)) return true;
  ::test::Fail(file, line, std::string(what) + " == " + RectStr(want) + "  [got " + RectStr(got) + "]");
  return false;
}
// Variadic so a brace-initialised RECT{l, t, r, b} can be passed directly as the expected value.
#define CHECK_RECT(got, ...) CheckRect(__FILE__, __LINE__, #got, (got), (__VA_ARGS__))

// VLC windowed on a 1920x1080 monitor: 1280x800 window, a 1280x720 video area under a 30 px toolbar.
VlcState Windowed() {
  VlcState v;
  v.main = FakeHwnd(1);
  v.videoMain = FakeHwnd(2);
  v.videoOut = FakeHwnd(3);
  v.pid = 1234;
  v.mainRect = RECT{100, 100, 1380, 900};
  v.videoMainRect = RECT{100, 130, 1380, 850};
  v.pictureRect = v.videoMainRect;  // 16:9 picture fills the video area exactly
  v.monitorRect = RECT{0, 0, 1920, 1080};
  v.fullscreen = false;
  return v;
}

// VLC fullscreen on a 3840x1080 ultra-wide with a 1920x1080 picture centred.
VlcState FullscreenUltrawide() {
  VlcState v;
  v.main = FakeHwnd(1);
  v.videoMain = FakeHwnd(2);
  v.videoOut = FakeHwnd(3);
  v.mainRect = RECT{0, 0, 3840, 1080};
  v.videoMainRect = RECT{0, 0, 3840, 1080};
  v.pictureRect = RECT{960, 0, 2880, 1080};
  v.monitorRect = RECT{0, 0, 3840, 1080};
  v.fullscreen = true;
  return v;
}

}  // namespace

TEST(layout_fullscreen_pillarbox_inner_only) {
  VlcState v = FullscreenUltrawide();
  CHECK(v.Active());
  Layouts L = ComputeLayouts(v, Bars{}, Settings{});

  CHECK(L.inner);
  CHECK_RECT(L.innerL.overlay, v.videoMainRect);
  CHECK_RECT(L.content, v.pictureRect);
  CHECK_RECT(L.innerL.hole, RECT{961, 0, 2879, 1080});  // 1 px into the picture where the glow is
  CHECK_RECT(L.innerL.frame, v.pictureRect);
  CHECK_RECT(L.innerL.scale, v.pictureRect);
  CHECK_RECT(L.innerL.pass, v.pictureRect);
  CHECK(!L.innerL.usePass);
  CHECK(L.innerL.opaque);
  CHECK_EQ(L.contentUV[0], 0.f);
  CHECK_EQ(L.contentUV[1], 0.f);
  CHECK_EQ(L.contentUV[2], 1.f);
  CHECK_EQ(L.contentUV[3], 1.f);

  CHECK(!L.outer);  // never around the window in fullscreen
}

TEST(layout_windowed_exact_fit_outer_only) {
  VlcState v = Windowed();
  Settings s;
  s.aroundWindow = true;  // off by default
  Layouts L = ComputeLayouts(v, Bars{}, s);

  CHECK(!L.inner);  // the picture fills the video area: no black to glow in
  CHECK(L.outer);

  // Overlay: the window grown by the glow reach (whole px, rounded up), clipped to the monitor.
  float reachF = GlowReach(s, 1280.f, 720.f);
  LONG reach = (LONG)std::ceil(reachF);
  // Defaults: 0.17*640 + 3*(30/512*1.275*720) + 2 = 108.8 + 161.37 + 2 = 272.17 -> 273
  CHECK_NEAR(reachF, 272.17, 0.01);
  CHECK_EQ(reach, 273);
  RECT want{std::max(0L, 100 - reach), std::max(0L, 100 - reach), std::min(1920L, 1380 + reach),
            std::min(1080L, 900 + reach)};
  CHECK_RECT(L.outerL.overlay, want);
  CHECK_RECT(L.outerL.overlay, RECT{0, 0, 1653, 1080});  // clipped at the monitor's left/top/bottom
  CHECK_RECT(L.outerL.frame, v.mainRect);
  CHECK_RECT(L.outerL.scale, v.pictureRect);
  CHECK(!L.outerL.usePass);
  CHECK(!L.outerL.opaque);  // drawn on the desktop, not over VLC's bars
}

TEST(layout_outer_overlay_not_clipped_when_room) {
  VlcState v = Windowed();
  v.monitorRect = RECT{-1000, -1000, 5000, 5000};
  Settings s;
  s.aroundWindow = true;  // off by default
  s.spread = 10;
  s.blur = 0;  // reach = 0.1 * 640 + 0 + 2 = 66
  Layouts L = ComputeLayouts(v, Bars{}, s);
  CHECK(L.outer);
  CHECK_EQ((LONG)std::ceil(GlowReach(s, 1280.f, 720.f)), 66L);
  CHECK_RECT(L.outerL.overlay, RECT{100 - 66, 100 - 66, 1380 + 66, 900 + 66});
}

TEST(layout_outer_reach_follows_picture_not_window) {
  // The reach scales with the picture size (as the glow does), not with the window.
  VlcState v = Windowed();
  v.pictureRect = RECT{420, 130, 1060, 850};  // 640x720: a 8:9 picture, base = 720
  Settings s;
  s.aroundWindow = true;  // off by default
  s.blur = 0;
  s.spread = 100;  // reach = 1.0 * 720 / 2 + 2 = 362
  v.monitorRect = RECT{-1000, -1000, 5000, 5000};
  Layouts L = ComputeLayouts(v, Bars{}, s);
  CHECK(L.outer);
  CHECK_RECT(L.outerL.overlay, RECT{100 - 362, 100 - 362, 1380 + 362, 900 + 362});
  CHECK_RECT(L.outerL.scale, v.pictureRect);
  // And that picture leaves VLC's own black side areas: inner too.
  CHECK(L.inner);
  CHECK_RECT(L.innerL.hole, RECT{421, 130, 1059, 850});
  CHECK(!L.innerL.usePass);
}

TEST(layout_burned_in_bars_shrink_content) {
  VlcState v = Windowed();
  Bars bars;
  bars.vertical = 0.13f;
  bars.horizontal = 0.f;
  Settings s;
  s.aroundWindow = true;  // off by default
  Layouts L = ComputeLayouts(v, bars, s);

  // 0.13 * 720 = 93.6 -> 94 px off the top and the bottom of the picture.
  LONG d = std::lround(0.13f * 720.f);
  CHECK_EQ(d, 94L);
  CHECK_RECT(L.content, RECT{100, 130 + d, 1380, 850 - d});
  CHECK_NEAR(L.contentUV[0], 0.f, 1e-7);
  CHECK_NEAR(L.contentUV[1], 0.13f, 1e-7);
  CHECK_NEAR(L.contentUV[2], 1.f, 1e-7);
  CHECK_NEAR(L.contentUV[3], 0.87f, 1e-7);

  CHECK(L.inner);  // the burned-in bars are black area to glow in
  CHECK_RECT(L.innerL.overlay, v.videoMainRect);
  CHECK_RECT(L.innerL.frame, L.content);
  CHECK_RECT(L.innerL.hole, RECT{100, 130 + d + 1, 1380, 850 - d - 1});
  CHECK_RECT(L.innerL.scale, v.pictureRect);
  CHECK_RECT(L.innerL.pass, v.pictureRect);
  CHECK(L.innerL.usePass);  // the bar content is copied through
  CHECK(L.innerL.opaque);

  CHECK(L.outer);  // still windowed: outer glow unaffected by the bars
  CHECK_RECT(L.outerL.frame, v.mainRect);
  CHECK_RECT(L.outerL.scale, v.pictureRect);
}

TEST(layout_horizontal_bars_and_clamp) {
  VlcState v = Windowed();
  Bars bars;
  bars.horizontal = 0.1f;  // 128 px each side
  Layouts L = ComputeLayouts(v, bars, Settings{});
  CHECK_RECT(L.content, RECT{228, 130, 1252, 850});
  CHECK_NEAR(L.contentUV[0], 0.1f, 1e-7);
  CHECK_NEAR(L.contentUV[2], 0.9f, 1e-7);
  CHECK(L.inner);
  CHECK(L.innerL.usePass);

  // Bars are clamped to 0.49 per side so the content never inverts; negatives count as none.
  Bars silly;
  silly.vertical = 0.9f;
  silly.horizontal = -0.2f;
  Layouts L2 = ComputeLayouts(v, silly, Settings{});
  CHECK_NEAR(L2.contentUV[1], 0.49f, 1e-7);
  CHECK_NEAR(L2.contentUV[3], 0.51f, 1e-7);
  CHECK_EQ(L2.contentUV[0], 0.f);
  CHECK_EQ(L2.contentUV[2], 1.f);
  CHECK(RectH(L2.content) > 0);
  CHECK_RECT(L2.content, RECT{100, 130 + std::lround(0.49f * 720.f), 1380, 850 - std::lround(0.49f * 720.f)});
}

TEST(layout_one_px_slack_does_not_trigger_inner) {
  // Rounding slack of 1 px between the picture and the video area is not a black bar.
  VlcState v = Windowed();
  v.pictureRect = RECT{101, 131, 1379, 849};
  Layouts L = ComputeLayouts(v, Bars{}, Settings{});
  CHECK(!L.inner);
  v.pictureRect = RECT{102, 130, 1380, 850};  // 2 px: counts
  Layouts L2 = ComputeLayouts(v, Bars{}, Settings{});
  CHECK(L2.inner);
}

TEST(layout_settings_switch_inner_and_outer_off) {
  Settings s;
  s.aroundWindow = true;  // off by default
  s.inBars = false;
  Layouts L = ComputeLayouts(FullscreenUltrawide(), Bars{}, s);
  CHECK(!L.inner);
  CHECK(!L.outer);

  Settings s2;
  s2.aroundWindow = false;
  Layouts L2 = ComputeLayouts(Windowed(), Bars{}, s2);
  CHECK(!L2.inner);
  CHECK(!L2.outer);

  Settings s3;
  s3.aroundWindow = true;
  VlcState fs = FullscreenUltrawide();
  Layouts L3 = ComputeLayouts(fs, Bars{}, s3);
  CHECK(L3.inner);
  CHECK(!L3.outer);  // fullscreen wins over aroundWindow

  // Content and UVs are computed regardless of the switches.
  Bars bars;
  bars.vertical = 0.13f;
  Settings s4;
  s4.inBars = false;
  s4.aroundWindow = false;
  Layouts L4 = ComputeLayouts(Windowed(), bars, s4);
  CHECK(!L4.inner && !L4.outer);
  CHECK_RECT(L4.content, RECT{100, 224, 1380, 756});
  CHECK_NEAR(L4.contentUV[1], 0.13f, 1e-7);
}

TEST(layout_outer_off_when_window_outside_monitor) {
  VlcState v = Windowed();
  v.monitorRect = RECT{3000, 0, 4920, 1080};  // the window is nowhere near this monitor rect
  Settings s;
  s.aroundWindow = true;  // off by default
  s.spread = 0;
  s.blur = 0;
  Layouts L = ComputeLayouts(v, Bars{}, s);
  CHECK(!L.outer);
}

TEST(layout_inner_in_windowed_mode_with_side_areas) {
  // A 4:3 picture in a 16:9 video area: inner glow in VLC's side areas and outer glow around the window.
  VlcState v = Windowed();
  v.pictureRect = RECT{260, 130, 1220, 850};  // 960x720
  Settings s;
  s.aroundWindow = true;  // off by default
  Layouts L = ComputeLayouts(v, Bars{}, s);
  CHECK(L.inner);
  CHECK_RECT(L.innerL.overlay, v.videoMainRect);
  CHECK_RECT(L.innerL.hole, RECT{261, 130, 1219, 850});
  CHECK(!L.innerL.usePass);
  CHECK(L.outer);
  CHECK_RECT(L.outerL.frame, v.mainRect);
  CHECK_RECT(L.outerL.scale, v.pictureRect);
}

TEST(layout_hole_covers_outer_row_only_on_sides_with_glow) {
  // VLC often draws its outermost picture row darker; the overlay covers it with the edge colour, but
  // only where there is glow next to it (a side flush with the video area keeps the picture intact).
  VlcState v = Windowed();
  v.pictureRect = RECT{100, 200, 1300, 850};  // flush left and bottom, glow above and on the right
  Layouts L = ComputeLayouts(v, Bars{}, Settings{});
  CHECK(L.inner);
  CHECK_RECT(L.innerL.hole, RECT{100, 200 + kSeamCoverPx, 1300 - kSeamCoverPx, 850});
  CHECK_RECT(L.innerL.frame, v.pictureRect);  // the glow still radiates from the picture itself
  CHECK_RECT(L.innerL.pass, v.pictureRect);
  CHECK(!L.innerL.usePass);
}

TEST(capture_box_frame_matches_window) {
  VlcState v = Windowed();  // window 1280x800, picture at (0,30)-(1280,750) in window coordinates
  RECT box{};
  CHECK(CaptureBox(v, SIZE{1280, 800}, 1280, 800, box));
  CHECK_RECT(box, RECT{0, 30, 1280, 750});
}

TEST(capture_box_clamped_to_texture) {
  VlcState v = Windowed();
  RECT box{};
  CHECK(CaptureBox(v, SIZE{1280, 800}, 1024, 600, box));
  CHECK_RECT(box, RECT{0, 30, 1024, 600});
  // A texture larger than the frame does not grow the box beyond the frame.
  CHECK(CaptureBox(v, SIZE{1280, 800}, 4096, 4096, box));
  CHECK_RECT(box, RECT{0, 30, 1280, 750});
}

TEST(capture_box_picture_offset_inside_window) {
  VlcState v = Windowed();
  v.pictureRect = RECT{260, 130, 1220, 850};
  RECT box{};
  CHECK(CaptureBox(v, SIZE{1280, 800}, 1280, 800, box));
  CHECK_RECT(box, RECT{160, 30, 1120, 750});
  // A picture hanging past the window edge is clamped to the window.
  v.pictureRect = RECT{-100, 50, 1500, 950};
  CHECK(CaptureBox(v, SIZE{1280, 800}, 1280, 800, box));
  CHECK_RECT(box, RECT{0, 0, 1280, 800});
}

TEST(capture_box_mismatched_frame_size_fails) {
  VlcState v = Windowed();
  RECT box{};
  CHECK(!CaptureBox(v, SIZE{1279, 800}, 1280, 800, box));
  CHECK(!CaptureBox(v, SIZE{1280, 801}, 1280, 800, box));
  CHECK(!CaptureBox(v, SIZE{0, 0}, 1280, 800, box));
}

TEST(capture_box_too_small_picture_fails) {
  VlcState v = Windowed();
  RECT box{};
  v.pictureRect = RECT{600, 130, 605, 850};  // 5 px wide
  CHECK(!CaptureBox(v, SIZE{1280, 800}, 1280, 800, box));
  v.pictureRect = RECT{100, 400, 1380, 405};  // 5 px tall
  CHECK(!CaptureBox(v, SIZE{1280, 800}, 1280, 800, box));
  // The threshold is "more than 8 px" in both directions.
  v.pictureRect = RECT{600, 130, 608, 850};  // exactly 8 wide: rejected
  CHECK(!CaptureBox(v, SIZE{1280, 800}, 1280, 800, box));
  v.pictureRect = RECT{600, 130, 609, 850};  // 9 wide: accepted
  CHECK(CaptureBox(v, SIZE{1280, 800}, 1280, 800, box));
  CHECK_RECT(box, RECT{500, 30, 509, 750});
  // A picture entirely outside the window clamps to an empty box.
  v.pictureRect = RECT{-500, 130, -100, 850};
  CHECK(!CaptureBox(v, SIZE{1280, 800}, 1280, 800, box));
  // A tiny texture clamps the box to nothing.
  v.pictureRect = Windowed().pictureRect;
  CHECK(!CaptureBox(v, SIZE{1280, 800}, 4, 4, box));
}
