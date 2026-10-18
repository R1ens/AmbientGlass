// AimpGlass shaders: the living background (ribbons and rays around the cover), its downsample for the glass,
// and the composite that draws background, glass panels and the cover glow in one full-screen pass.
#include "fullscreen.hlsli"

Texture2D t0 : register(t0);
Texture2D t1 : register(t1);
Texture2D t2 : register(t2);
SamplerState sLin : register(s0);

static const int kMaxPanels = 8;

cbuffer GlassCB : register(b0) {
  float4 target;   // xy = output size px, zw = 1 / size
  float4 centre;   // xy = cover centre px (output space), z = cover half side px, w = reach px (to the far corner)
  float4 audio;    // x = bass 0..1, y = highs 0..1, z = beat pulse 0..1, w = time s
  float4 col0;     // accent colours, linear; col0.w = ribbon intensity
  float4 col1;     // col1.w = ray intensity
  float4 col2;     // col2.w = cover glow strength
  float4 base;     // background colour, linear; w = panel tint (white mix)
  float4 panelSt;  // x = radius px, y = border alpha, z = shadow alpha, w = highlight alpha
  float4 info;     // x = panel count, y = dither amplitude (in 8-bit steps), z = seed, w = glass saturation
  float4 coverRc;  // cover rect px (l, t, r, b) for the glow
  float4 dim;      // x = dark veil of the overlay pass, z = 1 for the light theme, w = 1 while it looks light, y unused
  float4 fx;       // x = glass frost 0..1 (0 = clear), y = black-and-silver 0..1, z = relative loudness, w = tempo 0..1
  float4 panels[kMaxPanels];  // l, t, r, b px
};

// ---------------------------------------------------------------------------------------------------------
// Noise
// ---------------------------------------------------------------------------------------------------------

float Hash(float2 p) {
  p = frac(p * float2(123.34, 456.21));
  p += dot(p, p + 45.32);
  return frac(p.x * p.y);
}

float VNoise(float2 p) {
  float2 i = floor(p), f = frac(p);
  float2 u = f * f * f * (f * (f * 6 - 15) + 10);  // quintic: no grid creases in the ridges
  float a = Hash(i), b = Hash(i + float2(1, 0)), c = Hash(i + float2(0, 1)), d = Hash(i + float2(1, 1));
  return lerp(lerp(a, b, u.x), lerp(c, d, u.x), u.y);
}

float Fbm(float2 p) {
  float s = 0, a = 0.5;
  [unroll] for (int k = 0; k < 4; k++) {
    s += a * VNoise(p);
    p = mul(float2x2(1.6, 1.2, -1.2, 1.6), p) + 17.3;
    a *= 0.5;
  }
  return s / 0.9375;
}

// 1 on the middle value of the noise, falling to 0 at its extremes: thin bright lines where noise crosses 0.5.
float Ridge(float v) { return saturate(1 - abs(2 * v - 1)); }

float3 SrgbToLin(float3 c) { return c <= 0.04045 ? c / 12.92 : pow(max((c + 0.055) / 1.055, 0), 2.4); }
float3 LinToSrgb(float3 c) {
  c = saturate(c);
  return c <= 0.0031308 ? c * 12.92 : 1.055 * pow(c, 1 / 2.4) - 0.055;
}

float Ign(float2 p) { return frac(52.9829189 * frac(0.06711056 * p.x + 0.00583715 * p.y)); }

float3 RgbToHsv(float3 c) {
  float4 K = float4(0, -1.0 / 3, 2.0 / 3, -1);
  float4 p = c.g < c.b ? float4(c.bg, K.wz) : float4(c.gb, K.xy);
  float4 q = c.r < p.x ? float4(p.xyw, c.r) : float4(c.r, p.yzx);
  float d = q.x - min(q.w, q.y);
  return float3(abs(q.z + (q.w - q.y) / (6 * d + 1e-6)), d / (q.x + 1e-6), q.x);
}

float3 HsvToRgb(float3 c) {
  float3 p = abs(frac(c.xxx + float3(1, 2.0 / 3, 1.0 / 3)) * 6 - 3);
  return c.z * lerp(1.0.xxx, saturate(p - 1), c.y);
}

