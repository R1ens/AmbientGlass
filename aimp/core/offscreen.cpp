#include "offscreen.h"

#include <wincodec.h>

#include <cstring>

#include "gpu_timer.h"
#include "layout.h"
#include "log.h"
#include "ui_painter.h"
#include "wic_image.h"

namespace glass {

Offscreen::Offscreen() = default;
Offscreen::~Offscreen() = default;

bool Offscreen::Init(bool warp, std::wstring& error) {
  const D3D_FEATURE_LEVEL levels[] = {D3D_FEATURE_LEVEL_11_1, D3D_FEATURE_LEVEL_11_0};
  HRESULT hr = D3D11CreateDevice(nullptr, warp ? D3D_DRIVER_TYPE_WARP : D3D_DRIVER_TYPE_HARDWARE, nullptr,
                                 D3D11_CREATE_DEVICE_BGRA_SUPPORT, levels, 2, D3D11_SDK_VERSION, &m_device, nullptr,
                                 &m_ctx);
  if (FAILED(hr)) {
    error = L"D3D11 device: " + FromUtf8(Log::HrText(hr));
    return false;
  }
  if (!m_scene.Init(m_device.Get(), m_ctx.Get(), error)) return false;
  m_painter = std::make_unique<UiPainter>();
  return m_painter->Init(m_device.Get(), error);
}

bool Offscreen::Render(const Model& m, UINT w, UINT h, float scale, const SceneOptions& o, bool ui,
                       std::vector<uint8_t>& bgra, const UiState* state) {
  D3D11_TEXTURE2D_DESC d{};
  d.Width = w;
  d.Height = h;
  d.MipLevels = 1;
  d.ArraySize = 1;
  d.Format = DXGI_FORMAT_B8G8R8A8_UNORM;
  d.SampleDesc.Count = 1;
  d.Usage = D3D11_USAGE_DEFAULT;
  d.BindFlags = D3D11_BIND_RENDER_TARGET | D3D11_BIND_SHADER_RESOURCE;
  ComPtr<ID3D11Texture2D> tex;
  ComPtr<ID3D11RenderTargetView> rtv;
  if (FAILED(m_device->CreateTexture2D(&d, nullptr, &tex)) ||
      FAILED(m_device->CreateRenderTargetView(tex.Get(), nullptr, &rtv)))
    return false;

  Layout L = ComputeLayout(w / scale, h / scale);
  SceneOptions so = o;
  so.frost = m.settings.frost / 100.f;
  so.mono = m.settings.mono / 100.f;
  m_scene.Render(rtv.Get(), w, h, scale, m, L, so);
  if (ui) {
    UiState s = state ? *state : UiState{};
    m_painter->Paint(tex.Get(), scale, m, L, s);
    if (m.quickSettingsOpen) {
      m_scene.RenderOverlay(rtv.Get(), w, h, scale, m, L, so, L.quick, 0.40f, kPanelRadius);
      m_painter->PaintQuick(tex.Get(), scale, m, L, s);
    }
    if (s.menu.kind != MenuKind::None) {
      m_scene.RenderOverlay(rtv.Get(), w, h, scale, m, L, so, MenuRect(L, s.menu), 0.f, 14.f);
      m_painter->PaintMenu(tex.Get(), scale, m, L, s);
    }
    m_painter->ReleaseTarget();
  }

  d.Usage = D3D11_USAGE_STAGING;
  d.BindFlags = 0;
  d.CPUAccessFlags = D3D11_CPU_ACCESS_READ;
  ComPtr<ID3D11Texture2D> staging;
  if (FAILED(m_device->CreateTexture2D(&d, nullptr, &staging))) return false;
  m_ctx->CopyResource(staging.Get(), tex.Get());
  D3D11_MAPPED_SUBRESOURCE map;
  if (FAILED(m_ctx->Map(staging.Get(), 0, D3D11_MAP_READ, 0, &map))) return false;
  bgra.resize((size_t)w * h * 4);
  for (UINT y = 0; y < h; y++)
    std::memcpy(&bgra[(size_t)y * w * 4], (const uint8_t*)map.pData + (size_t)y * map.RowPitch, (size_t)w * 4);
  m_ctx->Unmap(staging.Get(), 0);
  return true;
}

std::wstring Offscreen::Bench(const Model& model, UINT w, UINT h, float scale, int frames) {
  D3D11_TEXTURE2D_DESC d{};
  d.Width = w, d.Height = h, d.MipLevels = 1, d.ArraySize = 1;
  d.Format = DXGI_FORMAT_B8G8R8A8_UNORM;
  d.SampleDesc.Count = 1;
  d.Usage = D3D11_USAGE_DEFAULT;
  d.BindFlags = D3D11_BIND_RENDER_TARGET | D3D11_BIND_SHADER_RESOURCE;
  ComPtr<ID3D11Texture2D> tex;
  ComPtr<ID3D11RenderTargetView> rtv;
  GpuTimer timer;
  if (FAILED(m_device->CreateTexture2D(&d, nullptr, &tex)) ||
      FAILED(m_device->CreateRenderTargetView(tex.Get(), nullptr, &rtv)) || !timer.Init(m_device.Get(), m_ctx.Get()))
    return L"bench: setup failed";
  Layout L = ComputeLayout(w / scale, h / scale);
  SceneOptions o;
  o.frost = model.settings.frost / 100.f;
  o.mono = model.settings.mono / 100.f;
  UiState s;
  m_scene.SetTimer(&timer);
  std::vector<std::pair<std::string, double>> steps, sum;
  double cpu = 0;
  int counted = 0;
  LARGE_INTEGER f, t0, t1;
  QueryPerformanceFrequency(&f);
  Model m = model;
  for (int i = 0; i < frames + 10; i++) {
    m.time = model.time + i / 240.f;
    QueryPerformanceCounter(&t0);
    timer.Begin();
    timer.Mark("start");
    m_scene.Render(rtv.Get(), w, h, scale, m, L, o);
    m_painter->Paint(tex.Get(), scale, m, L, s);
    timer.Mark("interface (Direct2D)");
    QueryPerformanceCounter(&t1);
    if (!timer.End(steps) || i < 10) continue;  // the first frames warm up caches and shaders
    cpu += (double)(t1.QuadPart - t0.QuadPart) * 1000.0 / f.QuadPart;
    if (sum.empty()) sum = steps;
    else
      for (size_t k = 0; k < sum.size() && k < steps.size(); k++) sum[k].second += steps[k].second;
    counted++;
  }
  m_scene.SetTimer(nullptr);
  m_painter->ReleaseTarget();
  if (!counted) return L"bench: no reliable frames";
  wchar_t line[160];
  std::wstring out;
  double total = 0;
  swprintf(line, 160, L"%ux%u, %d frames, average per frame:\n", w, h, counted);
  out += line;
  for (auto& st : sum) {
    swprintf(line, 160, L"  %-24hs %7.3f ms\n", st.first.c_str(), st.second / counted);
    out += line;
    total += st.second / counted;
  }
  swprintf(line, 160, L"  %-24ls %7.3f ms  (%.0f%% of a 240 Hz frame)\n  CPU to issue a frame      %7.3f ms\n",
           L"GPU total", total, total / (1000.0 / 240) * 100, cpu / counted);
  out += line;
  return out;
}

bool Offscreen::SavePng(const std::wstring& path, UINT w, UINT h, const std::vector<uint8_t>& bgra, std::string& err) {
  ComPtr<IWICImagingFactory> wic;
  if (FAILED(CoCreateInstance(CLSID_WICImagingFactory, nullptr, CLSCTX_INPROC_SERVER, IID_PPV_ARGS(&wic)))) {
    err = "WIC unavailable";
    return false;
  }
  std::vector<BYTE> copy(bgra.begin(), bgra.end());
  return ::SavePng(wic.Get(), path, w, h, copy, err);
}

}  // namespace glass
