#include "fullscreen.hlsli"  // VSOut (shared/)

Texture2D t0 : register(t0);
Texture2D t1 : register(t1);
Texture2D t2 : register(t2);
Texture2D t3 : register(t3);
Texture2D t4 : register(t4);
SamplerState sLin : register(s0);

cbuffer CB : register(b0) {
  float4 srcRect;   // project: uv offset (xy) and scale (zw) of the content inside t0
  float4 proj;      // project: x = mip level, z = 1 / SDR white level, w = mip level of the 1x1 change summary
  float4 glowA;     // glow: frame centre (xy) and half size (zw), glow texels
  float4 glowB;     // glow: x = edge band depth, y = transition width, z = reach (all glow texels), w = fade start
  float4 glowC;     // glow: x = fade easing exponent, y = vibrance, zw = glow texture size
  float4 dirs;      // glow: top, right, bottom, left enabled (1/0)
  float4 fin;       // final: xy = output size px, z = dither amplitude, w = 1 when output must be opaque
  float4 adj;       // final: contrast, brightness, saturation, frame seed
  float4 holeRect;  // final: l, t, r, b (output px) left fully transparent: the picture itself
  float4 passRect;  // final: l, t, r, b (output px) of the picture copy in t1, for burned-in bars
  float4 passInfo;  // final: x = 1 when passRect is used, yz = output px -> glow uv, w = SDR white level
  float4 edgeInfo;  // final: x = transition width px, y = far blur sigma px, z = 1 when on, w = px per glow texel
  float4 projInfo;  // glow: x = frame px per projector texel, in glow texels
  float4 srcInfo;   // project/final: xy = picture size / allocated size of the picture texture
  float4 glowInfo;  // final: xy = glow grid origin (output px), z = full-res zone width px, w = its max sigma px
  float4 temporal;  // project: x = dt ms, y = steady smoothing ms, z = scene-change fade ms, w = 1 to reset
};

float3 LinToSrgb(float3 c) {
  float3 a = abs(c);
  float3 r = a <= 0.0031308 ? a * 12.92 : 1.055 * pow(a, 1.0 / 2.4) - 0.055;
  return sign(c) * r;
}

float3 SrgbToLin(float3 c) {
  float3 a = abs(c);
  float3 r = a <= 0.04045 ? a / 12.92 : pow((a + 0.055) / 1.055, 2.4);
  return sign(c) * r;
}

// Quintic ease, C2 at both ends: a smoothstep would leave a kink in the second derivative.
float Smoother(float e0, float e1, float x) {
  float t = saturate((x - e0) / (e1 - e0));
  return t * t * t * (t * (t * 6 - 15) + 10);
}

// The picture at projector resolution, sRGB-encoded. On an HDR desktop SDR white is above 1.0 in scRGB;
// normalise so 1.0 means SDR white.
float3 SourceColor(float2 uv01) {
  float2 uv = (srcRect.xy + uv01 * srcRect.zw) * srcInfo.xy;
  return LinToSrgb(max(t0.SampleLevel(sLin, uv, proj.x).rgb * proj.z, 0));
}

// How much the new frame differs from the glow (r) and from the previous frame (g), per texel.
// Mipped down to 1x1 this is the mean over the whole picture, read by PSProject on the GPU.
float4 PSDiff(VSOut i) : SV_Target {
  float3 x = SourceColor(i.uv);
  float3 glow = t1.SampleLevel(sLin, i.uv, 0).rgb;
  float3 last = t2.SampleLevel(sLin, i.uv, 0).rgb;
  return float4(dot(abs(x - glow), 1.0 / 3), dot(abs(x - last), 1.0 / 3), 0, 1);
}

// Temporal smoothing of the projector: two first-order stages of tau/2 each (a critically damped
// response: no jump at the start, no overshoot). tau is the steady smoothing, raised to the scene-change
// fade while the glow is far from the picture after a cut or a seek, and lengthened when cuts pile up
// (scrubbing), so the glow eases instead of flashing. Ordinary motion keeps the steady value.
// Outputs: stage 1 (with the recent-cuts count in alpha), stage 2 = the projector, and the frame itself.
static const float kCutLo = 0.05, kCutHi = 0.12;  // mean |change| in sRGB units: below = motion, above = a cut
struct ProjectOut {
  float4 stage1 : SV_Target0;
  float4 glow : SV_Target1;
  float4 last : SV_Target2;
};

