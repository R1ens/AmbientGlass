#include "app.h"

#include <algorithm>
#include <cmath>

#include "common.h"
#include "display_info.h"
#include "log.h"
#include "system_integration.h"
#include "zorder.h"

namespace {

constexpr UINT WM_TRAY = WM_APP + 1;
constexpr ULONGLONG kTrackActiveMs = 50, kTrackIdleMs = 400;  // how often to look for VLC
constexpr ULONGLONG kCaptureRetryMs = 2000;
constexpr ULONGLONG kWhiteLevelMs = 3000;   // HDR can be toggled any time
constexpr ULONGLONG kHdrHoldMs = 10000;     // HDR filters stay on this long after the last HDR frame
constexpr ULONGLONG kSaveDelayMs = 500;     // settle time before settings are written
constexpr float kHdrThreshold = 1.1f;       // linear, 1.0 = SDR white
constexpr float kMaxProjectorStepMs = 100.f;  // so the first change after a pause still eases in
constexpr float kCutFadeMaxStretch = 2.5f;     // scrubbing lengthens the scene-change fade up to this (glow.hlsl)

}  // namespace

App* App::s_self = nullptr;

int App::Run(HINSTANCE inst, const std::string& command, bool background) {
  s_self = this;
  if (!Setup(inst)) {
    Teardown();
    return kFailed;
  }
  if (!background || !command.empty()) HandleCommand(command.empty() ? "show" : command);

  while (m_running) {
    HANDLE handles[2];
    DWORD count = 0;
    handles[count++] = m_frames.Event();
    if (m_ipc.WaitHandle() != INVALID_HANDLE_VALUE) handles[count++] = m_ipc.WaitHandle();
    DWORD timeout = m_panel.Visible() ? 8 : Animating(GetTickCount64()) ? 16 : m_vlc.Active() ? 40 : 200;
    DWORD r = MsgWaitForMultipleObjectsEx(count, handles, timeout, QS_ALLINPUT, MWMO_INPUTAVAILABLE);
    if (count > 1 && r == WAIT_OBJECT_0 + 1) {
      m_ipc.OnSignalled();
      std::string cmd = m_ipc.PollCommand();
      if (!cmd.empty()) HandleCommand(cmd);
    }
    MSG msg;
    while (PeekMessageW(&msg, nullptr, 0, 0, PM_REMOVE)) {
      if (msg.message == WM_QUIT) m_running = false;
      TranslateMessage(&msg);
      DispatchMessageW(&msg);
    }
    if (m_running) Tick();
  }

  Teardown();
  return m_restart ? kRestart : kOk;
}

bool App::Setup(HINSTANCE inst) {
  m_inst = inst;
  // The glow chases VLC's frames: wake up for them ahead of ordinary background work.
  SetThreadPriority(GetCurrentThread(), THREAD_PRIORITY_ABOVE_NORMAL);
  LARGE_INTEGER f;
  QueryPerformanceFrequency(&f);
  m_qpcFreq = f.QuadPart;
  m_exe = ExePath();
  std::wstring dataDir = Ipc::DataDir();
  Log::Init(dataDir + L"\\log.txt");
  Log::Info("---- VlcAmbient starting: %s", ToUtf8(m_exe).c_str());
  m_settingsPath = dataDir + L"\\settings.ini";
  m_ipc.Init(m_exe);
  if (!LoadSettings(m_settingsPath, m_settings)) Log::Info("settings: none yet, using defaults");

  std::wstring error;
  if (!m_renderer.Init(error)) {
    MessageBoxW(nullptr, error.c_str(), L"VLC Ambient Light", MB_ICONERROR);
    return false;
  }
  WindowCapture::RequestBorderless();
  if (!m_windows.Start() || !m_inner.Create(m_renderer.Device(), m_renderer.DComp(), m_windows) ||
      !m_outer.Create(m_renderer.Device(), m_renderer.DComp(), m_windows) ||
      !m_panel.Create(inst, m_renderer.Device(), m_renderer.Context())) {
    MessageBoxW(nullptr, L"Не удалось создать окна приложения.", L"VLC Ambient Light", MB_ICONERROR);
    return false;
  }

  WNDCLASSEXW wc{sizeof(wc)};
  wc.lpfnWndProc = MsgProc;
  wc.hInstance = inst;
  wc.lpszClassName = L"VlcAmbientMsg";
  RegisterClassExW(&wc);
  m_msgWnd = CreateWindowExW(WS_EX_TOOLWINDOW, wc.lpszClassName, L"VLC Ambient Light", WS_POPUP, 0, 0, 0, 0, nullptr,
                             nullptr, inst, nullptr);
  m_tray.Create(inst, m_msgWnd, WM_TRAY);
  m_luaInstalled = InstallLuaExtension(inst, false, m_luaPath);
  if (m_settings.autostart) SetAutostart(true, m_exe);

  m_fgHook = SetWinEventHook(EVENT_SYSTEM_FOREGROUND, EVENT_SYSTEM_FOREGROUND, nullptr, WinEventProc, 0, 0,
                             WINEVENT_OUTOFCONTEXT | WINEVENT_SKIPOWNPROCESS);
  // A click on VLC raises it above the overlay; put the overlay back as soon as the order changes,
  // not on the next tracking tick, or the glow in the bars blinks.
  m_orderHook = SetWinEventHook(EVENT_OBJECT_REORDER, EVENT_OBJECT_REORDER, nullptr, WinEventProc, 0, 0,
                                WINEVENT_OUTOFCONTEXT | WINEVENT_SKIPOWNPROCESS);
  return true;
}

