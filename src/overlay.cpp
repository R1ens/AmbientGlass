#include "overlay.h"

#include <algorithm>

#include "common.h"
#include "log.h"
static const wchar_t* kClass = L"VlcAmbientOverlay";

static LRESULT CALLBACK OverlayProc(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp) {
  switch (msg) {
    case WM_NCHITTEST:
      return HTTRANSPARENT;
    case WM_MOUSEACTIVATE:
      return MA_NOACTIVATE;
  }
  return DefWindowProcW(hwnd, msg, wp, lp);
}

bool Overlay::Create(ID3D11Device* device, IDCompositionDevice* dcomp, WindowThread& windows) {
  m_device = device;
  m_dcomp = dcomp;
  m_windows = &windows;
  m_x = m_y = 0;
  m_w = m_h = 16;
  m_visible = false;
  HWND hwnd = nullptr;
  windows.Run([&hwnd, this] {
    static bool registered = false;
    if (!registered) {
      WNDCLASSEXW wc{sizeof(wc)};
      wc.lpfnWndProc = OverlayProc;
      wc.hInstance = GetModuleHandleW(nullptr);
      wc.lpszClassName = kClass;
      wc.hCursor = LoadCursorW(nullptr, IDC_ARROW);
      RegisterClassExW(&wc);
      registered = true;
    }
    hwnd = CreateWindowExW(WS_EX_NOREDIRECTIONBITMAP | WS_EX_TOOLWINDOW | WS_EX_NOACTIVATE | WS_EX_TRANSPARENT |
                               WS_EX_LAYERED,
                           kClass, L"VLC Ambient Light overlay", WS_POPUP, 0, 0, m_w, m_h, nullptr, nullptr,
                           GetModuleHandleW(nullptr), nullptr);
    if (hwnd) SetLayeredWindowAttributes(hwnd, 0, 255, LWA_ALPHA);
  });
  m_hwnd = hwnd;
  if (!m_hwnd) {
    Log::Error("overlay: cannot create the window (%lu)", GetLastError());
    return false;
  }

  ComPtr<IDXGIDevice> dxgiDevice;
  ComPtr<IDXGIAdapter> adapter;
  ComPtr<IDXGIFactory2> factory;
  if (FAILED(device->QueryInterface(IID_PPV_ARGS(&dxgiDevice))) || FAILED(dxgiDevice->GetAdapter(&adapter)) ||
      FAILED(adapter->GetParent(IID_PPV_ARGS(&factory))))
    return false;

  DXGI_SWAP_CHAIN_DESC1 sd{};
  sd.Width = m_w;
  sd.Height = m_h;
  sd.Format = kGpuFormat;
  sd.SampleDesc.Count = 1;
  sd.BufferUsage = DXGI_USAGE_RENDER_TARGET_OUTPUT;
  sd.BufferCount = 2;
  sd.Scaling = DXGI_SCALING_STRETCH;
  sd.SwapEffect = DXGI_SWAP_EFFECT_FLIP_SEQUENTIAL;
  sd.AlphaMode = DXGI_ALPHA_MODE_PREMULTIPLIED;
  HRESULT hr = factory->CreateSwapChainForComposition(device, &sd, nullptr, &m_swap);
  if (FAILED(hr)) {
    Log::Error("overlay: swap chain creation failed: %s", Log::HrText(hr).c_str());
    return false;
  }

  ComPtr<IDXGISwapChain3> swap3;
  if (SUCCEEDED(m_swap.As(&swap3))) swap3->SetColorSpace1(DXGI_COLOR_SPACE_RGB_FULL_G10_NONE_P709);

  if (FAILED(dcomp->CreateTargetForHwnd(m_hwnd, TRUE, &m_target)) || FAILED(dcomp->CreateVisual(&m_visual)))
    return false;
  m_visual->SetContent(m_swap.Get());
  m_target->SetRoot(m_visual.Get());
  dcomp->Commit();
  return true;
}

void Overlay::Destroy() {
  m_rtv.Reset();
  m_visual.Reset();
  m_target.Reset();
  m_swap.Reset();
  if (m_dcomp) m_dcomp->Commit();
  HWND hwnd = m_hwnd;
  if (hwnd && IsWindow(hwnd)) {
    if (m_windows) m_windows->Run([hwnd] { DestroyWindow(hwnd); });
    else DestroyWindow(hwnd);
  }
  m_hwnd = nullptr;
  m_visible = false;
}

void Overlay::SetOwner(HWND owner) {
  if (!Alive() || GetWindow(m_hwnd, GW_OWNER) == owner) return;
  SetWindowLongPtrW(m_hwnd, GWLP_HWNDPARENT, reinterpret_cast<LONG_PTR>(owner));
}

void Overlay::SetRect(const RECT& r) {
  int x = r.left, y = r.top;
  int w = std::max<int>(1, r.right - r.left), h = std::max<int>(1, r.bottom - r.top);
  if (w != m_w || h != m_h) {
    m_rtv.Reset();
    HRESULT hr = m_swap->ResizeBuffers(2, w, h, kGpuFormat, 0);
    if (FAILED(hr)) Log::Error("overlay: resize to %dx%d failed: %s", w, h, Log::HrText(hr).c_str());
  }
  if (x != m_x || y != m_y || w != m_w || h != m_h)
    SetWindowPos(m_hwnd, nullptr, x, y, w, h, SWP_NOACTIVATE | SWP_NOZORDER | SWP_NOOWNERZORDER);
  m_x = x;
  m_y = y;
  m_w = w;
  m_h = h;
}

void Overlay::Show(bool show) {
  if (show == m_visible) return;
  ShowWindow(m_hwnd, show ? SW_SHOWNOACTIVATE : SW_HIDE);
  m_visible = show;
}

ID3D11RenderTargetView* Overlay::BackBuffer() {
  if (!m_rtv) {
    ComPtr<ID3D11Texture2D> back;
    if (FAILED(m_swap->GetBuffer(0, IID_PPV_ARGS(&back)))) return nullptr;
    m_device->CreateRenderTargetView(back.Get(), nullptr, &m_rtv);
  }
  return m_rtv.Get();
}

void Overlay::Present() { m_swap->Present(0, 0); }
