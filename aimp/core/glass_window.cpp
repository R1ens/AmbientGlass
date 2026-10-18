#include "glass_window.h"

#include <d3d11.h>
#include <dwmapi.h>
#include <dxgi1_3.h>
#include <shellapi.h>
#include <shobjidl.h>
#include <windowsx.h>

#include <algorithm>
#include <cmath>

#include "gpu_scene.h"
#include "layout.h"
#include "log.h"
#include "ui_painter.h"
#include "ui_text.h"

namespace glass {
namespace {

constexpr wchar_t kClass[] = L"AimpGlassWindow";
constexpr UINT WM_GLASS_POSTED = WM_APP + 1;
constexpr UINT WM_GLASS_SHOW = WM_APP + 2;
constexpr UINT WM_GLASS_HIDE = WM_APP + 3;
constexpr float kResizeBorder = 6;
enum { kMenuShowFile = 1, kMenuAimpSettings, kMenuClassic };
constexpr UINT_PTR kModalTimer = 1;

std::wstring PickFolder(HWND owner) {
  ComPtr<IFileOpenDialog> dlg;
  if (FAILED(CoCreateInstance(CLSID_FileOpenDialog, nullptr, CLSCTX_INPROC_SERVER, IID_PPV_ARGS(&dlg)))) return {};
  DWORD opts = 0;
  dlg->GetOptions(&opts);
  dlg->SetOptions(opts | FOS_PICKFOLDERS | FOS_FORCEFILESYSTEM);
  dlg->SetTitle(L"Папка с музыкой");
  if (FAILED(dlg->Show(owner))) return {};
  ComPtr<IShellItem> item;
  PWSTR path = nullptr;
  std::wstring out;
  if (SUCCEEDED(dlg->GetResult(&item)) && SUCCEEDED(item->GetDisplayName(SIGDN_FILESYSPATH, &path))) {
    out = path;
    CoTaskMemFree(path);
  }
  return out;
}

bool SamePalette(const Palette& a, const Palette& b) {
  for (int i = 0; i < 3; i++)
    if (a.accent[i].r != b.accent[i].r || a.accent[i].g != b.accent[i].g || a.accent[i].b != b.accent[i].b) return false;
  return true;
}

}  // namespace

struct GlassWindow::Gpu {
  ComPtr<ID3D11Device> device;
  ComPtr<ID3D11DeviceContext> ctx;
  ComPtr<IDXGISwapChain2> swap;
  HANDLE waitable = nullptr;
  ComPtr<ID3D11Texture2D> back;
  ComPtr<ID3D11RenderTargetView> rtv;
  UINT w = 0, h = 0;
  GpuScene scene;
  UiPainter painter;
};

GlassWindow::GlassWindow() = default;
GlassWindow::~GlassWindow() { Stop(); }

bool GlassWindow::Start(Commands* commands, const GlassSettings& settings, std::wstring& error) {
  m_cmd = commands;
  HANDLE ready = CreateEventW(nullptr, TRUE, FALSE, nullptr);
  m_running = true;
  m_thread = std::thread(&GlassWindow::ThreadMain, this, settings, &error, ready);
  WaitForSingleObject(ready, INFINITE);
  CloseHandle(ready);
  if (!m_hwnd) {
    m_running = false;
    if (m_thread.joinable()) m_thread.join();
    return false;
  }
  return true;
}

void GlassWindow::Stop() {
  if (!m_thread.joinable()) return;
  m_running = false;
  if (m_hwnd) PostMessageW(m_hwnd, WM_CLOSE, 1, 0);  // wParam 1: from Stop, not the user
  m_thread.join();
}

void GlassWindow::Post(std::function<void(Model&)> update) {
  {
    std::lock_guard<std::mutex> lock(m_postMu);
    m_posted.push_back(std::move(update));
  }
  if (m_hwnd) PostMessageW(m_hwnd, WM_GLASS_POSTED, 0, 0);
}

void GlassWindow::Show() {
  if (m_hwnd) PostMessageW(m_hwnd, WM_GLASS_SHOW, 0, 0);
}
void GlassWindow::Hide() {
  if (m_hwnd) PostMessageW(m_hwnd, WM_GLASS_HIDE, 0, 0);
}

void GlassWindow::Send(Action a) {
  if (m_cmd) m_cmd->Command(a);
}

void GlassWindow::SettingsChanged() {
  Action a{Act::SettingsChanged};
  a.settings = m_model.settings;
  Send(a);
}

// ---------------------------------------------------------------------------------------------------------

void GlassWindow::ThreadMain(GlassSettings settings, std::wstring* error, HANDLE ready) {
  CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED);
  SetThreadDpiAwarenessContext(DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE_V2);
  QueryPerformanceFrequency(&m_qpcFreq);
  QueryPerformanceCounter(&m_qpcLast);
  m_model.settings = settings;
  m_model.theme = (Theme)settings.theme;
  m_palFrom = m_palTo = m_model.palette;

