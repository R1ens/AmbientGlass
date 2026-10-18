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

// A thin line pow(Ridge(v), sharp), never narrower than about 1.5 px on screen: a thinner line would only break
// up into flickering grain. A line widened that way gets dimmer by as much, so it keeps its brightness overall,
// like a real line covering part of a pixel.
float Line(float v, float sharp) {
  float fw = max(fwidth(v), 1e-5);
  float s = min(sharp, 0.45 / fw);
  return pow(Ridge(v), s) * (s / sharp);
}

// A drift of the noise domain that never runs away: at velocity v at first (so the start looks exactly as it
// always did), then bending slowly round a circle of radius 60, so after hours the domain is still near the
// origin, at the same scale and float precision.
float2 Drift(float t, float2 v) {
  const float R = 60;
  float s = length(v);
  float a = t * s / R;
  float2 d = v / max(s, 1e-6), n = float2(-d.y, d.x);
  return R * (d * sin(a) + n * (1 - cos(a)));
}

// A beam of light across v: the sharp line as the bright core, inside a glow about eight times wider and much
// fainter, the way a ray of light scatters in haze. Without the glow a ray reads as a painted stripe.
float Beam(float v, float sharp) { return Line(v, sharp) + 0.11 * Line(v, sharp * 0.125); }

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
  float n1 = Fbm(cs * 1.4 + Drift(t, float2(0.07, -0.05)));
  float rim = (1.25 + 0.45 * n1 + 0.30 * bass + 0.45 * pulse) * (0.92 + 0.16 * fx.z);
  float mass = exp(-2.2 * pow(max(rn - 0.55, 0) / max(rim - 0.55, 0.05), 2));

  // Plasma filaments wrapped around the mass: ridged, domain-warped noise flowing slowly outwards.
  float2 q = d / halfSide;
  float2 warp = float2(Fbm(q * 0.7 + Drift(t, float2(0.05, 0))), Fbm(q * 0.7 + float2(3.1, 0) + Drift(t, float2(0, -0.04))));
  // The filaments flow outwards. A flow that keeps adding up would pull them into ever thinner radial threads
  // (neighbouring angles drift further and further apart), so it runs in cycles: two layers, each flowing over
  // half a cycle either side of rest, cross-fading so that one is always at rest when the other jumps back.
  const float cycle = 1.2;                        // flow distance of one cycle, in cover half-sides
  float flow = t * 0.08 / cycle;
  float u1 = frac(flow + 0.5) - 0.5, u2 = frac(flow) - 0.5;  // -0.5..0.5; layer 1 is at rest at t = 0
  float w1 = 1 - 2 * abs(u1);                     // w1 + w2 = 1
  float2 out1 = normalize(q + 1e-4) * cycle;
  float2 base = q * 1.15 + warp * 1.8;
  float fv1 = Fbm(base - out1 * u1), fv2 = Fbm(base - out1 * u2 + float2(5.2, 1.3));
  float sharp = lerp(14.0, 7.0, high);
  float ripple = 1 + 0.35 * high * sin(rn * 16 - t * 3.2);
  float filReach = 1.9 + 0.5 * bass + 0.8 * pulse;  // in cover half-sides
  float fil = (w1 * Line(fv1, sharp) + (1 - w1) * Line(fv2, sharp)) * exp(-pow(max(rn - 0.6, 0) / filReach, 2) * 2.5) * ripple;

  // Rays: thin angular threads leaving the mass, slightly curled, each with its own length; they taper and
  // fade along their length but the longest reach the far edges of the window, further on a beat.
  float curl = 0.22 * log2(1 + rn);
  float2 rc = float2(cos(ang + curl), sin(ang + curl));
  float rv = VNoise(rc * 5.5 + Drift(t, float2(0.03, -0.02)));
  float lenNoise = VNoise(rc * 4.0 + 11.0 + Drift(t, float2(0.01, 0.01)));
  float rayLen = lerp(0.35, 1.05, lenNoise) * reach * (0.85 + 0.35 * pulse + 0.15 * bass);
  float along = saturate(rn / max(rayLen, 1e-3));
  float width = lerp(22.0, 80.0, saturate(rn / reach));          // thinner further out
  float ray = Beam(rv, width);
  ray *= smoothstep(0.75, 1.35, rn) * (1 - smoothstep(0.45, 1.0, along)) * pow(1 - along, 1.5);
  // Depth: whole sectors of rays are brighter or fainter (slowly changing), so some rays come forward and
  // others stay in the haze instead of all being alike.
  float sector = VNoise(rc * 2.2 + 31.0 + Drift(t, float2(0.015, -0.01)));
  ray *= lerp(0.35, 1.35, smoothstep(0.2, 0.8, sector));
  // Light shafts: a few broad, soft beams behind the thin rays, as when light falls through haze; they start
  // at the mass and fade with distance.
  float shaftN = VNoise(rc * 3.0 + 47.0 + Drift(t, float2(0.02, 0.015)));
  float shaft = shaftN * shaftN * shaftN * smoothstep(0.8, 1.6, rn) * exp(-max(rn - 1, 0) / max(reach, 1) * 2.2);

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
  o += cRay * shaft * (0.18 + 0.10 * pulse) * col1.w * bright;
  return float4(o, 1);
}

