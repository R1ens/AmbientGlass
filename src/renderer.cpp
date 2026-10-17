#include "renderer.h"

#include <DirectXPackedVector.h>

#include <algorithm>
#include <cmath>

#include "common.h"
#include "log.h"

// Shader bytecode, produced from src/glow.hlsl by fxc at build time.
#include "glow_ps_blur.h"
#include "glow_ps_diff.h"
#include "glow_ps_final.h"
#include "glow_ps_glow.h"
#include "glow_ps_project.h"
#include "glow_vs.h"

namespace {

constexpr float kBlurTexelsPerSigma = 5.f;   // glow resolution: ~5 px of blur per glow texel
constexpr float kMaxBlurTaps = 64.f;         // per side, in glow texels
constexpr float kTransitionTexels = 6.f;     // glow texels across the transition at least
constexpr UINT kLumaMinRows = 480;           // luma grid: the smallest mip at least this tall
constexpr float kDitherLsb = 1.5f / 255.f;   // triangular dither of up to 1.5 steps of 8 bits at 100%
constexpr UINT kAlign = 64;                  // textures with mips: every level down to 1/64 stays aligned
constexpr float kFullResZoneTexels = 2.f;    // the picture itself continues this many glow texels out

UINT AlignUp(UINT v, UINT a) { return (v + a - 1) / a * a; }

void SetRectF(float out[4], const RECT& r, const RECT& origin) {
  out[0] = (float)(r.left - origin.left);
  out[1] = (float)(r.top - origin.top);
  out[2] = (float)(r.right - origin.left);
  out[3] = (float)(r.bottom - origin.top);
}

}  // namespace

// Mirrors cbuffer CB in glow.hlsl.
struct alignas(16) Renderer::CB {
  float srcRect[4];
  float proj[4];
  float glowA[4];
  float glowB[4];
  float glowC[4];
  float dirs[4];
  float blur[4];
  float fin[4];
  float adj[4];
  float holeRect[4];
  float passRect[4];
  float passInfo[4];
  float edgeInfo[4];
  float projInfo[4];
  float srcInfo[4];
  float glowInfo[4];
  float temporal[4];
};