  // Device and painters first: without them there is no window to show.
  m_gpu = std::make_unique<Gpu>();
  const D3D_FEATURE_LEVEL levels[] = {D3D_FEATURE_LEVEL_11_1, D3D_FEATURE_LEVEL_11_0};
  HRESULT hr = D3D11CreateDevice(nullptr, D3D_DRIVER_TYPE_HARDWARE, nullptr, D3D11_CREATE_DEVICE_BGRA_SUPPORT, levels,
                                 2, D3D11_SDK_VERSION, &m_gpu->device, nullptr, &m_gpu->ctx);
  if (FAILED(hr)) {
    *error = L"Direct3D 11: " + FromUtf8(Log::HrText(hr));
    SetEvent(ready);
    return;
  }
  if (!m_gpu->scene.Init(m_gpu->device.Get(), m_gpu->ctx.Get(), *error) ||
      !m_gpu->painter.Init(m_gpu->device.Get(), *error)) {
    SetEvent(ready);
    return;
  }

  WNDCLASSEXW wc{sizeof(wc)};
  wc.style = CS_DBLCLKS;
  wc.lpfnWndProc = WndProc;
  wc.hInstance = GetModuleHandleW(nullptr);
  wc.hCursor = LoadCursorW(nullptr, IDC_ARROW);
  wc.lpszClassName = kClass;
  wc.hIcon = LoadIconW(GetModuleHandleW(nullptr), MAKEINTRESOURCEW(1));
  RegisterClassExW(&wc);

  // Restore the last placement when it is still on a monitor, else centre on the primary one.
  RECT r{settings.windowX, settings.windowY, settings.windowX + settings.windowW, settings.windowY + settings.windowH};
  if (settings.windowX == -32000 || !MonitorFromRect(&r, MONITOR_DEFAULTTONULL)) {
    RECT work;
    SystemParametersInfoW(SPI_GETWORKAREA, 0, &work, 0);
    int w = std::min<int>(settings.windowW, work.right - work.left), h = std::min<int>(settings.windowH, work.bottom - work.top);
    r = RECT{work.left + (work.right - work.left - w) / 2, work.top + (work.bottom - work.top - h) / 2, 0, 0};
    r.right = r.left + w, r.bottom = r.top + h;
  }
  m_hwnd = CreateWindowExW(WS_EX_APPWINDOW, kClass, L"AIMP", WS_OVERLAPPEDWINDOW, r.left, r.top, r.right - r.left,
                           r.bottom - r.top, nullptr, nullptr, wc.hInstance, this);
  if (!m_hwnd) {
    *error = L"Window creation failed";
    SetEvent(ready);
    return;
  }
  m_dpi = GetDpiForWindow(m_hwnd);
  m_drawScale = DpiScale();
  Log::Info("glass: window created, dpi %u", m_dpi);

  // Shadow and rounded corners from DWM; dark title colours for the system menu and snap previews.
  MARGINS margins{0, 0, 0, 1};
  DwmExtendFrameIntoClientArea(m_hwnd, &margins);
  DWM_WINDOW_CORNER_PREFERENCE corner = DWMWCP_ROUND;
  DwmSetWindowAttribute(m_hwnd, DWMWA_WINDOW_CORNER_PREFERENCE, &corner, sizeof(corner));
  BOOL dark = TRUE;
  DwmSetWindowAttribute(m_hwnd, DWMWA_USE_IMMERSIVE_DARK_MODE, &dark, sizeof(dark));
  SetWindowPos(m_hwnd, nullptr, 0, 0, 0, 0, SWP_FRAMECHANGED | SWP_NOMOVE | SWP_NOSIZE | SWP_NOZORDER);

  // Flip-model swap chain with a frame-latency waitable: render right after the previous frame was shown.
  ComPtr<IDXGIDevice> dxgiDevice;
  ComPtr<IDXGIAdapter> adapter;
  ComPtr<IDXGIFactory2> factory;
  m_gpu->device.As(&dxgiDevice);
  dxgiDevice->GetAdapter(&adapter);
  adapter->GetParent(IID_PPV_ARGS(&factory));
  DXGI_SWAP_CHAIN_DESC1 sd{};
  sd.Format = DXGI_FORMAT_B8G8R8A8_UNORM;
  sd.SampleDesc.Count = 1;
  sd.BufferUsage = DXGI_USAGE_RENDER_TARGET_OUTPUT;
  sd.BufferCount = 2;
  sd.SwapEffect = DXGI_SWAP_EFFECT_FLIP_DISCARD;
  sd.Scaling = DXGI_SCALING_NONE;  // while sizing, never stretch an old frame to the new size
  sd.Flags = DXGI_SWAP_CHAIN_FLAG_FRAME_LATENCY_WAITABLE_OBJECT;
  ComPtr<IDXGISwapChain1> sc1;
  hr = factory->CreateSwapChainForHwnd(m_gpu->device.Get(), m_hwnd, &sd, nullptr, nullptr, &sc1);
  if (SUCCEEDED(hr)) hr = sc1.As(&m_gpu->swap);
  if (FAILED(hr)) {
    *error = L"Swap chain: " + FromUtf8(Log::HrText(hr));
    DestroyWindow(m_hwnd);
    m_hwnd = nullptr;
    SetEvent(ready);
    return;
  }
  factory->MakeWindowAssociation(m_hwnd, DXGI_MWA_NO_ALT_ENTER);
  m_gpu->swap->SetMaximumFrameLatency(1);
  m_gpu->waitable = m_gpu->swap->GetFrameLatencyWaitableObject();
  if (settings.maximized) m_ui.maximized = true;
  SetEvent(ready);