// A downsample of the ribbons for the glass (an eighth of the window): a box over a few mips, half a step short
// of the target size; the blur pass smooths it further.
float4 PSDown(VSOut i) : SV_Target {
  float rw, rh;
  t0.GetDimensions(rw, rh);
  return t0.SampleLevel(sLin, i.uv, max(log2(rw / max(target.x / 8, 1)) - 0.5, 0));
}

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

// The theme's look, faded into black and white by fx.y. Dark themes: a black background with silver light.
// The light theme: its negative, a white background with the light drawn in black, so the background stays
// light (and the dark text readable) all along the slider.
float3 Lit(float3 rib, float v) {
  float3 c = LitTheme(rib, v);
  if (fx.y <= 0) return c;
  float3 mono = ToneMap(Silver(rib) * v * 1.15);
  if (dim.z < 0.5) return lerp(c, mono, fx.y);
  float l = dot(mono, float3(0.2126, 0.7152, 0.0722));
  float3 negative = SrgbToLin(1 - LinToSrgb(l.xxx));
  return lerp(c, negative, fx.y);
}

float3 Background(float2 uv, float2 p) {
  float3 rib = t0.SampleLevel(sLin, uv, 0).rgb;
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

  // Glow around the cover: a wide halo of the cover's own colours (each point takes the colour of the nearest
  // part of the cover, blurred), so the window carries the cover's real colours next to the single-accent blob.
  if (col2.w > 0 && coverRc.z > coverRc.x) {
    float2 cc = (coverRc.xy + coverRc.zw) * 0.5, hs = (coverRc.zw - coverRc.xy) * 0.5;
    float2 rel = (p - cc) / hs;                          // -1..1 over the cover
    float2 cuv = saturate(rel * 0.5 + 0.5);
    float3 cov = SrgbToLin(t2.SampleLevel(sLin, cuv, 6.5).rgb);
    cov = lerp(cov, Silver(cov), fx.y);
    float dist = max(RoundRectSd(p, coverRc, 24), 0) / hs.x;  // in cover half-sides from its edge
    // A bright near glow and a long soft tail reaching about a cover's width away; brighter on the beat.
    float glow = (exp(-dist * dist * 1.1) + 0.35 * exp(-dist * 1.4)) * (0.8 + 0.4 * audio.z);
    if (dim.w < 0.5) {
      c += cov * glow * col2.w * 0.85;
    } else {
      // Light theme: the glow tints towards a deep version of the cover's colours instead of adding light.
      float l = dot(cov, float3(0.2126, 0.7152, 0.0722));
      c = lerp(c, min(cov / max(l, 1e-4) * 0.15, 1), saturate(glow * col2.w * 0.75));
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
