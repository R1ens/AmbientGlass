// Accent colours from synthetic covers.
#include <cmath>
#include <cstdint>
#include <vector>

#include "../../aimp/core/palette.h"
#include "../test_framework.h"

using namespace glass;

namespace {

struct Picture {
  int w, h;
  std::vector<uint8_t> px;
  Picture(int w_, int h_) : w(w_), h(h_), px((size_t)w_ * h_ * 4, 255) {}
  void Set(int x, int y, uint32_t rgb) {
    uint8_t* p = &px[((size_t)y * w + x) * 4];
    p[0] = (rgb >> 16) & 255, p[1] = (rgb >> 8) & 255, p[2] = rgb & 255;
  }
  void Fill(int x0, int y0, int x1, int y1, uint32_t rgb) {
    for (int y = y0; y < y1; y++)
      for (int x = x0; x < x1; x++) Set(x, y, rgb);
  }
  Palette Extract() const { return ExtractPalette(px.data(), w, h, w * 4); }
};

Lab LabOf(uint32_t rgb) {
  return ToOklab(Rgb{((rgb >> 16) & 255) / 255.f, ((rgb >> 8) & 255) / 255.f, (rgb & 255) / 255.f});
}

bool SameRgb(Rgb a, Rgb b) { return a.r == b.r && a.g == b.g && a.b == b.b; }

}  // namespace

TEST(palette_oklab_round_trip) {
  for (uint32_t c : {0x000000u, 0xFFFFFFu, 0x8B7CFFu, 0x12A0C3u, 0xFF3300u}) {
    Rgb in{((c >> 16) & 255) / 255.f, ((c >> 8) & 255) / 255.f, (c & 255) / 255.f};
    Rgb out = FromOklab(ToOklab(in));
    CHECK_NEAR(out.r, in.r, 1e-3);
    CHECK_NEAR(out.g, in.g, 1e-3);
    CHECK_NEAR(out.b, in.b, 1e-3);
  }
}

namespace {
Picture GreyCover(int lo, int hi) {
  Picture p(300, 300);
  for (int y = 0; y < 300; y++)
    for (int x = 0; x < 300; x++) {
      uint8_t v = (uint8_t)(lo + (x + y) * (hi - lo) / 598);
      p.Set(x, y, (uint32_t)v << 16 | (uint32_t)v << 8 | v);
    }
  return p;
}
}  // namespace

TEST(palette_grey_cover_gives_near_neutral_tones_from_its_brightness) {
  Palette pal = GreyCover(0, 255).Extract();
  CHECK(!pal.fallback);
  CHECK(pal.monochrome);
  for (const Rgb& c : pal.accent) CHECK(Chroma(ToOklab(c)) < 0.04f);
  // Light, middle and deep tones in that order.
  CHECK(ToOklab(pal.accent[0]).L > ToOklab(pal.accent[1]).L);
  CHECK(ToOklab(pal.accent[1]).L > ToOklab(pal.accent[2]).L);
  // A brighter cover glows brighter than a dark one.
  Palette dark = GreyCover(10, 90).Extract(), light = GreyCover(160, 250).Extract();
  CHECK(dark.monochrome && light.monochrome);
  CHECK(ToOklab(light.accent[0]).L > ToOklab(dark.accent[0]).L + 0.08f);
}

TEST(palette_empty_input_uses_default_set) {
  CHECK(ExtractPalette(nullptr, 10, 10, 40).fallback);
  uint8_t one[4] = {255, 0, 0, 255};
  CHECK(ExtractPalette(one, 0, 1, 4).fallback);
}

TEST(palette_single_colour_keeps_hue_and_adds_a_second_tone) {
  Picture p(200, 200);
  p.Fill(0, 0, 200, 200, 0xC0392B);  // brick red
  Palette pal = p.Extract();
  CHECK(!pal.fallback);
  Lab red = LabOf(0xC0392B);
  CHECK(HueDistanceDeg(ToOklab(pal.accent[0]), red) < 10.f);
  CHECK(HueDistanceDeg(ToOklab(pal.accent[1]), ToOklab(pal.accent[0])) >= 30.f);
  CHECK(HueDistanceDeg(ToOklab(pal.accent[2]), ToOklab(pal.accent[0])) > 90.f);
}