  // Loop: render while visible (every refresh while playing, 30 fps otherwise), sleep while hidden.
  MSG msg;
  double lastFrame = 0;
  while (m_running) {
    const bool visible = IsWindowVisible(m_hwnd) && !IsIconic(m_hwnd);
    if (!visible) {
      if (GetMessageW(&msg, nullptr, 0, 0) <= 0) break;
      TranslateMessage(&msg);
      DispatchMessageW(&msg);
      continue;
    }
    const bool playing = m_model.state == PlayState::Playing;
    LARGE_INTEGER now;
    QueryPerformanceCounter(&now);
    double t = (double)(now.QuadPart) / m_qpcFreq.QuadPart;
    double wait = playing ? 0 : std::max(0.0, 1.0 / 30 - (t - lastFrame));
    DWORD r2 = MsgWaitForMultipleObjectsEx(1, &m_gpu->waitable, (DWORD)(wait * 1000), QS_ALLINPUT,
                                           MWMO_INPUTAVAILABLE);
    while (PeekMessageW(&msg, nullptr, 0, 0, PM_REMOVE)) {
      if (msg.message == WM_QUIT) m_running = false;
      TranslateMessage(&msg);
      DispatchMessageW(&msg);
    }
    if (!m_running || !IsWindowVisible(m_hwnd) || IsIconic(m_hwnd)) continue;
    QueryPerformanceCounter(&now);
    t = (double)(now.QuadPart) / m_qpcFreq.QuadPart;
    if (r2 == WAIT_OBJECT_0 && (playing || t - lastFrame >= 1.0 / 30 - 0.002)) {
      Frame();
      lastFrame = t;
    } else if (!playing && t - lastFrame >= 1.0 / 30) {
      // The waitable was consumed by an earlier frame: wait for it once more before the next one.
      WaitForSingleObject(m_gpu->waitable, 50);
      Frame();
      lastFrame = t;
    }
  }
  if (m_hwnd) DestroyWindow(m_hwnd);
  m_hwnd = nullptr;
  m_gpu.reset();
  UnregisterClassW(kClass, GetModuleHandleW(nullptr));
  CoUninitialize();
}

void GlassWindow::Resize() {
  RECT rc;
  GetClientRect(m_hwnd, &rc);
  UINT w = std::max<LONG>(1, rc.right - rc.left), h = std::max<LONG>(1, rc.bottom - rc.top);
  if (w == m_gpu->w && h == m_gpu->h && m_gpu->rtv) return;
  m_gpu->painter.ReleaseTarget();
  m_gpu->rtv.Reset();
  m_gpu->back.Reset();
  m_gpu->ctx->ClearState();
  m_gpu->ctx->Flush();
  HRESULT hr = m_gpu->swap->ResizeBuffers(0, w, h, DXGI_FORMAT_UNKNOWN, DXGI_SWAP_CHAIN_FLAG_FRAME_LATENCY_WAITABLE_OBJECT);
  if (FAILED(hr)) {
    Log::Error("glass: ResizeBuffers %s", Log::HrText(hr).c_str());
    return;
  }
  m_gpu->swap->GetBuffer(0, IID_PPV_ARGS(&m_gpu->back));
  m_gpu->device->CreateRenderTargetView(m_gpu->back.Get(), nullptr, &m_gpu->rtv);
  m_gpu->w = w, m_gpu->h = h;
}

void GlassWindow::EnterModal() {
  if (m_modal++ == 0) SetTimer(m_hwnd, kModalTimer, 15, nullptr);
}

void GlassWindow::LeaveModal() {
  if (m_modal > 0 && --m_modal == 0) KillTimer(m_hwnd, kModalTimer);
}