void App::Teardown() {
  if (m_saveAt) SaveSettings(m_settingsPath, m_settings);
  m_tray.Remove();
  if (m_fgHook) UnhookWinEvent(m_fgHook);
  if (m_locHook) UnhookWinEvent(m_locHook);
  if (m_orderHook) UnhookWinEvent(m_orderHook);
  m_fgHook = m_locHook = m_orderHook = nullptr;
  m_ipc.Shutdown();
  m_frames.Stop();
  m_panel.Destroy();
  m_inner.Destroy();
  m_outer.Destroy();
  m_windows.Stop();
  if (m_msgWnd) DestroyWindow(m_msgWnd);
  m_msgWnd = nullptr;
  Log::Info("---- VlcAmbient stopped%s", m_restart ? " (restarting)" : "");
  s_self = nullptr;
}

void App::Tick() {
  ULONGLONG now = GetTickCount64();
  if (!m_renderer.DeviceOk()) {
    Log::Error("renderer: device lost, restarting");
    m_restart = true;
    m_running = false;
    return;
  }
  // A changed window layout must be known before drawing; the periodic look-around waits until the
  // fresh frame is on its way, so it never delays the glow.
  if (m_geometryDirty) {
    Track();
    m_lastTrack = now;
  }

  bool stale = false;
  bool newFrame = m_frames.Pump(m_renderer, m_vlc, now, stale);
  if (stale) m_redraw = true;
  if (m_renderer.ReadLuma(m_luma)) {
    if (m_luma.maxLinear > kHdrThreshold) m_hdrUntil = now + kHdrHoldMs;
    if (m_bars.Feed(m_luma, m_settings.detectBars, m_settings.detectSideBars)) m_redraw = true;
  }

  LARGE_INTEGER t0, t1, freq;
  QueryPerformanceCounter(&t0);
  UpdateOverlays(newFrame);
  QueryPerformanceCounter(&t1);
  QueryPerformanceFrequency(&freq);
  if (newFrame) {
    m_renderMs = (float)((t1.QuadPart - t0.QuadPart) * 1000.0 / freq.QuadPart);
    float latency = (float)((t1.QuadPart - m_frames.LastComposedQpc()) * 1000.0 / freq.QuadPart);
    if (latency >= 0 && latency < 1000) m_latencyMs += (latency - m_latencyMs) * 0.1f;
  }
  // Without VLC on screen look for it rarely; the foreground hook still reacts at once.
  if (now - m_lastTrack >= (m_vlc.Active() ? kTrackActiveMs : kTrackIdleMs)) {
    Track();
    m_lastTrack = now;
  }

  PanelStatus st;
  st.vlcFound = m_vlc.Active();
  st.captureOk = m_frames.Target() != nullptr && m_frames.HasFrame();
  st.fullscreen = m_vlc.fullscreen;
  st.pictureW = RectW(m_vlc.pictureRect);
  st.pictureH = RectH(m_vlc.pictureRect);
  st.captureFps = m_frames.Fps(now);
  st.renderMs = m_renderMs;
  st.latencyMs = m_latencyMs;
  st.hdr = now < m_hdrUntil;
  st.bars[0] = st.bars[1] = m_settings.detectBars ? m_bars.Vertical() : 0.f;
  st.bars[2] = st.bars[3] = m_settings.detectSideBars ? m_bars.Horizontal() : 0.f;
  st.luaInstalled = m_luaInstalled;
  st.luaPath = m_luaPath;
  st.lastError = Log::LastError();
  Settings before = m_settings;
  bool changed = false;
  int action = m_panel.Frame(m_settings, st, changed);
  if (changed) {
    m_settings.Clamp();
    if (m_settings.autostart != before.autostart) SetAutostart(m_settings.autostart, m_exe);
    if (m_settings.enabled != before.enabled) m_geometryDirty = true;
    m_saveAt = now + kSaveDelayMs;
    m_redraw = true;
  }
  if (action & PanelAction_ReinstallLua) m_luaInstalled = InstallLuaExtension(m_inst, true, m_luaPath);
  if (action & PanelAction_Quit) m_running = false;

  if (m_saveAt && now >= m_saveAt) {
    if (!SaveSettings(m_settingsPath, m_settings)) Log::Error("settings: cannot write %s", ToUtf8(m_settingsPath).c_str());
    m_saveAt = 0;
  }
  m_ipc.Heartbeat(now);
}