bool Renderer::Init(std::wstring& error, bool warp) {
  D3D_FEATURE_LEVEL levels[] = {D3D_FEATURE_LEVEL_11_1, D3D_FEATURE_LEVEL_11_0};
  HRESULT hr = D3D11CreateDevice(nullptr, warp ? D3D_DRIVER_TYPE_WARP : D3D_DRIVER_TYPE_HARDWARE, nullptr,
                                 D3D11_CREATE_DEVICE_BGRA_SUPPORT, levels, ARRAYSIZE(levels), D3D11_SDK_VERSION,
                                 &m_device, nullptr, &m_ctx);
  if (FAILED(hr)) {
    error = L"Direct3D 11 device creation failed: " + FromUtf8(Log::HrText(hr));
    Log::Error("renderer: %s", ToUtf8(error).c_str());
    return false;
  }
  ComPtr<IDXGIDevice1> dxgi;
  m_device.As(&dxgi);
  // One queued frame at most: the glow trails the video by the capture already, never by a queue too.
  if (dxgi) dxgi->SetMaximumFrameLatency(1);
  hr = DCompositionCreateDevice(dxgi.Get(), IID_PPV_ARGS(&m_dcomp));
  if (FAILED(hr)) {
    error = L"DirectComposition device creation failed: " + FromUtf8(Log::HrText(hr));
    Log::Error("renderer: %s", ToUtf8(error).c_str());
    return false;
  }

  hr = m_device->CreateVertexShader(g_VSMain, sizeof(g_VSMain), nullptr, &m_vs);
  struct {
    const BYTE* code;
    size_t size;
    ComPtr<ID3D11PixelShader>* ps;
  } pixel[] = {{g_PSProject, sizeof(g_PSProject), &m_psProject},
               {g_PSDiff, sizeof(g_PSDiff), &m_psDiff},
               {g_PSGlow, sizeof(g_PSGlow), &m_psGlow},
               {g_PSBlur, sizeof(g_PSBlur), &m_psBlur},
               {g_PSFinal, sizeof(g_PSFinal), &m_psFinal}};
  for (auto& p : pixel)
    if (SUCCEEDED(hr)) hr = m_device->CreatePixelShader(p.code, p.size, nullptr, p.ps->GetAddressOf());
  if (FAILED(hr)) {
    error = L"Shader creation failed: " + FromUtf8(Log::HrText(hr));
    Log::Error("renderer: %s", ToUtf8(error).c_str());
    return false;
  }

  D3D11_BUFFER_DESC bd{};
  bd.ByteWidth = sizeof(CB);
  bd.Usage = D3D11_USAGE_DYNAMIC;
  bd.BindFlags = D3D11_BIND_CONSTANT_BUFFER;
  bd.CPUAccessFlags = D3D11_CPU_ACCESS_WRITE;
  m_device->CreateBuffer(&bd, nullptr, &m_cb);

  D3D11_SAMPLER_DESC sd{};
  sd.Filter = D3D11_FILTER_MIN_MAG_MIP_LINEAR;
  sd.AddressU = sd.AddressV = sd.AddressW = D3D11_TEXTURE_ADDRESS_CLAMP;
  sd.MaxLOD = D3D11_FLOAT32_MAX;
  m_device->CreateSamplerState(&sd, &m_sampler);
  if (!m_cb || !m_sampler) {
    error = L"Renderer state creation failed";
    Log::Error("renderer: %s", ToUtf8(error).c_str());
    return false;
  }
  Log::Info("renderer: %s device ready", warp ? "WARP" : "hardware");
  return true;
}

bool Renderer::MakeTex(Tex& t, UINT w, UINT h, UINT mips, DXGI_FORMAT format) {
  t = Tex{};
  D3D11_TEXTURE2D_DESC d{};
  d.Width = w;
  d.Height = h;
  d.MipLevels = mips;
  d.ArraySize = 1;
  d.Format = format;
  d.SampleDesc.Count = 1;
  d.Usage = D3D11_USAGE_DEFAULT;
  d.BindFlags = D3D11_BIND_SHADER_RESOURCE | D3D11_BIND_RENDER_TARGET;
  d.MiscFlags = mips != 1 ? D3D11_RESOURCE_MISC_GENERATE_MIPS : 0;
  HRESULT hr = m_device->CreateTexture2D(&d, nullptr, &t.tex);
  if (SUCCEEDED(hr)) hr = m_device->CreateShaderResourceView(t.tex.Get(), nullptr, &t.srv);
  if (SUCCEEDED(hr)) hr = m_device->CreateRenderTargetView(t.tex.Get(), nullptr, &t.rtv);
  if (FAILED(hr)) {
    Log::Error("renderer: cannot create a %ux%u texture: %s", w, h, Log::HrText(hr).c_str());
    t = Tex{};
    return false;
  }
  t.tex->GetDesc(&d);
  t.w = w;
  t.h = h;
  t.mips = d.MipLevels;
  return true;
}

void Renderer::Pass(ID3D11RenderTargetView* rtv, UINT w, UINT h, ID3D11PixelShader* ps,
                    std::initializer_list<ID3D11ShaderResourceView*> srvList, const CB& cb) {
  Pass({rtv}, w, h, ps, srvList, cb);
}