void GlassWindow::Frame() {
  if (m_inFrame) return;
  m_inFrame = true;
  struct Done {
    bool& f;
    ~Done() { f = false; }
  } done{m_inFrame};
  LARGE_INTEGER now;
  QueryPerformanceCounter(&now);
  float dt = (float)((now.QuadPart - m_qpcLast.QuadPart) / (double)m_qpcFreq.QuadPart);
  m_qpcLast = now;
  dt = std::clamp(dt, 0.f, 0.1f);
  m_clock += dt;

  // Apply what the player posted.
  std::vector<std::function<void(Model&)>> posted;
  {
    std::lock_guard<std::mutex> lock(m_postMu);
    posted.swap(m_posted);
  }
  if (!posted.empty()) {
    double pos = m_model.position;
    Palette pal = m_model.palette;
    for (auto& f : posted) f(m_model);
    ApplyQuery(m_model);
    if (m_model.position != pos) m_posBase = m_model.position, m_posAt = m_clock;
    if (!SamePalette(pal, m_model.palette)) {
      m_palFrom = MixPalettes(m_palFrom, m_palTo, m_palT);
      m_palTo = m_model.palette;
      m_palT = 0;
    }
  }

  // Animated values for this frame.
  Model m = m_model;
  AudioFrame a = m_audio.Snapshot();
  // The background's own clock runs faster for faster music (smoothly, so a tempo change never jolts it).
  const float target = a.bpm > 0 ? std::clamp(a.bpm / 120.f, 0.65f, 1.5f) : 1.f;
  m_rate += (target - m_rate) * (1 - std::exp(-dt / 1.5f));
  m_animClock += dt * (m_model.state == PlayState::Playing ? m_rate : 0.6);  // paused: slow breathing
  m.time = (float)m_animClock;
  if (m.state == PlayState::Playing && m.track.duration > 0)
    m.position = std::min(m.track.duration, m_posBase + (m_clock - m_posAt));
  m_palT = std::min(1.f, m_palT + dt / 0.6f);
  m.palette = MixPalettes(m_palFrom, m_palTo, m_palT * m_palT * (3 - 2 * m_palT));
  const float beat = m.settings.beat / 100.f;
  for (int i = 0; i < SpectrumBars::kCount; i++) m.spectrum.level[i] = a.bars[i];
  m.bass = std::min(1.f, a.bass * beat), m.high = std::min(1.f, a.high * beat), m.pulse = std::min(1.f, a.pulse * beat);
  m.bpm = a.bpm;
  m.loud = a.loud;
  m.theme = (Theme)m.settings.theme;
  m_ui.likeFlash = std::max(0.f, m_ui.likeFlash - dt / 0.45f);
  m_ui.listScroll += (m_scrollTarget - m_ui.listScroll) * (1 - std::exp(-dt / 0.06f));
  if (std::fabs(m_scrollTarget - m_ui.listScroll) < 0.25f) m_ui.listScroll = m_scrollTarget;

  Resize();
  if (!m_gpu->rtv) return;
  m_drawScale = std::min({DpiScale(), m_gpu->w / kMinWindowW, m_gpu->h / kMinWindowH});
  const float s = Scale();
  m_layout = ComputeLayout(m_gpu->w / s, m_gpu->h / s);
  SceneOptions o;
  o.background = m.settings.background;
  o.coverGlow = m.settings.coverGlow;
  o.ribbons = o.rays = m.settings.ribbons / 100.f;
  o.frost = m.settings.frost / 100.f;
  o.mono = m.settings.mono / 100.f;
  o.seed = (uint32_t)(m_clock * 60);
  m_gpu->scene.Render(m_gpu->rtv.Get(), m_gpu->w, m_gpu->h, s, m, m_layout, o);
  m_gpu->painter.Paint(m_gpu->back.Get(), s, m, m_layout, m_ui);
  m_ui.listTitleR = m_gpu->painter.ListTitleRight();
  if (m.quickSettingsOpen) {
    m_gpu->scene.RenderOverlay(m_gpu->rtv.Get(), m_gpu->w, m_gpu->h, s, m, m_layout, o, m_layout.quick, 0.40f,
                               kPanelRadius);
    m_gpu->painter.PaintQuick(m_gpu->back.Get(), s, m, m_layout, m_ui);
  }
  if (m_ui.menu.kind != MenuKind::None) {
    m_gpu->scene.RenderOverlay(m_gpu->rtv.Get(), m_gpu->w, m_gpu->h, s, m, m_layout, o, MenuRect(m_layout, m_ui.menu),
                               0.f, 14.f);
    m_gpu->painter.PaintMenu(m_gpu->back.Get(), s, m, m_layout, m_ui);
  }
  HRESULT hr = m_gpu->swap->Present(1, 0);
  if (hr == DXGI_ERROR_DEVICE_REMOVED || hr == DXGI_ERROR_DEVICE_RESET)
    Log::Error("glass: device lost: %s", Log::HrText(hr).c_str());
}

// ---------------------------------------------------------------------------------------------------------

LRESULT CALLBACK GlassWindow::WndProc(HWND h, UINT msg, WPARAM wp, LPARAM lp) {
  GlassWindow* self = reinterpret_cast<GlassWindow*>(GetWindowLongPtrW(h, GWLP_USERDATA));
  if (msg == WM_NCCREATE) {
    self = static_cast<GlassWindow*>(reinterpret_cast<CREATESTRUCTW*>(lp)->lpCreateParams);
    SetWindowLongPtrW(h, GWLP_USERDATA, (LONG_PTR)self);
    self->m_hwnd = h;
  }
  return self ? self->Handle(msg, wp, lp) : DefWindowProcW(h, msg, wp, lp);
}