ProjectOut PSProject(VSOut i) {
  float3 x = SourceColor(i.uv);
  ProjectOut o;
  if (temporal.w > 0.5) {
    o.stage1 = float4(x, 0);
    o.glow = float4(x, 1);
    o.last = float4(x, 1);
    return o;
  }
  float4 s1 = t1.SampleLevel(sLin, i.uv, 0);
  float3 s2 = t2.SampleLevel(sLin, i.uv, 0).rgb;
  float2 change = t3.SampleLevel(sLin, float2(0.5, 0.5), proj.w).rg;
  float dt = temporal.x;
  float cuts = s1.a * exp(-dt / 1000) + Smoother(kCutLo, kCutHi, change.y);
  float settling = Smoother(kCutLo, kCutHi, change.x);
  float fade = temporal.z * lerp(1, 2.5, saturate((cuts - 1) / 3));
  float tau = max(temporal.y, settling * fade);
  float a = tau > 0 ? 1 - exp(-dt / (0.5 * tau)) : 1;
  float3 n1 = lerp(s1.rgb, x, a);
  o.stage1 = float4(n1, cuts);
  o.glow = float4(lerp(s2, n1, a), 1);
  o.last = float4(x, 1);
  return o;
}

// Vibrance: saturation boosted most where it is low, in HSV.
float3 Rgb2Hsv(float3 c) {
  float4 K = float4(0.0, -1.0 / 3.0, 2.0 / 3.0, -1.0);
  float4 p = lerp(float4(c.bg, K.wz), float4(c.gb, K.xy), step(c.b, c.g));
  float4 q = lerp(float4(p.xyw, c.r), float4(c.r, p.yzx), step(p.x, c.r));
  float d = q.x - min(q.w, q.y);
  float e = 1.0e-10;
  return float3(abs(q.z + (q.w - q.y) / (6.0 * d + e)), d / (q.x + e), q.x);
}

float3 Hsv2Rgb(float3 c) {
  float4 K = float4(1.0, 2.0 / 3.0, 1.0 / 3.0, 3.0);
  float3 p = abs(frac(c.xxx + K.xyz) * 6.0 - K.www);
  return c.z * lerp(K.xxx, saturate(p - K.xxx), c.y);
}

float Vibrance(float c, float v) {
  float x = v < 0 ? 1 - c : c;
  float a = 1 + 5 * (1 - abs(v));
  float y = a * a - x * ((a * a - 1) / (a * a)) - (a - x / a) * (a - x / a);
  y = min(1, x + (y - x) * 5);
  return v >= 0 ? y : 1 - y;
}

// Darkness of the outer fade along one axis; u = 0 at the frame edge, 1 at the end of the reach.
float Shadow(float u) {
  float p = 1 - (1 - u) / (1 - glowB.w);
  return pow(saturate(p), glowC.x);
}

