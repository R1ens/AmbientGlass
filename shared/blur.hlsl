// Shared shaders: the full-screen vertex shader (VSMain) and one direction of the separable gaussian
// (PSBlur), both driven by PassRunner.
#include "fullscreen.hlsli"

Texture2D t0 : register(t0);
SamplerState sLin : register(s0);

cbuffer BlurCB : register(b0) {
  float4 blur;  // xy = texel step, z = sigma, w = radius in taps
};

float4 PSBlur(VSOut i) : SV_Target {
  float inv = -0.5 / (blur.z * blur.z);
  float4 acc = t0.SampleLevel(sLin, i.uv, 0);
  float wsum = 1;
  int r = (int)blur.w;
  for (int k = 1; k <= r; k++) {
    float w = exp(k * k * inv);
    acc += w * (t0.SampleLevel(sLin, i.uv + blur.xy * k, 0) + t0.SampleLevel(sLin, i.uv - blur.xy * k, 0));
    wsum += 2 * w;
  }
  return acc / wsum;
}
