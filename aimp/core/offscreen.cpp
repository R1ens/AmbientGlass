#include "offscreen.h"

#include <wincodec.h>

#include <cstring>

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
                       std::vector<uint8_t>& bgra) {
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
  m_scene.Render(rtv.Get(), w, h, scale, m, L, o);
  if (ui) {
    UiState s;
    m_painter->Paint(tex.Get(), scale, m, L, s);
    if (m.quickSettingsOpen) {
      m_scene.RenderOverlay(rtv.Get(), w, h, scale, m, L, o);
      m_painter->PaintQuick(tex.Get(), scale, m, L, s);
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