void App::Track() {
  m_geometryDirty = false;
  VlcState s = m_settings.enabled ? m_tracker.Find() : VlcState{};
  if (!s.Active()) {
    if (m_frames.Target()) {
      Log::Info("vlc: gone");
      m_frames.Stop();
      m_renderer.DropSource();
    }
    m_vlc = s;
    HideOverlays();
    m_inner.SetOwner(nullptr);  // no input shared with a VLC that shows no video
    EnsureInner();
    return;
  }
  if (RectW(s.pictureRect) != RectW(m_vlc.pictureRect) || RectH(s.pictureRect) != RectH(m_vlc.pictureRect) ||
      s.main != m_vlc.main)
    m_bars.Reset();
  ULONGLONG now = GetTickCount64();
  if (m_frames.Target() != s.main && now >= m_retryCaptureAt) {
    m_renderer.DropSource();
    if (!m_frames.Start(m_renderer.Device(), s.main)) m_retryCaptureAt = now + kCaptureRetryMs;
  }
  if (s.pid != m_hookPid) HookVlc(s.pid);
  HMONITOR monitor = MonitorFromWindow(s.main, MONITOR_DEFAULTTONEAREST);
  if (monitor != m_monitor || now - m_whiteTick > kWhiteLevelMs) {
    m_renderer.SetWhiteLevel(SdrWhiteLevel(monitor));
    m_monitor = monitor;
    m_whiteTick = now;
  }
  if (!SameRect(s.mainRect, m_vlc.mainRect) || !SameRect(s.videoMainRect, m_vlc.videoMainRect) ||
      !SameRect(s.pictureRect, m_vlc.pictureRect) || s.fullscreen != m_vlc.fullscreen)
    m_redraw = true;
  m_vlc = s;
  if (EnsureInner()) m_inner.SetOwner(m_vlc.main);
  PlaceOverlays(m_vlc.main, m_inner, m_outer);
}

// The inner overlay is owned by VLC's window, and Windows destroys owned windows with their owner:
// when VLC closes, a new one is made for the next time.
bool App::EnsureInner() {
  if (m_inner.Alive()) return true;
  Log::Info("overlay: window went away with VLC's, making a new one");
  m_inner.Destroy();
  m_redraw = true;
  return m_inner.Create(m_renderer.Device(), m_renderer.DComp(), m_windows);
}

void App::UpdateOverlays(bool newFrame) {
  if (!m_settings.enabled || !m_vlc.Active() || !m_frames.HasFrame()) {
    HideOverlays();
    return;
  }
  ULONGLONG now = GetTickCount64();
  // Easing toward a new colour keeps rendering after the last frame, at most at display rate.
  if (!newFrame && !m_redraw && (!Animating(now) || now - m_lastRenderTick < 15)) return;
  m_redraw = false;
  m_lastRenderTick = now;

  Bars bars{m_settings.detectBars ? m_bars.Vertical() : 0.f, m_settings.detectSideBars ? m_bars.Horizontal() : 0.f};
  Layouts L = ComputeLayouts(m_vlc, bars, m_settings);

  // Time steps from the performance counter: the tick count moves in 15.6 ms jumps.
  LARGE_INTEGER q;
  QueryPerformanceCounter(&q);
  float dt = m_lastProjQpc ? (float)((q.QuadPart - m_lastProjQpc) * 1000.0 / m_qpcFreq) : 0.f;
  m_lastProjQpc = q.QuadPart;
  m_renderer.UpdateProjector(L.contentUV, std::min(dt, kMaxProjectorStepMs), (float)m_settings.smoothingMs,
                             (float)m_settings.cutFadeMs);
  bool hdr = now < m_hdrUntil;

  if (L.inner) DrawOverlay(m_inner, m_innerTargets, L.innerL, hdr);
  else m_inner.Show(false);
  if (L.outer) DrawOverlay(m_outer, m_outerTargets, L.outerL, hdr);
  else m_outer.Show(false);
}