void Renderer::Pass(std::initializer_list<ID3D11RenderTargetView*> rtvList, UINT w, UINT h, ID3D11PixelShader* ps,
                    std::initializer_list<ID3D11ShaderResourceView*> srvList, const CB& cb) {
  D3D11_MAPPED_SUBRESOURCE m;
  if (SUCCEEDED(m_ctx->Map(m_cb.Get(), 0, D3D11_MAP_WRITE_DISCARD, 0, &m))) {
    memcpy(m.pData, &cb, sizeof(CB));
    m_ctx->Unmap(m_cb.Get(), 0);
  }
  D3D11_VIEWPORT vp{0, 0, (float)w, (float)h, 0, 1};
  ID3D11RenderTargetView* rtvs[3] = {};
  UINT nr = 0;
  for (ID3D11RenderTargetView* v : rtvList)
    if (nr < 3) rtvs[nr++] = v;
  m_ctx->OMSetRenderTargets(nr, rtvs, nullptr);
  m_ctx->RSSetViewports(1, &vp);
  m_ctx->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
  m_ctx->IASetInputLayout(nullptr);
  m_ctx->VSSetShader(m_vs.Get(), nullptr, 0);
  m_ctx->PSSetShader(ps, nullptr, 0);
  m_ctx->PSSetConstantBuffers(0, 1, m_cb.GetAddressOf());
  m_ctx->PSSetSamplers(0, 1, m_sampler.GetAddressOf());
  ID3D11ShaderResourceView* srvs[5] = {};
  UINT n = 0;
  for (ID3D11ShaderResourceView* v : srvList)
    if (n < 5) srvs[n++] = v;
  m_ctx->PSSetShaderResources(0, 5, srvs);
  m_ctx->Draw(3, 0);
  ID3D11ShaderResourceView* none[5] = {};
  m_ctx->PSSetShaderResources(0, 5, none);
  ID3D11RenderTargetView* noRtv[3] = {};
  m_ctx->OMSetRenderTargets(3, noRtv, nullptr);
}

bool Renderer::Ingest(ID3D11Texture2D* captured, const D3D11_BOX& box, bool wantLuma) {
  UINT w = box.right - box.left, h = box.bottom - box.top;
  if (!w || !h) return false;
  if (m_picW != w || m_picH != h || !m_src.tex) {
    if (!MakeTex(m_src, AlignUp(w, kAlign), AlignUp(h, kAlign), 0)) return false;
    m_picW = w;
    m_picH = h;
    m_lumaStaging.Reset();
    m_lumaPending = false;
  }
  m_ctx->CopySubresourceRegion(m_src.tex.Get(), 0, 0, 0, 0, captured, 0, &box);
  // VLC often draws the outermost picture row and column darker (a fractional viewport blends them with
  // the black behind). Replace each with its neighbour so neither the mips nor the projector carry that
  // dark rim into the glow; the overlay covers the rim on screen (see layout.cpp).
  if (w >= 3 && h >= 3) {
    D3D11_BOX c1{1, 0, 0, 2, h, 1}, cw{w - 2, 0, 0, w - 1, h, 1};
    m_ctx->CopySubresourceRegion(m_src.tex.Get(), 0, 0, 0, 0, m_src.tex.Get(), 0, &c1);
    m_ctx->CopySubresourceRegion(m_src.tex.Get(), 0, w - 1, 0, 0, m_src.tex.Get(), 0, &cw);
    D3D11_BOX r1{0, 1, 0, w, 2, 1}, rh{0, h - 2, 0, w, h - 1, 1};
    m_ctx->CopySubresourceRegion(m_src.tex.Get(), 0, 0, 0, 0, m_src.tex.Get(), 0, &r1);
    m_ctx->CopySubresourceRegion(m_src.tex.Get(), 0, 0, h - 1, 0, m_src.tex.Get(), 0, &rh);
  }
  // Repeat the last column and row into the padding (doubling copies, never overlapping), like a clamp:
  // the coarse mips at the right and bottom edges then match the ones at the left and top.
  for (UINT x = w; x < m_src.w;) {
    UINT n = std::min(x - (w - 1), m_src.w - x);
    D3D11_BOX b{w - 1, 0, 0, w - 1 + n, h, 1};
    m_ctx->CopySubresourceRegion(m_src.tex.Get(), 0, x, 0, 0, m_src.tex.Get(), 0, &b);
    x += n;
  }
  for (UINT y = h; y < m_src.h;) {
    UINT n = std::min(y - (h - 1), m_src.h - y);
    D3D11_BOX b{0, h - 1, 0, m_src.w, h - 1 + n, 1};
    m_ctx->CopySubresourceRegion(m_src.tex.Get(), 0, 0, y, 0, m_src.tex.Get(), 0, &b);
    y += n;
  }
  m_ctx->GenerateMips(m_src.srv.Get());

  if (wantLuma && !m_lumaPending) {
    if (!m_lumaStaging) {
      // The smallest mip that is still at least ~480 px tall keeps bar edges within a couple of screen px.
      m_lumaLevel = 0;
      while (m_lumaLevel + 1 < m_src.mips && (h >> (m_lumaLevel + 1)) >= kLumaMinRows) m_lumaLevel++;
      // Only the picture: the padding would read as extra rows and columns.
      m_lumaW = std::max(1u, (w + (1u << m_lumaLevel) - 1) >> m_lumaLevel);
      m_lumaH = std::max(1u, (h + (1u << m_lumaLevel) - 1) >> m_lumaLevel);
      D3D11_TEXTURE2D_DESC d{};
      d.Width = m_lumaW;
      d.Height = m_lumaH;
      d.MipLevels = 1;
      d.ArraySize = 1;
      d.Format = kGpuFormat;
      d.SampleDesc.Count = 1;
      d.Usage = D3D11_USAGE_STAGING;
      d.CPUAccessFlags = D3D11_CPU_ACCESS_READ;
      HRESULT hr = m_device->CreateTexture2D(&d, nullptr, &m_lumaStaging);
      if (FAILED(hr)) Log::Error("renderer: cannot create the luma staging texture: %s", Log::HrText(hr).c_str());
    }
    if (m_lumaStaging) {
      D3D11_BOX lb{0, 0, 0, m_lumaW, m_lumaH, 1};
      m_ctx->CopySubresourceRegion(m_lumaStaging.Get(), 0, 0, 0, 0, m_src.tex.Get(), m_lumaLevel, &lb);
      m_ctx->Flush();  // nothing else may submit work when no overlay is visible
      m_lumaPending = true;
    }
  }
  return true;
}

