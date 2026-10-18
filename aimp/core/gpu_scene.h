#pragma once
// The GPU part of a frame: living background, glass panels and the cover glow, drawn with Direct3D 11 into the
// window's back buffer before the text and icons go on top (ui_painter).

#include <d3d11.h>

#include <cstdint>
#include <string>

#include "d3d_helpers.h"
#include "layout.h"
#include "model.h"

namespace glass {

struct SceneOptions {
  bool background = true;   // ribbons on; off = plain dark background
  bool coverGlow = true;
  float ribbons = 1.0f;     // intensity of mass and filaments
  float rays = 1.0f;        // intensity of the rays
  float dither = 1.5f;      // amplitude in 8-bit steps
  float frost = 1.0f;       // glass: 0 = clear inside (frosted rim only), 1 = fully frosted
  float mono = 0.0f;        // 0 = colours, 1 = black background with silver light
  uint32_t seed = 0;        // dither pattern offset (fixed in tests)
};

class GpuScene {
 public:
  bool Init(ID3D11Device* device, ID3D11DeviceContext* ctx, std::wstring& error);
  // Draws into `rtv` (an 8-bit BGRA or RGBA target of w x h px); `scale` = px per layout unit (DPI / 96).
  void Render(ID3D11RenderTargetView* rtv, UINT w, UINT h, float scale, const Model& m, const Layout& L,
              const SceneOptions& o);
  // A layer above the finished main frame (after its text): the quick settings panel or a menu. `veil` darkens
  // the window around it (0 = only a soft shadow), `radius` rounds its corners; the glass is always frosted so
  // its text reads. Uses the background Render() made for this frame.
  void RenderOverlay(ID3D11RenderTargetView* rtv, UINT w, UINT h, float scale, const Model& m, const Layout& L,
                     const SceneOptions& o, const Rc& panel, float veil, float radius);

 private:
  bool Ensure(UINT w, UINT h);
  void UploadCover(const Cover* cover);

  ComPtr<ID3D11Device> m_device;
  ComPtr<ID3D11DeviceContext> m_ctx;
  PassRunner m_pass;
  ComPtr<ID3D11PixelShader> m_psRibbons, m_psDown, m_psComposite, m_psOverlay;
  ComPtr<ID3D11BlendState> m_blend;
  Tex m_rib, m_small, m_tmp, m_glass;
  ComPtr<ID3D11Texture2D> m_coverTex;
  ComPtr<ID3D11ShaderResourceView> m_coverSrv;
  uint64_t m_coverId = ~0ull;
  UINT m_w = 0, m_h = 0;
};

}  // namespace glass
