// The vertex stage of every full-screen pass (PassRunner::Pass draws one triangle covering the target).
// Pixel shaders take VSOut: uv 0..1 across the render target, origin top-left.

struct VSOut { float4 pos : SV_Position; float2 uv : TEXCOORD0; };

VSOut VSMain(uint id : SV_VertexID) {
  VSOut o;
  float2 uv = float2((id << 1) & 2, id & 2);
  o.uv = uv;
  o.pos = float4(uv * float2(2, -2) + float2(-1, 1), 0, 1);
  return o;
}
