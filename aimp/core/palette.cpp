#include "palette.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <vector>

namespace glass {
namespace {

constexpr int kSide = 64;            // the cover is reduced to at most 64x64 before clustering
constexpr int kClusters = 5;
constexpr int kIterations = 12;
constexpr float kGreyChroma = 0.035f;  // below this everywhere, the cover counts as grey
constexpr float kMinHueGap = 30.f;     // degrees between the main and the second accent
constexpr float kPi = 3.14159265f;

float SrgbToLin(float v) { return v <= 0.04045f ? v / 12.92f : std::pow((v + 0.055f) / 1.055f, 2.4f); }
float LinToSrgb(float v) {
  v = std::clamp(v, 0.f, 1.f);
  return v <= 0.0031308f ? v * 12.92f : 1.055f * std::pow(v, 1 / 2.4f) - 0.055f;
}

Rgb Hex(uint32_t v) { return Rgb{((v >> 16) & 255) / 255.f, ((v >> 8) & 255) / 255.f, (v & 255) / 255.f}; }

float Dist2(Lab x, Lab y) {
  float dl = x.L - y.L, da = x.a - y.a, db = x.b - y.b;
  return dl * dl + da * da + db * db;
}

bool InGamut(Lab c) {
  float l = c.L + 0.3963377774f * c.a + 0.2158037573f * c.b;
  float m = c.L - 0.1055613458f * c.a - 0.0638541728f * c.b;
  float s = c.L - 0.0894841775f * c.a - 1.2914855480f * c.b;
  l = l * l * l, m = m * m * m, s = s * s * s;
  const float e = 1e-4f;
  float r = 4.0767416621f * l - 3.3077115913f * m + 0.2309699292f * s;
  float g = -1.2684380046f * l + 2.6097574011f * m - 0.3413193965f * s;
  float b = -0.0041960863f * l - 0.7034186147f * m + 1.7076147010f * s;
  return r >= -e && r <= 1 + e && g >= -e && g <= 1 + e && b >= -e && b <= 1 + e;
}

// Glow colours must read as light: keep lightness in a band and give weak colours some chroma, without
// turning the hue. Out-of-gamut results lose chroma (not hue) until they fit sRGB: clipping the channels
// instead would shift the hue.
Lab Vivid(Lab c) {
  c.L = std::clamp(c.L, 0.58f, 0.82f);
  float ch = Chroma(c);
  if (ch > 1e-4f) {
    float want = std::clamp(ch, 0.10f, 0.26f);
    c.a *= want / ch;
    c.b *= want / ch;
  }
  for (int i = 0; i < 40 && !InGamut(c); i++) c.a *= 0.95f, c.b *= 0.95f;
  return c;
}

struct Sample {
  Lab lab;
  float weight;
};

}  // namespace

Lab ToOklab(Rgb c) {
  float r = SrgbToLin(c.r), g = SrgbToLin(c.g), b = SrgbToLin(c.b);
  float l = 0.4122214708f * r + 0.5363325363f * g + 0.0514459929f * b;
  float m = 0.2119034982f * r + 0.6806995451f * g + 0.1073969566f * b;
  float s = 0.0883024619f * r + 0.2817188376f * g + 0.6299787005f * b;
  l = std::cbrt(l), m = std::cbrt(m), s = std::cbrt(s);
  return Lab{0.2104542553f * l + 0.7936177850f * m - 0.0040720468f * s,
             1.9779984951f * l - 2.4285922050f * m + 0.4505937099f * s,
             0.0259040371f * l + 0.7827717662f * m - 0.8086757660f * s};
}

Rgb FromOklab(Lab c) {
  float l = c.L + 0.3963377774f * c.a + 0.2158037573f * c.b;
  float m = c.L - 0.1055613458f * c.a - 0.0638541728f * c.b;
  float s = c.L - 0.0894841775f * c.a - 1.2914855480f * c.b;
  l = l * l * l, m = m * m * m, s = s * s * s;
  return Rgb{LinToSrgb(4.0767416621f * l - 3.3077115913f * m + 0.2309699292f * s),
             LinToSrgb(-1.2684380046f * l + 2.6097574011f * m - 0.3413193965f * s),
             LinToSrgb(-0.0041960863f * l - 0.7034186147f * m + 1.7076147010f * s)};
}

float Chroma(Lab c) { return std::sqrt(c.a * c.a + c.b * c.b); }

float HueDistanceDeg(Lab x, Lab y) {
  if (Chroma(x) < 1e-4f || Chroma(y) < 1e-4f) return 0;
  float d = std::fabs(std::atan2(x.b, x.a) - std::atan2(y.b, y.a)) * 180.f / kPi;
  return d > 180.f ? 360.f - d : d;
}

Palette DefaultPalette() {
  Palette p;
  p.accent[0] = Hex(0x8B7CFF);
  p.accent[1] = Hex(0xFF6B9D);
  p.accent[2] = Hex(0x00E5FF);
  p.fallback = true;
  return p;
}

Palette ExtractPalette(const uint8_t* rgba, int width, int height, int stride) {
  if (!rgba || width <= 0 || height <= 0) return DefaultPalette();

  // Box-reduce in linear light to at most kSide x kSide: every source pixel counts once.
  const int gw = std::min(width, kSide), gh = std::min(height, kSide);
  std::vector<std::array<double, 4>> acc((size_t)gw * gh, {0, 0, 0, 0});
  for (int y = 0; y < height; y++) {
    const uint8_t* row = rgba + (size_t)y * stride;
    int gy = (int)((int64_t)y * gh / height);
    for (int x = 0; x < width; x++) {
      int gx = (int)((int64_t)x * gw / width);
      auto& a = acc[(size_t)gy * gw + gx];
      a[0] += SrgbToLin(row[x * 4] / 255.f);
      a[1] += SrgbToLin(row[x * 4 + 1] / 255.f);
      a[2] += SrgbToLin(row[x * 4 + 2] / 255.f);
      a[3] += 1;
    }
  }
  std::vector<Sample> samples;
  samples.reserve(acc.size());
  float maxChroma = 0;
  for (const auto& a : acc) {
    if (a[3] <= 0) continue;
    Rgb c{LinToSrgb((float)(a[0] / a[3])), LinToSrgb((float)(a[1] / a[3])), LinToSrgb((float)(a[2] / a[3]))};
    Lab lab = ToOklab(c);
    float ch = Chroma(lab);
    maxChroma = std::max(maxChroma, ch);
    // Colourful, mid-light pixels matter most for a glow; near-black and near-white ones barely.
    float light = 1.f - std::fabs(lab.L - 0.62f) / 0.62f;
    samples.push_back({lab, (0.15f + 4.f * ch) * std::max(0.05f, light)});
  }
  if (samples.empty() || maxChroma < kGreyChroma) return DefaultPalette();

  // k-means with a deterministic farthest-point start, seeded by the heaviest sample.
  std::vector<Lab> centres;
  size_t first = 0;
  for (size_t i = 1; i < samples.size(); i++)
    if (samples[i].weight > samples[first].weight) first = i;
  centres.push_back(samples[first].lab);
  while ((int)centres.size() < std::min<int>(kClusters, (int)samples.size())) {
    size_t best = 0;
    float bestD = -1;
    for (size_t i = 0; i < samples.size(); i++) {
      float d = 1e9f;
      for (const Lab& c : centres) d = std::min(d, Dist2(samples[i].lab, c));
      d *= samples[i].weight;
      if (d > bestD) bestD = d, best = i;
    }
    centres.push_back(samples[best].lab);
  }
  const int k = (int)centres.size();
  std::vector<int> owner(samples.size(), 0);
  std::vector<float> mass(k, 0);
  for (int it = 0; it < kIterations; it++) {
    for (size_t i = 0; i < samples.size(); i++) {
      int bi = 0;
      float bd = 1e9f;
      for (int c = 0; c < k; c++) {
        float d = Dist2(samples[i].lab, centres[c]);
        if (d < bd) bd = d, bi = c;
      }
      owner[i] = bi;
    }
    std::vector<Lab> sum(k);
    std::fill(mass.begin(), mass.end(), 0.f);
    for (size_t i = 0; i < samples.size(); i++) {
      Lab& s = sum[owner[i]];
      float w = samples[i].weight;
      s.L += samples[i].lab.L * w, s.a += samples[i].lab.a * w, s.b += samples[i].lab.b * w;
      mass[owner[i]] += w;
    }
    for (int c = 0; c < k; c++)
      if (mass[c] > 0) centres[c] = Lab{sum[c].L / mass[c], sum[c].a / mass[c], sum[c].b / mass[c]};
  }

  // Rank clusters by weight times colourfulness; the main accent is the best one.
  std::vector<int> order(k);
  for (int c = 0; c < k; c++) order[c] = c;
  auto score = [&](int c) { return mass[c] * (0.05f + Chroma(centres[c])); };
  std::stable_sort(order.begin(), order.end(), [&](int x, int y) { return score(x) > score(y); });

  const Lab main = centres[order[0]];
  int second = -1;
  for (int i = 1; i < k && second < 0; i++)
    if (HueDistanceDeg(main, centres[order[i]]) >= kMinHueGap && Chroma(centres[order[i]]) > kGreyChroma)
      second = order[i];
  Lab secondLab;
  if (second >= 0) {
    secondLab = centres[second];
  } else {
    // One-colour cover: rotate the main hue by 40 degrees so the glow still has two tones.
    float h = std::atan2(main.b, main.a) + 40.f * kPi / 180.f, ch = std::max(Chroma(main), 0.1f);
    secondLab = Lab{main.L, ch * std::cos(h), ch * std::sin(h)};
  }
  // Contrast: the remaining cluster farthest from both; if there is none, the complement of the main hue.
  int third = -1;
  float thirdD = -1;
  for (int c = 0; c < k; c++) {
    if (c == order[0] || c == second || Chroma(centres[c]) < kGreyChroma) continue;
    float d = std::min(Dist2(centres[c], main), Dist2(centres[c], secondLab));
    if (d > thirdD) thirdD = d, third = c;
  }
  Lab thirdLab;
  if (third >= 0 && thirdD > 0.01f) {
    thirdLab = centres[third];
  } else {
    float h = std::atan2(main.b, main.a) + kPi, ch = std::max(Chroma(main), 0.1f);
    thirdLab = Lab{main.L, ch * std::cos(h), ch * std::sin(h)};
  }

  Palette p;
  p.accent[0] = FromOklab(Vivid(main));
  p.accent[1] = FromOklab(Vivid(secondLab));
  p.accent[2] = FromOklab(Vivid(thirdLab));
  return p;
}

Palette MixPalettes(const Palette& a, const Palette& b, float t) {
  t = std::clamp(t, 0.f, 1.f);
  Palette p;
  for (int i = 0; i < 3; i++) {
    Lab x = ToOklab(a.accent[i]), y = ToOklab(b.accent[i]);
    p.accent[i] = FromOklab(Lab{x.L + (y.L - x.L) * t, x.a + (y.a - x.a) * t, x.b + (y.b - x.b) * t});
  }
  p.fallback = t < 0.5f ? a.fallback : b.fallback;
  return p;
}

}  // namespace glass