float4 PSGlow(VSOut i) : SV_Target {
  float2 p = i.uv * glowC.zw;
  float2 d = p - glowA.xy;
  float2 over = abs(d) - glowA.zw;          // > 0 outside the frame
  float o = max(over.x, over.y);
  // projInfo.y = 1 draws the picture's continuation for the transition: the same projection with
  // no fade and no end, which the final pass blurs progressively and hands over to the glow.
  bool continuation = projInfo.y > 0.5;
  if (o > glowB.z && !continuation) return 0;

  // The picture enlarged continuously: a point o texels outside the frame samples the copy that is
  // o texels bigger on every side, so on the axis it lies beyond, it hits the picture edge exactly.
  float2 halfK = glowA.zw + max(o, 0);
  float2 suv = 0.5 + d / (2 * halfK);

  // The edge band: colours are averaged over a band `depth` deep at the picture edge. The band grows
  // from nothing at the edge to the full "edge size" over the transition width, so the glow starts as
  // the exact edge colour and no seam appears where the band begins. Four taps across the band, each
  // from a mip a quarter of the band wide, keep the mip level low (box mips shimmer under motion), and
  // nothing is read finer than a glow texel (that would alias).
  float projTexel = max(projInfo.x, 1e-3);  // glow texels per projector texel
  float minLod = max(0, log2(1 / projTexel));
  float r = glowB.y > 0 ? saturate(o / glowB.y) : 1;
  float depth = o > 0 ? glowB.x * r : 0;
  float3 c;
  if (depth > 0) {
    bool xDominant = over.x >= over.y;
    float2 n = xDominant ? float2(sign(d.x), 0) : float2(0, sign(d.y));
    float tapLod = max(minLod, log2(max(1, 0.25 * depth / projTexel)));
    c = 0;
    [unroll] for (int k = 0; k < 4; k++) {
      float inset = (k + 0.5) * 0.25 * depth;
      c += t0.SampleLevel(sLin, suv - n * inset / (2 * halfK), tapLod).rgb;
    }
    c *= 0.25;
  } else {
    c = t0.SampleLevel(sLin, suv, minLod).rgb;
  }
  if (glowC.y != 0) {
    float3 hsv = Rgb2Hsv(max(c, 0));
    hsv.y = Vibrance(hsv.y, glowC.y);
    c = Hsv2Rgb(hsv);
  }

  float m = max(glowB.z, 1e-3);
  float sx = Shadow(over.x / m), sy = Shadow(over.y / m);
  float alpha = continuation ? 1 : (1 - sx) * (1 - sy);

  // Disabled sides are blacked out by sectors:
  // outside the frame split at 45 degrees from its corners, inside along its diagonals.
  bool vertical = o > 0 ? over.y >= over.x : abs(d.y) * glowA.z >= abs(d.x) * glowA.w;
  float enabled = vertical ? (d.y < 0 ? dirs.x : dirs.z) : (d.x > 0 ? dirs.y : dirs.w);
  if (enabled < 0.5) alpha = 0;
  return float4(c * alpha, alpha);
}

// Interleaved gradient noise: even, pattern-free, cheap.
float Ign(float2 p) {
  return frac(52.9829189 * frac(0.06711056 * p.x + 0.00583715 * p.y));
}

bool Inside(float2 p, float4 r) {
  return p.x >= r.x && p.x < r.z && p.y >= r.y && p.y < r.w;
}