void GlassWindow::UpdateHover(float x, float y) {
  HitResult hr = HitTest(m_layout, m_model, m_ui, x, y);
  m_ui.hover = hr;
  if (!m_tracking) {
    TRACKMOUSEEVENT tme{sizeof(tme), TME_LEAVE, m_hwnd, 0};
    TrackMouseEvent(&tme);
    m_tracking = true;
  }
  if (m_ui.draggingWave) m_ui.dragValue = Fraction(m_layout.wave, x);
  if (m_ui.draggingSlider) DragSlider(x);
  if (m_ui.draggingVolume) {
    m_ui.dragValue = Fraction(m_layout.volume, x);
    Send(Action{Act::Volume, m_ui.dragValue});
    m_model.volume = m_ui.dragValue;
  }
}

LRESULT GlassWindow::Handle(UINT msg, WPARAM wp, LPARAM lp) {
  const float s = Scale();
  switch (msg) {
    case WM_NCCALCSIZE:
      if (wp) {
        // The whole window is client area; when maximised, keep it inside the monitor's work area.
        auto* p = reinterpret_cast<NCCALCSIZE_PARAMS*>(lp);
        if (IsZoomed(m_hwnd)) {
          int f = GetSystemMetricsForDpi(SM_CXFRAME, m_dpi) + GetSystemMetricsForDpi(SM_CXPADDEDBORDER, m_dpi);
          p->rgrc[0].left += f, p->rgrc[0].top += f, p->rgrc[0].right -= f, p->rgrc[0].bottom -= f;
        }
        return 0;
      }
      break;
    case WM_NCHITTEST: {
      POINT pt{GET_X_LPARAM(lp), GET_Y_LPARAM(lp)};
      ScreenToClient(m_hwnd, &pt);
      RECT rc;
      GetClientRect(m_hwnd, &rc);
      const float b = kResizeBorder * s;
      if (!IsZoomed(m_hwnd)) {
        bool l = pt.x < b, r = pt.x >= rc.right - b, t = pt.y < b, d = pt.y >= rc.bottom - b;
        if (t && l) return HTTOPLEFT;
        if (t && r) return HTTOPRIGHT;
        if (d && l) return HTBOTTOMLEFT;
        if (d && r) return HTBOTTOMRIGHT;
        if (l) return HTLEFT;
        if (r) return HTRIGHT;
        if (t) return HTTOP;
        if (d) return HTBOTTOM;
      }
      HitResult hr = HitTest(m_layout, m_model, m_ui, pt.x / s, pt.y / s);
      return hr.hit == Hit::Caption ? HTCAPTION : HTCLIENT;
    }
    case WM_GETMINMAXINFO: {
      auto* mm = reinterpret_cast<MINMAXINFO*>(lp);
      mm->ptMinTrackSize.x = (LONG)(kMinWindowW * DpiScale());
      mm->ptMinTrackSize.y = (LONG)(kMinWindowH * DpiScale());
      return 0;
    }
    case WM_DPICHANGED: {
      m_dpi = HIWORD(wp);
      const RECT* r = reinterpret_cast<const RECT*>(lp);
      SetWindowPos(m_hwnd, nullptr, r->left, r->top, r->right - r->left, r->bottom - r->top, SWP_NOZORDER | SWP_NOACTIVATE);
      return 0;
    }
    case WM_SIZE:
      m_ui.maximized = wp == SIZE_MAXIMIZED;
      // Inside the sizing loop: draw the new size right away, so the window never shows a stretched frame.
      if (m_modal > 0 && wp != SIZE_MINIMIZED) Frame();
      return 0;
    case WM_ENTERSIZEMOVE:
    case WM_ENTERMENULOOP:
      EnterModal();
      return 0;
    case WM_EXITMENULOOP:
      LeaveModal();
      return 0;
    case WM_TIMER:
      if (wp == kModalTimer && IsWindowVisible(m_hwnd) && !IsIconic(m_hwnd)) Frame();
      return 0;
    case WM_ACTIVATE:
      if (LOWORD(wp) == WA_INACTIVE) m_ui.menu = {};  // a menu closes when the window loses focus
      break;
    case WM_EXITSIZEMOVE: {
      LeaveModal();
      // Remember the placement (restored size in px, even when maximised).
      WINDOWPLACEMENT wpl{sizeof(wpl)};
      GetWindowPlacement(m_hwnd, &wpl);
      RECT r = wpl.rcNormalPosition;
      m_model.settings.windowX = r.left, m_model.settings.windowY = r.top;
      m_model.settings.windowW = r.right - r.left, m_model.settings.windowH = r.bottom - r.top;
      m_model.settings.maximized = IsZoomed(m_hwnd) ? 1 : 0;
      SettingsChanged();
      return 0;
    }
    case WM_GLASS_POSTED:
      return 0;  // applied in Frame()
    case WM_GLASS_SHOW:
      ShowWindow(m_hwnd, IsIconic(m_hwnd) ? SW_RESTORE : (m_model.settings.maximized ? SW_SHOWMAXIMIZED : SW_SHOW));
      SetForegroundWindow(m_hwnd);
      return 0;
    case WM_GLASS_HIDE:
      ShowWindow(m_hwnd, SW_HIDE);
      return 0;
    case WM_MOUSEMOVE:
      UpdateHover(GET_X_LPARAM(lp) / s, GET_Y_LPARAM(lp) / s);
      return 0;
    case WM_MOUSELEAVE:
      m_tracking = false;
      m_ui.hover = {};
      return 0;
    case WM_LBUTTONDOWN:
    case WM_LBUTTONDBLCLK: {
      SetCapture(m_hwnd);
      float x = GET_X_LPARAM(lp) / s, y = GET_Y_LPARAM(lp) / s;
      m_ui.pressed = HitTest(m_layout, m_model, m_ui, x, y);
      if (m_ui.pressed.hit == Hit::Wave && m_model.track.duration > 0) {
        m_ui.draggingWave = true;
        m_ui.dragValue = Fraction(m_layout.wave, x);
      } else if (m_ui.pressed.hit == Hit::Volume) {
        m_ui.draggingVolume = true;
        UpdateHover(x, y);
      } else if (m_ui.pressed.hit == Hit::QuickSlider) {
        m_ui.draggingSlider = true;
        DragSlider(x);
      }
      if (m_ui.pressed.hit != Hit::Search && m_ui.searchFocus) m_ui.searchFocus = false;
      return 0;
    }
    case WM_LBUTTONUP: {
      float x = GET_X_LPARAM(lp) / s, y = GET_Y_LPARAM(lp) / s;
      HitResult up = HitTest(m_layout, m_model, m_ui, x, y);
      if (m_ui.draggingWave) {
        m_ui.draggingWave = false;
        double sec = Fraction(m_layout.wave, x) * m_model.track.duration;
        Send(Action{Act::Seek, sec});
        m_model.position = m_posBase = sec;
        m_posAt = m_clock;
      } else if (m_ui.draggingVolume) {
        m_ui.draggingVolume = false;
      } else if (m_ui.pressed.hit == Hit::QuickSlider) {
        m_ui.draggingSlider = false;
        DragSlider(x);
        SettingsChanged();
      } else if (up.hit == m_ui.pressed.hit && up.index == m_ui.pressed.index) {
        Click(up, x, y);
      }
      m_ui.pressed = {};
      ReleaseCapture();  // after the drags have ended, so WM_CAPTURECHANGED finds nothing to cancel
      return 0;
    }
    case WM_CAPTURECHANGED:
      // Another window took the mouse mid-drag (a menu, Alt+Tab): no button-up will come. A slider keeps
      // where it was dragged to; a wave drag is dropped without seeking.
      if ((HWND)lp != m_hwnd) {
        if (m_ui.draggingSlider) SettingsChanged();
        m_ui.draggingWave = m_ui.draggingVolume = m_ui.draggingSlider = false;
        m_ui.pressed = {};
      }
      return 0;
    case WM_MOUSEWHEEL: {
      POINT pt{GET_X_LPARAM(lp), GET_Y_LPARAM(lp)};
      ScreenToClient(m_hwnd, &pt);
      float x = pt.x / s, y = pt.y / s, steps = GET_WHEEL_DELTA_WPARAM(wp) / (float)WHEEL_DELTA;
      HitResult h = HitTest(m_layout, m_model, m_ui, x, y);
      if (h.hit == Hit::Volume || h.hit == Hit::BPlay) {
        float v = std::clamp(m_model.volume + 0.05f * steps, 0.f, 1.f);
        m_model.volume = v;
        Send(Action{Act::Volume, v});
      } else if (m_layout.list.Contains(x, y)) {
        float maxS = MaxScroll(m_layout, (int)m_model.visible.size());
        m_scrollTarget = std::clamp(m_scrollTarget - steps * m_layout.rowHeight * 3, 0.f, maxS);
      }
      return 0;
    }
    case WM_KEYDOWN:
      Key((UINT)wp);
      return 0;
    case WM_CHAR:
      Char((wchar_t)wp);
      return 0;
    case WM_CLOSE:
      if (wp == 1 || !m_running) {
        PostQuitMessage(0);
        return 0;
      }
      Send(Action{Act::Quit});
      return 0;
    case WM_ERASEBKGND:
      return 1;
  }
  return DefWindowProcW(m_hwnd, msg, wp, lp);
}

