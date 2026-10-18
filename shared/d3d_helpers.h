#pragma once
#include <windows.h>
#include <d3d11.h>
#include <wrl/client.h>

#include <initializer_list>
#include <string>

#include "common.h"

using Microsoft::WRL::ComPtr;

// A render-target texture with its views. Sizes are those asked for; `mips` is what was created.
struct Tex {
  ComPtr<ID3D11Texture2D> tex;
  ComPtr<ID3D11ShaderResourceView> srv;
  ComPtr<ID3D11RenderTargetView> rtv;
  UINT w = 0, h = 0, mips = 1;
};

// A shader-readable render target; mips = 0 means the full chain (with GenerateMips allowed), 1 none.
// On failure logs, leaves `t` empty and returns false.
bool MakeTex(ID3D11Device* device, Tex& t, UINT w, UINT h, UINT mips, DXGI_FORMAT format = kGpuFormat);

// A gaussian of sigma s needs taps out to ~3s; blurring at reduced resolution keeps that count low:
// ~kBlurTexelsPerSigma px of sigma per texel stays smooth. Taps per side never exceed kMaxBlurTaps.
constexpr float kBlurTexelsPerSigma = 5.f;
constexpr float kMaxBlurTaps = 64.f;

// Full-screen passes: one triangle from the shared VSMain (shared/fullscreen.hlsli), a linear clamp sampler at
// s0, the caller's constant buffer at b0 and up to five SRVs at t0..t4. Everything is unbound afterwards, so a
// texture written by one pass can be read by the next.
class PassRunner {
 public:
  // `cbBytes` is the size of the caller's constant buffer struct (a multiple of 16).
  bool Init(ID3D11Device* device, ID3D11DeviceContext* ctx, UINT cbBytes, std::wstring& error);

  // `cb` points to cbBytes of constants, uploaded before the draw.
  void Pass(ID3D11RenderTargetView* rtv, UINT w, UINT h, ID3D11PixelShader* ps,
            std::initializer_list<ID3D11ShaderResourceView*> srvs, const void* cb);
  // Several render targets at once (SV_Target0..n), all w x h.
  void Pass(std::initializer_list<ID3D11RenderTargetView*> rtvs, UINT w, UINT h, ID3D11PixelShader* ps,
            std::initializer_list<ID3D11ShaderResourceView*> srvs, const void* cb);

  // Separable gaussian from `in` into `out` (through `tmp`, the same size); sigma in texels of these textures.
  void Blur(const Tex& in, Tex& tmp, Tex& out, float sigma);

 private:
  void Draw(std::initializer_list<ID3D11RenderTargetView*> rtvs, UINT w, UINT h, ID3D11PixelShader* ps,
            std::initializer_list<ID3D11ShaderResourceView*> srvs, ID3D11Buffer* cb, const void* data, UINT bytes);

  ComPtr<ID3D11DeviceContext> m_ctx;
  ComPtr<ID3D11VertexShader> m_vs;
  ComPtr<ID3D11PixelShader> m_psBlur;
  ComPtr<ID3D11Buffer> m_cb, m_blurCb;
  ComPtr<ID3D11SamplerState> m_sampler;
  UINT m_cbBytes = 0;
};