void Renderer::DropSource() {
  m_src = Tex{};
  m_picW = m_picH = 0;
  m_proj[0] = m_proj[1] = m_stage1[0] = m_stage1[1] = m_last[0] = m_last[1] = Tex{};
  m_change = Tex{};
  m_lumaStaging.Reset();
  m_lumaPending = false;
}

void Renderer::UpdateProjector(const float content[4], float dtMs, float steadyMs, float cutMs) {
  if (!m_src.tex) return;
  float cw = std::max(1.f, m_picW * (content[2] - content[0]));
  float ch = std::max(1.f, m_picH * (content[3] - content[1]));
  // The short side of the projector is ~160 px: colours only need that much detail.
  // Rounded up to a multiple of 64 so its mips line up; the content is stretched over all of it.
  UINT pw, ph;
  if (cw >= ch) {
    ph = (UINT)kProjectorMin;
    pw = (UINT)std::clamp(std::round(kProjectorMin * cw / ch), 8.f, 1024.f);
  } else {
    pw = (UINT)kProjectorMin;
    ph = (UINT)std::clamp(std::round(kProjectorMin * ch / cw), 8.f, 1024.f);
  }
  pw = AlignUp(pw, kAlign);
  ph = AlignUp(ph, kAlign);
  bool reset = false;
  if (m_proj[0].w != pw || m_proj[0].h != ph || !m_proj[0].rtv || !m_change.rtv) {
    constexpr DXGI_FORMAT kState = DXGI_FORMAT_R32G32B32A32_FLOAT;
    bool ok = MakeTex(m_change, pw, ph, 0);
    for (int k = 0; k < 2 && ok; k++)
      ok = MakeTex(m_proj[k], pw, ph, 0, kState) && MakeTex(m_stage1[k], pw, ph, 1, kState) &&
           MakeTex(m_last[k], pw, ph, 1, kState);
    if (!ok) {
      DropSource();
      return;
    }
    reset = true;
  }

  CB cb{};
  cb.srcRect[0] = content[0];
  cb.srcRect[1] = content[1];
  cb.srcRect[2] = content[2] - content[0];
  cb.srcRect[3] = content[3] - content[1];
  cb.proj[0] = std::max(0.f, std::log2(cw / pw));
  cb.proj[2] = 1.f / m_white;
  cb.proj[3] = (float)(m_change.mips - 1);
  cb.srcInfo[0] = (float)m_picW / m_src.w;
  cb.srcInfo[1] = (float)m_picH / m_src.h;
  cb.temporal[0] = std::max(0.f, dtMs);
  cb.temporal[1] = std::max(0.f, steadyMs);
  cb.temporal[2] = std::max(0.f, cutMs);
  cb.temporal[3] = reset ? 1.f : 0.f;
  const int cur = m_projCur, next = 1 - m_projCur;
  if (!reset) {
    Pass(m_change.rtv.Get(), pw, ph, m_psDiff.Get(), {m_src.srv.Get(), m_proj[cur].srv.Get(), m_last[cur].srv.Get()},
         cb);
    m_ctx->GenerateMips(m_change.srv.Get());
  }
  Pass({m_stage1[next].rtv.Get(), m_proj[next].rtv.Get(), m_last[next].rtv.Get()}, pw, ph, m_psProject.Get(),
       {m_src.srv.Get(), m_stage1[cur].srv.Get(), m_proj[cur].srv.Get(), m_change.srv.Get()}, cb);
  m_ctx->GenerateMips(m_proj[next].srv.Get());  // the glow averages the edge band through these
  m_projCur = next;
}