// One colour, varied: the blob's density waves are shifts of hue and saturation of the same accent.
// hsv is that accent (sRGB HSV); t in 0..1 picks the variation, hueSpan in turns, satLo..satHi scale the saturation.
float3 Vary(float3 hsv, float t, float hueSpan, float satLo, float satHi) {
  hsv.x = frac(hsv.x + (t - 0.5) * hueSpan);
  hsv.y = saturate(hsv.y * lerp(satLo, satHi, t));
  return SrgbToLin(HsvToRgb(hsv));
}

// Black and silver: light kept as a slightly cool grey of the same brightness, over black.
float3 Silver(float3 c) { return dot(c, float3(0.2126, 0.7152, 0.0722)) * float3(0.94, 0.97, 1.0); }

// ---------------------------------------------------------------------------------------------------------
// Ribbons: rendered at a reduced size, uv covers the whole output.
// ---------------------------------------------------------------------------------------------------------

float4 PSRibbons(VSOut i) : SV_Target {
  const float t = audio.w, bass = audio.x, high = audio.y, pulse = audio.z;
  float2 p = i.uv * target.xy;
  float2 d = p - centre.xy;
  float halfSide = max(centre.z, 1);
  float rn = length(d) / halfSide;  // 1 = edge of the cover (inscribed circle)
  float ang = atan2(d.y, d.x);
  float2 cs = float2(cos(ang), sin(ang));
  float reach = centre.w / halfSide;  // rn of the farthest window corner

  // The glowing mass behind the cover: an organic rim that breathes with the bass and swells on the beat.
  float n1 = Fbm(cs * 1.4 + float2(t * 0.07, -t * 0.05));
  float rim = (1.25 + 0.45 * n1 + 0.30 * bass + 0.45 * pulse) * (0.92 + 0.16 * fx.z);
  float mass = exp(-2.2 * pow(max(rn - 0.55, 0) / max(rim - 0.55, 0.05), 2));

  // Plasma filaments wrapped around the mass: ridged, domain-warped noise flowing slowly outwards.
  float2 q = d / halfSide;
  float2 warp = float2(Fbm(q * 0.7 + float2(t * 0.05, 0)), Fbm(q * 0.7 + float2(3.1, -t * 0.04)));
  float fv = Fbm(q * 1.15 + warp * 1.8 - normalize(q + 1e-4) * t * 0.08);
  float sharp = lerp(14.0, 7.0, high);
  float ripple = 1 + 0.35 * high * sin(rn * 16 - t * 3.2);
  float filReach = 1.9 + 0.5 * bass + 0.8 * pulse;  // in cover half-sides
  float fil = pow(Ridge(fv), sharp) * exp(-pow(max(rn - 0.6, 0) / filReach, 2) * 2.5) * ripple;

  // Rays: thin angular threads leaving the mass, slightly curled, each with its own length; they taper and
  // fade along their length but the longest reach the far edges of the window, further on a beat.
  float curl = 0.22 * log2(1 + rn);
  float2 rc = float2(cos(ang + curl), sin(ang + curl));
  float rv = VNoise(rc * 5.5 + float2(t * 0.03, -t * 0.02));
  float lenNoise = VNoise(rc * 4.0 + 11.0 + t * 0.01);
  float rayLen = lerp(0.35, 1.05, lenNoise) * reach * (0.85 + 0.35 * pulse + 0.15 * bass);
  float along = saturate(rn / max(rayLen, 1e-3));
  float width = lerp(22.0, 80.0, saturate(rn / reach));          // thinner further out
  float ray = pow(Ridge(rv), width);
  ray *= smoothstep(0.75, 1.35, rn) * (1 - smoothstep(0.45, 1.0, along)) * pow(1 - along, 1.5);

  // A single accent colour; the waves of density are variations of its hue and saturation.
  float3 hsv0 = RgbToHsv(LinToSrgb(col0.rgb));
  float3 cMass = Vary(hsv0, saturate(n1 * 1.2 - 0.1), 0.08, 0.80, 1.15);
  float3 cFil = Vary(hsv0, saturate(warp.x * 1.4 - 0.2), 0.12, 0.65, 1.20);
  // Rays are nearly white light, so they stand out from the blob by colour too, not only by shape.
  float3 cRay = lerp(Vary(hsv0, rv, 0.06, 0.5, 0.9), 1.0.xxx, 0.65);

  // Faster music shines a little brighter; louder moments let the rays reach a little further.
  float bright = 0.92 + 0.16 * fx.w;
  float3 o = cMass * mass * (0.40 + 0.25 * bass + 0.45 * pulse) * col0.w * bright;
  o += cFil * fil * (0.35 + 0.45 * pulse) * col0.w * bright;
  o += cRay * ray * (0.22 + 0.45 * pulse) * (0.85 + 0.30 * fx.z) * col1.w * bright;
  return float4(o, 1);
}