void App::DrawOverlay(Overlay& ov, GlowTargets& targets, const GlowLayout& layout, bool hdr) {
  ov.SetRect(layout.overlay);
  ID3D11RenderTargetView* back = ov.BackBuffer();
  if (!back) return;
  m_renderer.RenderTo(back, (UINT)ov.Width(), (UINT)ov.Height(), targets, layout, m_settings, hdr);
  ov.Present();
  if (!ov.Visible()) {
    ov.Show(true);
    PlaceOverlays(m_vlc.main, m_inner, m_outer);
  }
}

void App::HideOverlays() {
  m_inner.Show(false);
  m_outer.Show(false);
}

void App::HookVlc(DWORD pid) {
  if (m_locHook) UnhookWinEvent(m_locHook);
  m_locHook = SetWinEventHook(EVENT_OBJECT_LOCATIONCHANGE, EVENT_OBJECT_LOCATIONCHANGE, nullptr, WinEventProc, pid, 0,
                              WINEVENT_OUTOFCONTEXT);
  m_hookPid = pid;
}

void CALLBACK App::WinEventProc(HWINEVENTHOOK, DWORD event, HWND, LONG idObject, LONG, DWORD, DWORD) {
  if (!s_self) return;
  if (event == EVENT_OBJECT_REORDER || event == EVENT_SYSTEM_FOREGROUND) s_self->KeepOverlaysAboveVlc();
  if (event == EVENT_OBJECT_REORDER) return;  // only the order changed, not the layout
  if (event == EVENT_OBJECT_LOCATIONCHANGE && idObject != OBJID_WINDOW) return;
  s_self->m_geometryDirty = true;
}

void App::KeepOverlaysAboveVlc() {
  if (m_vlc.main && IsWindow(m_vlc.main)) PlaceOverlays(m_vlc.main, m_inner, m_outer);
}

// After the last frame the glow may still be easing: steady smoothing for ~5 time constants, a
// scene-change fade (possibly stretched by scrubbing) for ~3 of its longest.
bool App::Animating(ULONGLONG now) const {
  if (!m_frames.HasFrame()) return false;
  float window = std::max(m_settings.smoothingMs * 5.f, m_settings.cutFadeMs * kCutFadeMaxStretch * 3.f);
  return window > 0 && now - m_frames.LastFrameTick() < (ULONGLONG)window;
}

void App::HandleCommand(const std::string& cmd) {
  if (cmd == "show") {
    ShowPanel();
  } else if (cmd == "hide") {
    m_panel.Hide();
  } else if (cmd == "toggle") {
    m_settings.enabled = !m_settings.enabled;
    m_geometryDirty = m_redraw = true;
    m_saveAt = GetTickCount64() + kSaveDelayMs;
  } else if (cmd == "quit") {
    m_running = false;
  } else {
    Log::Warn("command '%s' is unknown", cmd.c_str());
  }
}

// The request comes from VLC (another process), so Windows would refuse to activate our window;
// borrowing the foreground thread's input state lets the panel come to the front.
void App::ShowPanel() {
  HWND fg = GetForegroundWindow();
  DWORD fgThread = fg ? GetWindowThreadProcessId(fg, nullptr) : 0;
  DWORD me = GetCurrentThreadId();
  bool attached = fgThread && fgThread != me && AttachThreadInput(me, fgThread, TRUE);
  m_panel.Show();
  BringWindowToTop(m_panel.Hwnd());
  SetForegroundWindow(m_panel.Hwnd());
  if (attached) AttachThreadInput(me, fgThread, FALSE);
}

LRESULT CALLBACK App::MsgProc(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp) {
  App* app = s_self;
  if (app && msg == WM_TRAY) {
    switch (app->m_tray.OnMessage(lp, app->m_settings.enabled)) {
      case TrayAction::TogglePanel:
        if (app->m_panel.Visible()) app->m_panel.Hide();
        else app->ShowPanel();
        break;
      case TrayAction::OpenPanel:
        app->ShowPanel();
        break;
      case TrayAction::ToggleGlow:
        app->HandleCommand("toggle");
        break;
      case TrayAction::Quit:
        app->m_running = false;
        break;
      case TrayAction::None:
        break;
    }
    return 0;
  }
  if (app && msg != 0 && msg == app->m_tray.TaskbarCreatedMessage()) {
    app->m_tray.Recreate();
    return 0;
  }
  return DefWindowProcW(hwnd, msg, wp, lp);
}