void Renderer::RenderTo(ID3D11RenderTargetView* rtv, UINT ow, UINT oh, GlowTargets& t, const GlowLayout& L,
                        const Settings& s, bool hdr) {
  if (!m_proj[m_projCur].srv || !rtv || !ow || !oh) return;

  const float fw = (float)RectW(L.frame), fh = (float)RectH(L.frame);
  const float pw = (float)RectW(L.scale), ph = (float)RectH(L.scale);
  if (fw < 2 || fh < 2 || pw < 2 || ph < 2) return;
  GlowMetrics g = ComputeGlowMetrics(s, pw, ph);
  const bool transition = L.opaque && g.transitionPx > 0;

  // The glow is smooth, so it is computed at reduced resolution: ~5 px of blur per glow texel, and never
  // finer than half a projector texel. The grid is aligned to the frame's top-left corner, so the glow's
  // sampling phase does not depend on where the overlay starts.
  float ds = std::clamp(std::floor(std::max(g.sigmaPx / kBlurTexelsPerSigma, ph / (2.f * kProjectorMin))), 1.f, 128.f);
  // The transition needs several texels across it, or a large blur leaves it a texel or two wide.
  if (transition) ds = std::max(1.f, std::min(ds, std::floor(g.transitionPx / kTransitionTexels)));
  float orgX = std::fmod((float)(L.frame.left - L.overlay.left), ds);
  float orgY = std::fmod((float)(L.frame.top - L.overlay.top), ds);
  if (orgX > 0) orgX -= ds;
  if (orgY > 0) orgY -= ds;
  UINT gw = std::max(1u, (UINT)std::ceil((ow - orgX) / ds)), gh = std::max(1u, (UINT)std::ceil((oh - orgY) / ds));
  if (t.raw.w != gw || t.raw.h != gh || !t.raw.rtv || !t.tmp.rtv || !t.blur.rtv ||
      (transition && (!t.cont.rtv || !t.cont1.rtv || !t.cont2.rtv))) {
    bool ok = MakeTex(t.raw, gw, gh, 1) && MakeTex(t.tmp, gw, gh, 1) && MakeTex(t.blur, gw, gh, 1);
    if (ok && transition)
      ok = MakeTex(t.cont, gw, gh, 1) && MakeTex(t.cont1, gw, gh, 1) && MakeTex(t.cont2, gw, gh, 1);
    if (!ok) {
      t = GlowTargets{};
      return;
    }
  }

  CB cb{};
  const float fx = (L.frame.left + L.frame.right) / 2.f - L.overlay.left - orgX;
  const float fy = (L.frame.top + L.frame.bottom) / 2.f - L.overlay.top - orgY;
  cb.glowA[0] = fx / ds;
  cb.glowA[1] = fy / ds;
  cb.glowA[2] = fw / 2.f / ds;
  cb.glowA[3] = fh / 2.f / ds;
  float fadeStart = s.fadeStart / 100.f;
  if (g.marginPx > 0) fadeStart = std::max(fadeStart, -std::min(fw, fh) / 2.f / g.marginPx);
  cb.glowB[0] = g.edgePx / ds;
  cb.glowB[1] = transition ? g.transitionPx / ds : 0.f;
  cb.glowB[2] = g.marginPx / ds;
  cb.glowB[3] = std::min(fadeStart, 0.99f);
  cb.glowC[0] = FadeExponent(s.fadeCurve);
  cb.glowC[1] = VibranceStrength(s.vibrance);
  cb.glowC[2] = (float)gw;
  cb.glowC[3] = (float)gh;
  cb.dirs[0] = s.dirTop;
  cb.dirs[1] = s.dirRight;
  cb.dirs[2] = s.dirBottom;
  cb.dirs[3] = s.dirLeft;
  cb.projInfo[0] = fw / m_proj[m_projCur].w / ds;
  Pass(t.raw.rtv.Get(), gw, gh, m_psGlow.Get(), {m_proj[m_projCur].srv.Get()}, cb);

  // The far field.
  ID3D11ShaderResourceView* farGlow = t.raw.srv.Get();
  float sigma = g.sigmaPx / ds;
  if (sigma > 0.3f) {
    Blur(t.raw, t.tmp, t.blur, sigma, cb);
    farGlow = t.blur.srv.Get();
  }

  // With the transition: the picture's continuation and two blurrier rungs of it. Gaussians compose,
  // so the second rung adds only the missing blur, sqrt(next^2 - this^2).
  ID3D11ShaderResourceView* cont[3] = {farGlow, farGlow, farGlow};
  if (transition) {
    cb.projInfo[1] = 1.f;
    Pass(t.cont.rtv.Get(), gw, gh, m_psGlow.Get(), {m_proj[m_projCur].srv.Get()}, cb);
    cb.projInfo[1] = 0.f;
    cont[0] = cont[1] = cont[2] = t.cont.srv.Get();
    float s1 = sigma / 4, s2 = sigma / 2;
    if (s1 > 0.3f) {
      Blur(t.cont, t.tmp, t.cont1, s1, cb);
      Blur(t.cont1, t.tmp, t.cont2, std::sqrt(s2 * s2 - s1 * s1), cb);
      cont[1] = t.cont1.srv.Get();
      cont[2] = t.cont2.srv.Get();
    }
  }

  cb.fin[0] = (float)ow;
  cb.fin[1] = (float)oh;
  cb.fin[2] = s.debanding / 100.f * kDitherLsb;
  cb.fin[3] = L.opaque ? 1.f : 0.f;
  // HDR filters add to the regular ones.
  cb.adj[0] = std::max(0, s.contrast + (hdr ? s.hdrContrast - 100 : 0)) / 100.f;
  cb.adj[1] = std::max(0, s.brightness + (hdr ? s.hdrBrightness - 100 : 0)) / 100.f;
  cb.adj[2] = std::max(0, s.saturation + (hdr ? s.hdrSaturation - 100 : 0)) / 100.f;
  cb.adj[3] = m_seed;
  m_seed = std::fmod(m_seed + 17.31f, 1000.f);
  SetRectF(cb.holeRect, L.hole, L.overlay);
  SetRectF(cb.passRect, L.pass, L.overlay);
  cb.passInfo[0] = L.usePass && m_src.srv ? 1.f : 0.f;
  cb.passInfo[1] = 1.f / (ds * gw);  // glow texel i covers output px [org + i*ds, org + (i+1)*ds)
  cb.passInfo[2] = 1.f / (ds * gh);
  cb.passInfo[3] = m_white;
  cb.srcInfo[0] = m_src.w ? (float)m_picW / m_src.w : 1.f;
  cb.srcInfo[1] = m_src.h ? (float)m_picH / m_src.h : 1.f;
  cb.glowInfo[0] = orgX;
  cb.glowInfo[1] = orgY;
  if (transition) {
    cb.edgeInfo[0] = g.transitionPx;
    cb.edgeInfo[1] = g.sigmaPx;
    cb.edgeInfo[2] = 1.f;
    cb.edgeInfo[3] = ds;
    cb.glowInfo[2] = std::min(g.transitionPx, kFullResZoneTexels * ds);
    cb.glowInfo[3] = ds;  // the picture's own blur never exceeds what a glow texel would add
  }
  Pass(rtv, ow, oh, m_psFinal.Get(), {cont[0], m_src.srv.Get(), farGlow, cont[1], cont[2]}, cb);
}

