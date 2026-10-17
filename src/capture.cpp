#include "capture.h"

#include <unknwn.h>
#include <dxgi.h>
#include <windows.graphics.capture.interop.h>
#include <windows.graphics.directx.direct3d11.interop.h>
#include <winrt/Windows.Foundation.h>
#include <winrt/Windows.Foundation.Metadata.h>
#include <winrt/Windows.Graphics.Capture.h>
#include <winrt/Windows.Graphics.DirectX.Direct3D11.h>
#include <winrt/Windows.Security.Authorization.AppCapabilityAccess.h>

#include "common.h"
#include "log.h"

namespace wgc = winrt::Windows::Graphics::Capture;
namespace wgdx = winrt::Windows::Graphics::DirectX;
namespace wd3d = winrt::Windows::Graphics::DirectX::Direct3D11;

static constexpr auto kFormat = wgdx::DirectXPixelFormat::R16G16B16A16Float;  // = kGpuFormat
static constexpr int kPoolFrames = 2;

struct WindowCapture::Impl {
  HANDLE event = nullptr;
  HWND hwnd = nullptr;
  wd3d::IDirect3DDevice device{nullptr};
  wgc::GraphicsCaptureItem item{nullptr};
  wgc::Direct3D11CaptureFramePool pool{nullptr};
  wgc::GraphicsCaptureSession session{nullptr};
  wgc::Direct3D11CaptureFrame frame{nullptr};
  winrt::Windows::Graphics::SizeInt32 poolSize{};
};

static std::string Describe(const winrt::hresult_error& e) {
  return Log::HrText(e.code()) + " " + ToUtf8(std::wstring(e.message()));
}

WindowCapture::WindowCapture() : m_impl(std::make_unique<Impl>()) {
  m_impl->event = CreateEventW(nullptr, FALSE, FALSE, nullptr);
}

WindowCapture::~WindowCapture() {
  Stop();
  CloseHandle(m_impl->event);
}

HWND WindowCapture::Target() const { return m_impl->hwnd; }
HANDLE WindowCapture::Event() const { return m_impl->event; }

void WindowCapture::RequestBorderless() {
  try {
    using winrt::Windows::Foundation::Metadata::ApiInformation;
    if (ApiInformation::IsTypePresent(L"Windows.Graphics.Capture.GraphicsCaptureAccess"))
      wgc::GraphicsCaptureAccess::RequestAccessAsync(wgc::GraphicsCaptureAccessKind::Borderless).get();
  } catch (const winrt::hresult_error& e) {
    Log::Warn("capture: borderless request failed: %s", Describe(e).c_str());
  } catch (...) {
  }
}

bool WindowCapture::Start(ID3D11Device* device, HWND hwnd) {
  Stop();
  Impl& s = *m_impl;
  try {
    if (!s.device) {
      winrt::com_ptr<IDXGIDevice> dxgi;
      winrt::check_hresult(device->QueryInterface(IID_PPV_ARGS(dxgi.put())));
      winrt::com_ptr<::IInspectable> inspectable;
      winrt::check_hresult(CreateDirect3D11DeviceFromDXGIDevice(dxgi.get(), inspectable.put()));
      s.device = inspectable.as<wd3d::IDirect3DDevice>();
    }
    auto interop = winrt::get_activation_factory<wgc::GraphicsCaptureItem, IGraphicsCaptureItemInterop>();
    wgc::GraphicsCaptureItem item{nullptr};
    winrt::check_hresult(
        interop->CreateForWindow(hwnd, winrt::guid_of<wgc::GraphicsCaptureItem>(), winrt::put_abi(item)));

    s.poolSize = item.Size();
    s.pool = wgc::Direct3D11CaptureFramePool::CreateFreeThreaded(s.device, kFormat, kPoolFrames, s.poolSize);
    s.session = s.pool.CreateCaptureSession(item);
    try {
      s.session.IsCursorCaptureEnabled(false);
    } catch (...) {
    }
    try {
      s.session.IsBorderRequired(false);
    } catch (...) {
    }
    HANDLE ev = s.event;
    s.pool.FrameArrived([ev](auto&&, auto&&) { SetEvent(ev); });
    s.session.StartCapture();
    s.item = item;
    s.hwnd = hwnd;
    Log::Info("capture: started for window %p (%dx%d)", (void*)hwnd, s.poolSize.Width, s.poolSize.Height);
    return true;
  } catch (const winrt::hresult_error& e) {
    Log::Error("capture: cannot start for window %p: %s", (void*)hwnd, Describe(e).c_str());
  } catch (...) {
    Log::Error("capture: cannot start for window %p: unknown exception", (void*)hwnd);
  }
  Stop();
  return false;
}

void WindowCapture::Stop() {
  Impl& s = *m_impl;
  try {
    if (s.frame) s.frame.Close();
    if (s.session) s.session.Close();
    if (s.pool) s.pool.Close();
  } catch (...) {
  }
  s.frame = nullptr;
  s.session = nullptr;
  s.pool = nullptr;
  s.item = nullptr;
  s.hwnd = nullptr;
}

bool WindowCapture::Acquire(ID3D11Texture2D** tex, SIZE* contentSize, LONGLONG* composedQpc) {
  Impl& s = *m_impl;
  if (!s.pool) return false;
  try {
    wgc::Direct3D11CaptureFrame latest{nullptr};
    for (;;) {
      auto f = s.pool.TryGetNextFrame();
      if (!f) break;
      if (latest) latest.Close();
      latest = f;
    }
    if (!latest) return false;
    if (s.frame) s.frame.Close();
    s.frame = nullptr;

    // The window was resized: this frame's surface still has the old size, later ones get the new one.
    auto size = latest.ContentSize();
    if (size.Width != s.poolSize.Width || size.Height != s.poolSize.Height) {
      latest.Close();
      s.poolSize = size;
      s.pool.Recreate(s.device, kFormat, kPoolFrames, size);
      return false;
    }
    s.frame = latest;
    auto access = latest.Surface().as<::Windows::Graphics::DirectX::Direct3D11::IDirect3DDxgiInterfaceAccess>();
    winrt::check_hresult(access->GetInterface(IID_PPV_ARGS(tex)));
    contentSize->cx = size.Width;
    contentSize->cy = size.Height;
    // SystemRelativeTime is QPC time in 100 ns units.
    static const LONGLONG freq = [] {
      LARGE_INTEGER f;
      QueryPerformanceFrequency(&f);
      return f.QuadPart;
    }();
    *composedQpc = latest.SystemRelativeTime().count() * freq / 10'000'000;
    return true;
  } catch (const winrt::hresult_error& e) {
    Log::Error("capture: frame lost: %s", Describe(e).c_str());
  } catch (...) {
    Log::Error("capture: frame lost: unknown exception");
  }
  return false;
}
