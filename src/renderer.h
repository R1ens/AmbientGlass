#pragma once
#include <windows.h>
#include <d3d11.h>
#include <dcomp.h>
#include <wrl/client.h>

#include <initializer_list>
#include <string>

#include "glow_math.h"
#include "layout.h"
#include "luma_grid.h"
#include "settings.h"

using Microsoft::WRL::ComPtr;

struct Tex {
  ComPtr<ID3D11Texture2D> tex;
  ComPtr<ID3D11ShaderResourceView> srv;
  ComPtr<ID3D11RenderTargetView> rtv;
  UINT w = 0, h = 0, mips = 1;
};

// Per-output scratch textures at glow resolution; the renderer sizes them as needed.
struct GlowTargets {
  Tex raw;   // the unblurred glow
  Tex tmp;   // first blur direction
  Tex blur;  // the far field: the glow blurred by its sigma
  // Only with the transition on: the picture's continuation (the projection with no fade) blurred by
  // 0, 1/4 and 1/2 of the far sigma.
  Tex cont, cont1, cont2;
};

// The GPU side: holds the captured picture, builds the projector and renders the glow.
class Renderer {
 public:
  // `warp` picks the software rasteriser: deterministic output for headless rendering and tests.
  bool Init(std::wstring& error, bool warp = false);

  ID3D11Device* Device() const { return m_device.Get(); }
  ID3D11DeviceContext* Context() const { return m_ctx.Get(); }
  IDCompositionDevice* DComp() const { return m_dcomp.Get(); }

  // SDR white in scRGB units of the monitor VLC is on (1.0 on an SDR desktop, ~2.5 with HDR on).
  void SetWhiteLevel(float white) { m_white = white > 0.1f ? white : 1.f; }
  // The dither pattern moves every frame; fix it for reproducible output.
  void SetDitherSeed(float seed) { m_seed = seed; }
  // False once the GPU device is lost; the app then restarts itself.
  bool DeviceOk() const { return m_device && m_device->GetDeviceRemovedReason() == S_OK; }

  // Copies `box` of a captured frame as the new source picture.
  bool Ingest(ID3D11Texture2D* captured, const D3D11_BOX& box, bool wantLuma);
  void DropSource();

  // Steps the small projector image toward the content part of the source.
  // content = l, t, r, b in 0..1 of the picture; dtMs = time since the last step; steadyMs = smoothing of
  // ordinary changes (0 = none); cutMs = how long a scene change or a seek fades (0 = instant).
  // The first step after the source or its size changes starts from the frame itself.
  void UpdateProjector(const float content[4], float dtMs, float steadyMs, float cutMs);

  // Renders one overlay's picture into `rtv` of size w x h (the size of layout.overlay).
  void RenderTo(ID3D11RenderTargetView* rtv, UINT w, UINT h, GlowTargets& targets, const GlowLayout& layout,
                const Settings& s, bool hdr);

  // Returns true when a new grid was read back (non-blocking).
  bool ReadLuma(LumaGrid& out);

 private:
  struct CB;
  bool MakeTex(Tex& t, UINT w, UINT h, UINT mips, DXGI_FORMAT format = DXGI_FORMAT_R16G16B16A16_FLOAT);
  void Pass(ID3D11RenderTargetView* rtv, UINT w, UINT h, ID3D11PixelShader* ps,
            std::initializer_list<ID3D11ShaderResourceView*> srvs, const CB& cb);
  void Pass(std::initializer_list<ID3D11RenderTargetView*> rtvs, UINT w, UINT h, ID3D11PixelShader* ps,
            std::initializer_list<ID3D11ShaderResourceView*> srvs, const CB& cb);
  // Separable gaussian from `in` into `out` (through `tmp`); sigma in texels of these textures.
  void Blur(const Tex& in, Tex& tmp, Tex& out, float sigma, CB& cb);

  ComPtr<ID3D11Device> m_device;
  ComPtr<ID3D11DeviceContext> m_ctx;
  ComPtr<IDCompositionDevice> m_dcomp;
  ComPtr<ID3D11VertexShader> m_vs;
  ComPtr<ID3D11PixelShader> m_psProject, m_psDiff, m_psGlow, m_psBlur, m_psFinal;
  ComPtr<ID3D11Buffer> m_cb;
  ComPtr<ID3D11SamplerState> m_sampler;

  // Picture copy, FP16 linear, full mip chain. Allocated in multiples of 64 with the edges repeated into
  // the padding, so every mip level down to 1/64 lines up with the picture (odd sizes would shift each
  // level by a fraction of a texel and make the glow differ between the left and the right side).
  Tex m_src;
  UINT m_picW = 0, m_picH = 0;  // the picture inside m_src
  // Projector state, sRGB-encoded, FP32 so slow smoothing reaches its target (FP16 stalls a few levels
  // short), ping-ponged; sizes are multiples of 64. m_proj is what the glow reads (mipmapped), m_stage1
  // the first smoothing stage (recent cuts in alpha), m_last the previous frame for cut detection.
  Tex m_proj[2], m_stage1[2], m_last[2];
  Tex m_change;  // per-texel change, mipped to a 1x1 mean
  int m_projCur = 0;

  ComPtr<ID3D11Texture2D> m_lumaStaging;
  UINT m_lumaLevel = 0, m_lumaW = 0, m_lumaH = 0;
  bool m_lumaPending = false;
  float m_seed = 0;
  float m_white = 1.f;
};