// A quick settings slider follows the mouse while pressed (pressed.index is its QuickItem).
void GlassWindow::DragSlider(float x) {
  QuickRow rows[(int)QuickItem::Count];
  int n = QuickRows(m_layout, rows, (int)QuickItem::Count);
  for (int i = 0; i < n; i++)
    if ((int)rows[i].item == m_ui.pressed.index)
      if (int* v = QuickSliderValue(m_model.settings, rows[i].item))
        *v = (int)std::lround(Fraction(rows[i].control, x) * QuickSliderMax(rows[i].item));
}

void GlassWindow::Click(const HitResult& h, float x, float) {
  Model& m = m_model;
  switch (h.hit) {
    case Hit::Close: Send(Action{Act::Quit}); break;
    case Hit::Min: ShowWindow(m_hwnd, SW_MINIMIZE); break;
    case Hit::Max: ShowWindow(m_hwnd, IsZoomed(m_hwnd) ? SW_RESTORE : SW_MAXIMIZE); break;
    case Hit::Nav0: case Hit::Nav1: case Hit::Nav2: case Hit::Nav3: {
      int i = (int)h.hit - (int)Hit::Nav0;
      m.query.clear();
      m_scrollTarget = m_ui.listScroll = 0;
      Send(Action{Act::Nav, 0, i});
      break;
    }
    case Hit::AddFolder: {
      EnterModal();  // the folder dialog runs its own loop; keep the window alive behind it
      std::wstring path = PickFolder(m_hwnd);
      LeaveModal();
      if (!path.empty()) {
        Action a{Act::AddFolder};
        a.text = path;
        Send(a);
      }
      break;
    }
    case Hit::Scan: Send(Action{Act::Scan}); break;
    case Hit::ListTitle: {
      // AIMP's playlists in a menu under the title: the shown one checked, the playing one marked.
      PopupMenu menu;
      menu.kind = MenuKind::Playlists;
      menu.anchor = m_layout.listTitle;
      menu.anchor.l -= 6;
      for (int i = 0; i < (int)m.playlists.size() && i < (int)m.playlistIds.size(); i++) {
        PopupItem it;
        it.text = m.playlists[i];
        it.hint = i == m.playlistPlaying ? L"играет" : L"";
        it.key = m.playlistIds[i];
        it.id = i;
        it.checked = i == m.playlistShown;
        menu.items.push_back(std::move(it));
      }
      m_ui.menu = std::move(menu);
      break;
    }
    case Hit::NavSettings: case Hit::Settings: m.quickSettingsOpen = !m.quickSettingsOpen; break;
    case Hit::Shuffle: m.shuffle = !m.shuffle; Send(Action{Act::Shuffle, m.shuffle ? 1.0 : 0.0}); break;
    case Hit::Repeat: {
      m.repeat = m.repeat == Repeat::Off ? Repeat::All : Repeat::Off;
      Send(Action{Act::Repeat, (double)(int)m.repeat});
      break;
    }
    case Hit::Prev: case Hit::BPrev: Send(Action{Act::Prev}); break;
    case Hit::Next: case Hit::BNext: Send(Action{Act::Next}); break;
    case Hit::Play: case Hit::BPlay: Send(Action{Act::PlayPause}); break;
    case Hit::Like:
      m.track.favorite = !m.track.favorite;
      if (m.track.favorite) m_ui.likeFlash = 1;
      Send(Action{Act::Like, m.track.favorite ? 1.0 : 0.0});
      break;
    case Hit::More: {
      // A small menu under the button: things that do not deserve a place of their own.
      PopupMenu menu;
      menu.kind = MenuKind::More;
      menu.anchor = m_layout.btnMore;
      menu.alignRight = true;
      auto item = [&](const wchar_t* text, int id, bool enabled = true) {
        PopupItem it;
        it.text = text, it.id = id, it.enabled = enabled;
        menu.items.push_back(std::move(it));
      };
      item(L"Показать файл в папке", kMenuShowFile, !m.track.path.empty());
      item(L"Все настройки AIMP\u2026", kMenuAimpSettings);
      PopupItem sep;
      sep.separator = true;
      menu.items.push_back(sep);
      item(L"Вернуть классический AIMP", kMenuClassic);
      m_ui.menu = std::move(menu);
      break;
    }
    case Hit::MenuItem: {
      PopupMenu menu = std::move(m_ui.menu);
      m_ui.menu = {};
      RunMenu(menu, h.index);
      break;
    }
    case Hit::MenuClose: m_ui.menu = {}; break;
    case Hit::Search:
      m_ui.searchFocus = true;
      // A click on the clear cross empties the box.
      if (!m.query.empty() && x > m_layout.search.r - 26) {
        m.query.clear();
        ApplyQuery(m);
      }
      m_ui.caret = m.query.size();
      break;
    case Hit::Row:
      if (h.index >= 0 && h.index < (int)m.visible.size()) Send(Action{Act::PlayRow, 0, m.visible[h.index]});
      break;
    case Hit::QuickClose: m.quickSettingsOpen = false; break;
    case Hit::QuickAimpSettings: m.quickSettingsOpen = false; Send(Action{Act::AimpSettings}); break;
    case Hit::QuickClassic: m.quickSettingsOpen = false; Send(Action{Act::Classic}); break;
    case Hit::QuickTheme:
      m.settings.theme = std::clamp(h.index, 0, 2);
      SettingsChanged();
      break;
    case Hit::QuickToggle: {
      QuickItem it = (QuickItem)h.index;
      auto flip = [&](bool& v) { v = !v; };
      switch (it) {
        case QuickItem::Fading: flip(m.aimpFading); Send(Action{Act::AimpOption, m.aimpFading ? 1.0 : 0.0, h.index}); break;
        case QuickItem::PauseBetween: flip(m.aimpPause); Send(Action{Act::AimpOption, m.aimpPause ? 1.0 : 0.0, h.index}); break;
        case QuickItem::Equalizer: flip(m.aimpEq); Send(Action{Act::AimpOption, m.aimpEq ? 1.0 : 0.0, h.index}); break;
        case QuickItem::Background: m.settings.background = !m.settings.background; SettingsChanged(); break;
        case QuickItem::CoverGlow: m.settings.coverGlow = !m.settings.coverGlow; SettingsChanged(); break;
        case QuickItem::Spectrum: m.settings.spectrum = !m.settings.spectrum; SettingsChanged(); break;
        case QuickItem::AutoScan: m.settings.autoScan = !m.settings.autoScan; SettingsChanged(); break;
        default: break;
      }
      break;
    }
    default: break;
  }
}