float4 PSFinal(VSOut i) : SV_Target {
  float2 px = i.uv * fin.xy;
  if (Inside(px, holeRect)) return 0;
  float2 uv = (px - glowInfo.xy) * passInfo.yz;

  // Far field: the gaussian-blurred glow (t2). Next to the picture (within the transition width T) the
  // glow is the picture's continuation (t0: the same projection with no fade), blurred more and more
  // with distance (t3, t4: blurred by 1/4 and 1/2 of the far sigma) and cross-faded into the far field.
  // One quintic weight drives both, so the join is C2 at the picture and at T, the brightness changes
  // evenly across the transition, and detail dissolves by blurring rather than by fading.
  float4 g = t2.SampleLevel(sLin, uv, 0);
  float filterK = 1;
  if (edgeInfo.z > 0.5) {
    float2 q = clamp(px, holeRect.xy + 0.5, holeRect.zw - 0.5);  // centre of the nearest picture pixel
    float2 away = px - q;
    float dist = length(away);
    float T = edgeInfo.x;
    if (dist < T) {
      float w = Smoother(0, T, dist);
      float4 e = w < 0.5 ? lerp(t0.SampleLevel(sLin, uv, 0), t3.SampleLevel(sLin, uv, 0), w / 0.5)
                         : lerp(t3.SampleLevel(sLin, uv, 0), t4.SampleLevel(sLin, uv, 0), (w - 0.5) / 0.5);

      // Right at the edge even the continuation is too coarse (a texel is several px), so the first
      // pixels take the picture itself, lightly blurred, and hand over to it.
      float Tf = glowInfo.z;
      if (dist < Tf) {
        float2 side = step(holeRect.zw, px) - step(px, holeRect.xy);  // from the bounds, never rounding noise
        bool horizontal = abs(away.x) >= abs(away.y);
        float enabled = horizontal ? (side.x > 0 ? dirs.y : dirs.w) : (side.y > 0 ? dirs.z : dirs.x);
        if (enabled > 0.5) {
          float sig = min(0.5 * edgeInfo.y * w, glowInfo.w);
          float2 normal = horizontal ? float2(side.x, 0) : float2(0, side.y);
          float2 tangent = horizontal ? float2(0, 1) : float2(1, 0);
          // Next to burned-in bars the blur must stay inside the picture; elsewhere the picture texture
          // repeats its edge beyond it, so only a token inset is needed and gradients are not shifted.
          // The outermost rows are skipped either way: scaling and compression often leave them soft or noisy.
          float2 pos = q - normal * max(1.5, passInfo.x > 0.5 ? sig : 0.25 * sig);
          // Along the border the colour is never sharper than 4 px and at least as wide as the distance
          // from the picture (gaussian, sigma = sigT): every detail spreads as a cone and never
          // stretches into a streak, whatever grain or ringing the edge row carries. The gaussian is
          // 9 taps half a sigma apart, each prefiltered by a mip level as wide as the step, so the taps
          // overlap into one continuous blur: sparse sharp taps would repeat an edge that meets the
          // border as a fan of rays.
          float sigT = max(sig, max(4.0, dist));
          float stepT = 0.5 * sigT;
          float lod = log2(max(1, max(2 * sig, stepT)));
          float2 size = passRect.zw - passRect.xy;
          float3 lin = 0;
          float wsum = 0;
          [unroll] for (int k = -4; k <= 4; k++) {
            float wk = exp(-0.125 * k * k);  // exp(-x^2 / 2) at x = k / 2 sigma
            float2 suv = (pos + tangent * (k * stepT) - passRect.xy) / size * srcInfo.xy;
            lin += wk * t1.SampleLevel(sLin, suv, lod).rgb;
            wsum += wk;
          }
          lin /= wsum;
          float3 cf = LinToSrgb(max(lin / passInfo.w, 0));
          float wf = 1 - Smoother(0, Tf, dist);
          float ea = saturate(e.a);
          float3 ec = ea > 1e-4 ? e.rgb / ea : cf;
          float na = lerp(ea, 1, wf);
          e = float4(lerp(ec, cf, wf) * na, na);
        }
      }
      g = lerp(e, g, w);
      // The colour filters fade in over the transition too, so the glow meets the picture unchanged.
      filterK = w;
    }
  }

  float a = saturate(g.a);
  float3 c = a > 1e-4 ? g.rgb / a : 0;

  // Filter order: contrast, brightness, saturate (the CSS filter definitions); each step clamps.
  c = saturate((c - 0.5) * lerp(1, adj.x, filterK) + 0.5);
  c = saturate(c * lerp(1, adj.y, filterK));
  float s = lerp(1, adj.z, filterK);
  float3x3 sat = float3x3(
      0.213 + 0.787 * s, 0.715 - 0.715 * s, 0.072 - 0.072 * s,
      0.213 - 0.213 * s, 0.715 + 0.285 * s, 0.072 - 0.072 * s,
      0.213 - 0.213 * s, 0.715 - 0.715 * s, 0.072 + 0.928 * s);
  c = saturate(mul(sat, c));

  // The faded glow is blended in sRGB gamma, so fade before linearising. Dither the encoded
  // value that the display will quantise, with triangular noise, after the fade so dark tails get it too.
  float2 seed = float2(adj.w, adj.w * 0.37);
  float tri = Ign(px + seed) + Ign(px + seed + float2(37.1, 11.7)) - 1;
  float3 enc = saturate(c * a + tri * fin.z);
  float3 lin = SrgbToLin(enc) * passInfo.w;

  if (fin.w > 0.5) {
    // Over VLC's black bars: output is opaque. Inside burned-in bars keep whatever the video shows
    // there (subtitles, logos) on top of the glow; only near-black pixels show the glow.
    if (passInfo.x > 0.5 && Inside(px, passRect)) {
      float2 cuv = (px - passRect.xy) / (passRect.zw - passRect.xy) * srcInfo.xy;
      float3 cap = max(t1.SampleLevel(sLin, cuv, 0).rgb, 0);
      float3 area = max(t1.SampleLevel(sLin, cuv, 2).rgb, 0);
      float lum = dot(area, float3(0.2126, 0.7152, 0.0722)) / passInfo.w;
      lin = lerp(lin, cap, smoothstep(0.002, 0.01, lum));
    }
    return float4(lin, 1);
  }
  return float4(lin, a);
}