TEST(palette_two_colours_become_main_and_second) {
  Picture p(256, 256);
  p.Fill(0, 0, 256, 140, 0x1E5AA8);    // blue, larger
  p.Fill(0, 140, 256, 256, 0xF08A24);  // orange
  Palette pal = p.Extract();
  Lab blue = LabOf(0x1E5AA8), orange = LabOf(0xF08A24);
  Lab a0 = ToOklab(pal.accent[0]), a1 = ToOklab(pal.accent[1]);
  bool blueFirst = HueDistanceDeg(a0, blue) < 15.f && HueDistanceDeg(a1, orange) < 15.f;
  bool orangeFirst = HueDistanceDeg(a0, orange) < 15.f && HueDistanceDeg(a1, blue) < 15.f;
  CHECK(blueFirst || orangeFirst);
}

TEST(palette_blob_is_the_accent_not_the_dominant_colour) {
  // A mostly blue cover with an orange quarter: the blob takes the orange.
  Picture p(256, 256);
  p.Fill(0, 0, 256, 256, 0x1E5AA8);
  p.Fill(0, 192, 256, 256, 0xF08A24);
  Palette pal = p.Extract();
  CHECK(HueDistanceDeg(ToOklab(pal.blob), LabOf(0xF08A24)) < 15.f);
  // Its lightness is made fit to glow, like the accents.
  CHECK(ToOklab(pal.blob).L >= 0.57f);
}

TEST(palette_blob_ignores_specks_and_greys) {
  // Blue cover, a 1% red speck and a large grey band: no accent qualifies, the blob is the second tone.
  Picture p(200, 200);
  p.Fill(0, 0, 200, 200, 0x1E5AA8);
  p.Fill(0, 120, 200, 200, 0x808080);
  p.Fill(0, 0, 20, 20, 0xE02020);
  Palette pal = p.Extract();
  CHECK(HueDistanceDeg(ToOklab(pal.blob), LabOf(0xE02020)) > 30.f);
  // A grey cover gives a silver blob.
  Picture g(100, 100);
  g.Fill(0, 0, 100, 100, 0x909090);
  CHECK(Chroma(ToOklab(g.Extract().blob)) < 0.04f);
}

TEST(palette_small_vivid_patch_beats_large_dark_area) {
  // A mostly black cover with a bright magenta logo: the glow should be magenta, not grey.
  Picture p(400, 400);
  p.Fill(0, 0, 400, 400, 0x0A0A0C);
  p.Fill(160, 160, 240, 240, 0xE0309A);
  Palette pal = p.Extract();
  CHECK(!pal.fallback);
  CHECK(HueDistanceDeg(ToOklab(pal.accent[0]), LabOf(0xE0309A)) < 12.f);
}

TEST(palette_accents_are_light_enough_to_glow) {
  Picture p(128, 128);
  p.Fill(0, 0, 128, 64, 0x2B0F3A);   // very dark purple
  p.Fill(0, 64, 128, 128, 0x0F2B1A);  // very dark green
  Palette pal = p.Extract();
  for (const Rgb& c : pal.accent) {
    Lab l = ToOklab(c);
    CHECK(l.L >= 0.57f);
    CHECK(Chroma(l) >= 0.09f);
  }
}

TEST(palette_is_deterministic) {
  Picture p(333, 177);
  for (int y = 0; y < 177; y++)
    for (int x = 0; x < 333; x++) p.Set(x, y, (uint32_t)((x * 7) & 255) << 16 | (uint32_t)((y * 3) & 255) << 8 | 90);
  Palette a = p.Extract(), b = p.Extract();
  for (int i = 0; i < 3; i++) CHECK(SameRgb(a.accent[i], b.accent[i]));
}

TEST(palette_mix_ends_are_the_inputs) {
  Palette a = DefaultPalette(), b;
  b.accent[0] = Rgb{0.2f, 0.6f, 0.3f};
  b.accent[1] = Rgb{0.9f, 0.4f, 0.1f};
  b.accent[2] = Rgb{0.1f, 0.3f, 0.9f};
  Palette m0 = MixPalettes(a, b, 0), m1 = MixPalettes(a, b, 1), mh = MixPalettes(a, b, 0.5f);
  for (int i = 0; i < 3; i++) {
    CHECK_NEAR(m0.accent[i].r, a.accent[i].r, 1e-3);
    CHECK_NEAR(m1.accent[i].b, b.accent[i].b, 1e-3);
  }
  float lMid = ToOklab(mh.accent[0]).L, la = ToOklab(a.accent[0]).L, lb = ToOklab(b.accent[0]).L;
  CHECK_NEAR(lMid, (la + lb) / 2, 1e-3);
}