// A downsample of the ribbons for the glass: a box over a few mips, the blur pass smooths it further.
float4 PSDown(VSOut i) : SV_Target { return t0.SampleLevel(sLin, i.uv, 1.5); }

// ---------------------------------------------------------------------------------------------------------
// Composite: background, glass panels, cover glow, veil, dither. t0 = ribbons, t1 = blurred ribbons (glass),
// t2 = cover (with mips).
// ---------------------------------------------------------------------------------------------------------

float RoundRectSd(float2 p, float4 r, float rad) {
  float2 c = (r.xy + r.zw) * 0.5, h = (r.zw - r.xy) * 0.5;
  float2 q = abs(p - c) - h + rad;
  return length(max(q, 0)) + min(max(q.x, q.y), 0) - rad;
}

// Soft shoulder on luminance only: bright plasma never clips into flat white and keeps its colour.
float3 ToneMap(float3 c) {
  float l = dot(c, float3(0.2126, 0.7152, 0.0722));
  float m = 1 - exp(-l * 1.6);
  return l > 1e-5 ? c * (m / (l * 1.6)) : c;
}

// The ribbons over the base colour. Dark themes add their light; on the light theme added light would vanish,
// so there they tint the base towards a deep version of their own colour, more where they are brighter.
float3 LitTheme(float3 rib, float v) {
  if (dim.z < 0.5) return ToneMap(base.rgb + rib * v);
  float l = dot(rib, float3(0.2126, 0.7152, 0.0722));
  float3 hue = rib / max(l, 1e-4);           // the colour at luminance 1
  float k = saturate(1 - exp(-l * 9.0)) * v;
  return lerp(base.rgb, min(hue * 0.15, 1), k * 0.90);
}

// The theme's look, faded into black and silver by fx.y: a black background with silver light, whatever
// the theme. The light theme first turns grey itself, then, around the middle of the slider (where the
// interface switches to light text), quickly goes black, so text never sits on a mid grey.
float3 Lit(float3 rib, float v) {
  float3 c = LitTheme(rib, v);
  if (fx.y <= 0) return c;
  float3 mono = ToneMap(Silver(rib) * v * 1.15);
  if (dim.z < 0.5) return lerp(c, mono, fx.y);
  c = lerp(c, Silver(c), saturate(fx.y * 2));
  return lerp(c, mono, smoothstep(0.45, 0.55, fx.y));
}

float3 Background(float2 uv, float2 p) {
  float3 rib = t0.SampleLevel(sLin, uv, 0.7).rgb;
  // Darker towards the bottom and the corners, like light falling off in a room.
  float v = 1 - 0.55 * smoothstep(0.35, 1.0, uv.y) - 0.25 * smoothstep(0.55, 1.1, length(uv - 0.5) * 1.4);
  return Lit(rib, v);
}

float3 Saturate(float3 c, float s) {
  float l = dot(c, float3(0.2126, 0.7152, 0.0722));
  return max(lerp(l.xxx, c, s), 0);
}

// The frost of a panel at signed distance sd: fx.x inside, rising to full frost in a narrow band at the edge,
// so even clear panels keep a frosted rim that marks where they are.
float Frost(float sd) {
  float band = 0.6 * panelSt.x;  // a little over half the corner radius
  return lerp(fx.x, 1, 1 - smoothstep(0, band, -sd));
}