void GlassWindow::RunMenu(const PopupMenu& menu, int item) {
  if (item < 0 || item >= (int)menu.items.size()) return;
  const PopupItem& it = menu.items[item];
  if (it.separator || !it.enabled) return;
  Model& m = m_model;
  if (menu.kind == MenuKind::More) {
    if (it.id == kMenuShowFile) {
      std::wstring args = L"/select,\"" + m.track.path + L"\"";
      ShellExecuteW(m_hwnd, nullptr, L"explorer.exe", args.c_str(), nullptr, SW_SHOWNORMAL);
    } else if (it.id == kMenuAimpSettings) {
      Send(Action{Act::AimpSettings});
    } else if (it.id == kMenuClassic) {
      Send(Action{Act::Classic});
    }
  } else if (menu.kind == MenuKind::Playlists && !it.checked && !it.key.empty()) {
    m.query.clear();
    m_scrollTarget = m_ui.listScroll = 0;
    Send(Action{Act::Playlist, 0, it.id, it.key});
  }
}

void GlassWindow::Key(UINT vk) {
  Model& m = m_model;
  // An open menu has the keyboard: arrows choose, Enter runs, Esc closes.
  if (m_ui.menu.kind != MenuKind::None) {
    if (vk == VK_ESCAPE) {
      m_ui.menu = {};
    } else if (vk == VK_DOWN || vk == VK_UP) {
      m_ui.menu.hot = MenuStep(m_ui.menu, m_ui.menu.hot, vk == VK_DOWN ? 1 : -1);
    } else if ((vk == VK_RETURN || vk == VK_SPACE) && m_ui.menu.hot >= 0) {
      PopupMenu menu = std::move(m_ui.menu);
      m_ui.menu = {};
      RunMenu(menu, menu.hot);
    }
    return;
  }
  const bool ctrl = GetKeyState(VK_CONTROL) < 0;
  if (ctrl && vk == 'F') {
    m_ui.searchFocus = true;
    m_ui.caret = m.query.size();
    return;
  }
  if (vk == VK_ESCAPE) {
    if (m.quickSettingsOpen) m.quickSettingsOpen = false;
    else if (m_ui.searchFocus && !m.query.empty()) { m.query.clear(); ApplyQuery(m); m_ui.caret = 0; }
    else m_ui.searchFocus = false;
    return;
  }
  if (m_ui.searchFocus) {
    if (vk == VK_LEFT && m_ui.caret > 0) m_ui.caret--;
    else if (vk == VK_RIGHT && m_ui.caret < m.query.size()) m_ui.caret++;
    else if (vk == VK_HOME) m_ui.caret = 0;
    else if (vk == VK_END) m_ui.caret = m.query.size();
    else if (vk == VK_DELETE && m_ui.caret < m.query.size()) { m.query.erase(m_ui.caret, 1); ApplyQuery(m); }
    else if (vk == VK_RETURN && !m.visible.empty()) Send(Action{Act::PlayRow, 0, m.visible[0]});
    m_scrollTarget = std::min(m_scrollTarget, MaxScroll(m_layout, (int)m.visible.size()));
    return;
  }
  switch (vk) {
    case VK_SPACE: Send(Action{Act::PlayPause}); break;
    case VK_LEFT: Send(Action{Act::Seek, std::max(0.0, m.position - 5)}); break;
    case VK_RIGHT: Send(Action{Act::Seek, std::min(m.track.duration, m.position + 5)}); break;
    case VK_UP: m.volume = std::min(1.f, m.volume + 0.05f); Send(Action{Act::Volume, m.volume}); break;
    case VK_DOWN: m.volume = std::max(0.f, m.volume - 0.05f); Send(Action{Act::Volume, m.volume}); break;
    default: break;
  }
}

void GlassWindow::Char(wchar_t c) {
  if (!m_ui.searchFocus) return;
  Model& m = m_model;
  if (c == 8) {  // backspace
    if (m_ui.caret > 0) {
      m.query.erase(m_ui.caret - 1, 1);
      m_ui.caret--;
    }
  } else if (c >= 32 && c != 127) {
    m.query.insert(m_ui.caret, 1, c);
    m_ui.caret++;
  } else {
    return;
  }
  ApplyQuery(m);
  m_scrollTarget = m_ui.listScroll = 0;
}

}  // namespace glass
