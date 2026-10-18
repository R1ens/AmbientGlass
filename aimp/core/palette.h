#pragma once
// Accent colours from album art: the background ribbons, the glow behind the cover and the played part of
// the waveform take their colours from here.

#include <cstdint>

namespace glass {

struct Rgb {
  float r = 0, g = 0, b = 0;  // sRGB, 0..1
};

struct Palette {
  Rgb accent[3];          // main, second (a different hue when the cover has one), contrast
  bool fallback = false;  // the cover is (nearly) grey: a neutral built-in set was used
};

// The built-in set for grey or missing covers: violet, pink, cyan.
Palette DefaultPalette();

// Pixels are 8-bit sRGB, 4 bytes each in R, G, B, A order (alpha ignored), rows `stride` bytes apart.
// Deterministic: the same picture always gives the same palette.
Palette ExtractPalette(const uint8_t* rgba, int width, int height, int stride);

// Blend from `a` to `b` in Oklab, t = 0..1 (used for the 600 ms change between tracks).
Palette MixPalettes(const Palette& a, const Palette& b, float t);

// Helpers, exposed for tests.
struct Lab {
  float L = 0, a = 0, b = 0;
};
Lab ToOklab(Rgb c);
Rgb FromOklab(Lab c);
float Chroma(Lab c);
float HueDistanceDeg(Lab x, Lab y);  // 0..180; 0 when either is grey

}  // namespace glass
