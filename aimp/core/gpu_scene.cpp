#include "gpu_scene.h"

#include <algorithm>
#include <cmath>
#include <vector>

#include "log.h"

// Shader bytecode from aimp/core/glass.hlsl (fxc at build time).
#include "aimp_ps_composite.h"
#include "aimp_ps_down.h"
#include "aimp_ps_overlay.h"
#include "aimp_ps_ribbons.h"

namespace glass {
namespace {

constexpr int kMaxPanels = 8;
constexpr DXGI_FORMAT kHdr = DXGI_FORMAT_R16G16B16A16_FLOAT;

// Mirrors cbuffer GlassCB in glass.hlsl, field by field.
struct alignas(16) GlassCB {
  float target[4];
  float centre[4];
  float audio[4];
  float col0[4];
  float col1[4];
  float col2[4];
  float base[4];
  float panelSt[4];
  float info[4];
  float coverRc[4];
  float dim[4];
  float fx[4];
  float panels[kMaxPanels][4];
};
static_assert(sizeof(GlassCB) % 16 == 0, "constant buffer size");

float Lin(float c) { return c <= 0.04045f ? c / 12.92f : std::pow((c + 0.055f) / 1.055f, 2.4f); }

void SetRgb(float* dst, const Rgb& c, float w) {
  dst[0] = Lin(c.r), dst[1] = Lin(c.g), dst[2] = Lin(c.b), dst[3] = w;
}

void SetRc(float* dst, const Rc& r, float s) {
  dst[0] = r.l * s, dst[1] = r.t * s, dst[2] = r.r * s, dst[3] = r.b * s;
}

}  // namespace

bool GpuScene::Init(ID3D11Device* device, ID3D11DeviceContext* ctx, std::wstring& error) {
  m_device = device;
  m_ctx = ctx;
  if (!m_pass.Init(device, ctx, sizeof(GlassCB), error)) return false;
  HRESULT hr = device->CreatePixelShader(g_PSRibbons, sizeof(g_PSRibbons), nullptr, &m_psRibbons);
  if (SUCCEEDED(hr)) hr = device->CreatePixelShader(g_PSDown, sizeof(g_PSDown), nullptr, &m_psDown);
  if (SUCCEEDED(hr)) hr = device->CreatePixelShader(g_PSComposite, sizeof(g_PSComposite), nullptr, &m_psComposite);
  if (SUCCEEDED(hr)) hr = device->CreatePixelShader(g_PSOverlay, sizeof(g_PSOverlay), nullptr, &m_psOverlay);
  if (SUCCEEDED(hr)) {
    D3D11_BLEND_DESC bd{};
    bd.RenderTarget[0].BlendEnable = TRUE;
    bd.RenderTarget[0].SrcBlend = D3D11_BLEND_SRC_ALPHA;
    bd.RenderTarget[0].DestBlend = D3D11_BLEND_INV_SRC_ALPHA;
    bd.RenderTarget[0].BlendOp = D3D11_BLEND_OP_ADD;
    bd.RenderTarget[0].SrcBlendAlpha = D3D11_BLEND_ONE;
    bd.RenderTarget[0].DestBlendAlpha = D3D11_BLEND_ZERO;
    bd.RenderTarget[0].BlendOpAlpha = D3D11_BLEND_OP_ADD;
    bd.RenderTarget[0].RenderTargetWriteMask = D3D11_COLOR_WRITE_ENABLE_ALL;
    hr = device->CreateBlendState(&bd, &m_blend);
  }
  if (FAILED(hr)) {
    error = L"Glass shaders: " + FromUtf8(Log::HrText(hr));
    return false;
  }
  return true;
}

bool GpuScene::Ensure(UINT w, UINT h) {
  if (w == m_w && h == m_h && m_rib.tex) return true;
  m_w = m_h = 0;
  // Ribbons at half size (they are soft anyway), the glass background at an eighth.
  UINT rw = std::max(1u, (w + 1) / 2), rh = std::max(1u, (h + 1) / 2);
  UINT sw = std::max(1u, (w + 7) / 8), sh = std::max(1u, (h + 7) / 8);
  if (!MakeTex(m_device.Get(), m_rib, rw, rh, 0, kHdr) || !MakeTex(m_device.Get(), m_small, sw, sh, 1, kHdr) ||
      !MakeTex(m_device.Get(), m_tmp, sw, sh, 1, kHdr) || !MakeTex(m_device.Get(), m_glass, sw, sh, 1, kHdr))
    return false;
  m_w = w, m_h = h;
  return true;
}

void GpuScene::UploadCover(const Cover* cover) {
  uint64_t id = cover && !cover->Empty() ? cover->id : 0;
  if (id == m_coverId && m_coverSrv) return;
  m_coverId = id;
  m_coverTex.Reset();
  m_coverSrv.Reset();
  // A missing cover is a 1x1 mid-grey: the glow then simply follows the palette-coloured ribbons.
  const uint8_t grey[4] = {60, 60, 70, 255};
  const uint8_t* px = id ? cover->rgba.data() : grey;
  UINT w = id ? (UINT)cover->w : 1, h = id ? (UINT)cover->h : 1;
  D3D11_TEXTURE2D_DESC d{};
  d.Width = w;
  d.Height = h;
  d.MipLevels = 0;
  d.ArraySize = 1;
  d.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
  d.SampleDesc.Count = 1;
  d.Usage = D3D11_USAGE_DEFAULT;
  d.BindFlags = D3D11_BIND_SHADER_RESOURCE | D3D11_BIND_RENDER_TARGET;
  d.MiscFlags = D3D11_RESOURCE_MISC_GENERATE_MIPS;
  if (FAILED(m_device->CreateTexture2D(&d, nullptr, &m_coverTex)) ||
      FAILED(m_device->CreateShaderResourceView(m_coverTex.Get(), nullptr, &m_coverSrv))) {
    Log::Warn("glass: cover texture %ux%u not created", w, h);
    m_coverTex.Reset();
    m_coverSrv.Reset();
    return;
  }
  m_ctx->UpdateSubresource(m_coverTex.Get(), 0, nullptr, px, w * 4, 0);
  m_ctx->GenerateMips(m_coverSrv.Get());
}

namespace {

// Everything but the panel list and the veil.
void FillCommon(GlassCB& cb, UINT w, UINT h, float scale, const Model& m, const Layout& L, const SceneOptions& o) {
  cb.target[0] = (float)w, cb.target[1] = (float)h, cb.target[2] = 1.f / w, cb.target[3] = 1.f / h;
  const float cx = L.cover.Cx() * scale, cy = L.cover.Cy() * scale;
  float reach = 0;
  for (float x : {0.f, (float)w})
    for (float y : {0.f, (float)h}) reach = std::max(reach, std::hypot(x - cx, y - cy));
  cb.centre[0] = cx, cb.centre[1] = cy, cb.centre[2] = L.cover.W() * 0.5f * scale, cb.centre[3] = reach;
  cb.audio[0] = m.bass, cb.audio[1] = m.high, cb.audio[2] = m.pulse, cb.audio[3] = m.time;
  const float ribbons = o.background ? o.ribbons : 0.f, rays = o.background ? o.rays : 0.f;
  SetRgb(cb.col0, m.palette.blob, ribbons);
  SetRgb(cb.col1, m.palette.accent[1], rays);
  SetRgb(cb.col2, m.palette.accent[2], o.coverGlow ? 1.f : 0.f);

  const bool light = m.theme == Theme::Light, amoled = m.theme == Theme::Amoled;
  const bool lightLook = LightLook(m);  // the glass follows what the background looks like
  const Rgb base = amoled ? Rgb{0, 0, 0} : light ? Rgb{0.86f, 0.86f, 0.89f} : Rgb{10 / 255.f, 10 / 255.f, 14 / 255.f};
  SetRgb(cb.base, base, lightLook ? 0.30f : 0.08f);
  cb.panelSt[0] = kPanelRadius * scale;
  cb.panelSt[1] = lightLook ? 0.35f : 0.14f;
  cb.panelSt[2] = lightLook ? 0.12f : 0.35f;
  cb.panelSt[3] = 0.05f;
  cb.info[1] = o.dither;
  cb.info[2] = (float)(o.seed % 997);
  cb.info[3] = 1.6f;
  SetRc(cb.coverRc, L.cover, scale);
  cb.dim[2] = light ? 1.f : 0.f;
  cb.dim[3] = lightLook ? 1.f : 0.f;
  cb.fx[0] = std::clamp(o.frost, 0.f, 1.f);
  cb.fx[1] = std::clamp(o.mono, 0.f, 1.f);
  cb.fx[2] = std::clamp(m.loud, 0.f, 1.f);
  cb.fx[3] = m.bpm > 0 ? std::clamp((m.bpm - 80.f) / 80.f, 0.f, 1.f) : 0.3f;
}

}  // namespace

void GpuScene::Render(ID3D11RenderTargetView* rtv, UINT w, UINT h, float scale, const Model& m, const Layout& L,
                      const SceneOptions& o) {
  if (!Ensure(w, h)) return;
  UploadCover(m.cover.get());

  GlassCB cb{};
  FillCommon(cb, w, h, scale, m, L, o);
  int n = 0;
  for (const Rc* r : {&L.sidebar, &L.card, &L.list, &L.bottom})
    if (r->Valid() && n < kMaxPanels) SetRc(cb.panels[n++], *r, scale);
  cb.info[0] = (float)n;

  m_pass.Pass(m_rib.rtv.Get(), m_rib.w, m_rib.h, m_psRibbons.Get(), {}, &cb);
  m_ctx->GenerateMips(m_rib.srv.Get());
  m_pass.Pass(m_small.rtv.Get(), m_small.w, m_small.h, m_psDown.Get(), {m_rib.srv.Get()}, &cb);
  // Glass blur: 28 layout px in the eighth-size texture.
  m_pass.Blur(m_small, m_tmp, m_glass, std::max(1.f, 28.f * scale / 8.f));
  m_pass.Pass(rtv, w, h, m_psComposite.Get(), {m_rib.srv.Get(), m_glass.srv.Get(), m_coverSrv.Get()}, &cb);
}

void GpuScene::RenderOverlay(ID3D11RenderTargetView* rtv, UINT w, UINT h, float scale, const Model& m,
                             const Layout& L, const SceneOptions& o) {
  if (!m_rib.tex || w != m_w || h != m_h) return;
  GlassCB cb{};
  FillCommon(cb, w, h, scale, m, L, o);
  SetRc(cb.panels[0], L.quick, scale);
  cb.info[0] = 1;
  cb.dim[0] = 0.40f;
  const float factor[4] = {0, 0, 0, 0};
  m_ctx->OMSetBlendState(m_blend.Get(), factor, 0xFFFFFFFF);
  m_pass.Pass(rtv, w, h, m_psOverlay.Get(), {m_rib.srv.Get(), m_glass.srv.Get(), m_coverSrv.Get()}, &cb);
  m_ctx->OMSetBlendState(nullptr, factor, 0xFFFFFFFF);
}

}  // namespace glass