void Renderer::Blur(const Tex& in, Tex& tmp, Tex& out, float sigma, CB& cb) {
  cb.blur[2] = sigma;
  cb.blur[3] = std::min(std::ceil(sigma * 3.f), kMaxBlurTaps);
  cb.blur[0] = 1.f / in.w;
  cb.blur[1] = 0;
  Pass(tmp.rtv.Get(), tmp.w, tmp.h, m_psBlur.Get(), {in.srv.Get()}, cb);
  cb.blur[0] = 0;
  cb.blur[1] = 1.f / in.h;
  Pass(out.rtv.Get(), out.w, out.h, m_psBlur.Get(), {tmp.srv.Get()}, cb);
}

bool Renderer::ReadLuma(LumaGrid& out) {
  if (!m_lumaPending || !m_lumaStaging) return false;
  D3D11_MAPPED_SUBRESOURCE m;
  HRESULT hr = m_ctx->Map(m_lumaStaging.Get(), 0, D3D11_MAP_READ, D3D11_MAP_FLAG_DO_NOT_WAIT, &m);
  if (hr == DXGI_ERROR_WAS_STILL_DRAWING) return false;
  m_lumaPending = false;
  if (FAILED(hr)) return false;

  using DirectX::PackedVector::XMConvertHalfToFloat;
  out.w = (int)m_lumaW;
  out.h = (int)m_lumaH;
  out.luma.resize((size_t)out.w * out.h);
  out.maxLinear = 0;
  for (int y = 0; y < out.h; y++) {
    const uint16_t* row = reinterpret_cast<const uint16_t*>(static_cast<const uint8_t*>(m.pData) + y * m.RowPitch);
    for (int x = 0; x < out.w; x++) {
      float r = std::max(0.f, XMConvertHalfToFloat(row[x * 4 + 0]) / m_white);
      float gch = std::max(0.f, XMConvertHalfToFloat(row[x * 4 + 1]) / m_white);
      float b = std::max(0.f, XMConvertHalfToFloat(row[x * 4 + 2]) / m_white);
      out.maxLinear = std::max(out.maxLinear, std::max(r, std::max(gch, b)));
      out.luma[(size_t)y * out.w + x] = 0.2126f * r + 0.7152f * gch + 0.0722f * b;
    }
  }
  m_ctx->Unmap(m_lumaStaging.Get(), 0);
  return true;
}