// One glass panel at p: the blurred background, dimmed, under a faint white tint, a highlight along the top
// and a 1 px border; clear glass shows the sharp background instead. sd = signed distance to the panel's edge.
float3 Glass(float2 p, float2 uv, float4 r, float sd) {
  float frost = Frost(sd);
  float3 glassBg = t1.SampleLevel(sLin, uv, 0).rgb;
  float v = 1 - 0.55 * smoothstep(0.35, 1.0, uv.y);
  float3 frosted = Lit(Saturate(glassBg, info.w) * 0.55, v);
  float3 clear = Background(uv, p);
  float3 g = lerp(clear, frosted, frost);
  // Tint, highlight and border mix in sRGB like the CSS they come from: a few percent of white there is a
  // faint veil, while the same mix in linear light would wash the glass out to grey. Clear glass has no tint.
  float3 gs = LinToSrgb(g);
  gs = lerp(gs, 1.0.xxx, base.w * frost);
  float hl = saturate(1 - (p.y - r.y) / (0.4 * (r.w - r.y)));
  gs += panelSt.w * hl * hl * frost;
  float ring = saturate(1 - abs(sd + 0.5));
  gs = lerp(gs, 1.0.xxx, panelSt.y * ring);
  return SrgbToLin(saturate(gs));
}

float3 Dither(float3 enc, float2 p) {
  float tri = Ign(p + info.z) + Ign(p + info.z + float2(37.1, 11.7)) - 1;
  return saturate(enc + tri * info.y / 255.0);
}

float4 PSComposite(VSOut i) : SV_Target {
  float2 p = i.uv * target.xy;
  float3 c = Background(i.uv, p);

  const float rad = panelSt.x;
  int count = (int)info.x;
  // Shadows first (they fall on the background only), then the panels.
  float shadow = 0;
  for (int k = 0; k < count; k++) {
    float sd = RoundRectSd(p - float2(0, 8), panels[k], rad);
    shadow = max(shadow, exp(-max(sd, 0) * max(sd, 0) / (2 * 14.0 * 14.0)));
  }
  c *= 1 - panelSt.z * shadow;

  for (int j = 0; j < count; j++) {
    float4 r = panels[j];
    float sd = RoundRectSd(p, r, rad);
    float inside = saturate(0.5 - sd);  // 1 px anti-aliased edge
    if (inside <= 0) continue;
    c = lerp(c, Glass(p, i.uv, r, sd), inside);
  }

  // Glow behind the cover: its own colours, blurred far past its edges, added inside the card only.
  if (col2.w > 0 && coverRc.z > coverRc.x) {
    float2 cc = (coverRc.xy + coverRc.zw) * 0.5, hs = (coverRc.zw - coverRc.xy) * 0.5;
    float2 rel = (p - cc) / hs;                          // -1..1 over the cover
    float2 cuv = saturate(rel * 0.5 + 0.5);
    float3 cov = SrgbToLin(t2.SampleLevel(sLin, cuv, 6.5).rgb);
    cov = lerp(cov, Silver(cov), fx.y);
    float dist = max(RoundRectSd(p, coverRc, 24), 0) / hs.x;
    float glow = exp(-dist * dist * 1.8) * (0.8 + 0.4 * audio.z);
    if (dim.w < 0.5) {
      c += cov * glow * col2.w * 0.32;
    } else {
      // Light theme: the glow tints towards a deep version of the cover's colours instead of adding light.
      float l = dot(cov, float3(0.2126, 0.7152, 0.0722));
      c = lerp(c, min(cov / max(l, 1e-4) * 0.15, 1), saturate(glow * col2.w * 0.45));
    }
  }

  return float4(Dither(LinToSrgb(c), p), 1);
}

// Drawn with alpha blending over the finished main frame: a dark veil with the panel's shadow, and the glass
// of panels[0] (the quick settings panel) on top.
float4 PSOverlay(VSOut i) : SV_Target {
  float2 p = i.uv * target.xy;
  float4 r = panels[0];
  float sd = RoundRectSd(p, r, panelSt.x);
  float inside = saturate(0.5 - sd);
  float sh = RoundRectSd(p - float2(0, 10), r, panelSt.x);
  float veil = saturate(dim.x + 0.45 * exp(-max(sh, 0) * max(sh, 0) / (2 * 18.0 * 18.0)));
  if (inside <= 0) return float4(0, 0, 0, veil);
  float3 g = Glass(p, i.uv, r, sd);
  // Darker than the main panels: it sits above a dimmed window and must read as the top layer.
  g *= 0.85;
  return float4(Dither(LinToSrgb(g), p), lerp(veil, 1, inside));
}
