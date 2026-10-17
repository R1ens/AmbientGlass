#pragma once
#include <windows.h>

#include <string>

#include "bar_detector.h"
#include "frame_source.h"
#include "ipc.h"
#include "layout.h"
#include "overlay.h"
#include "panel.h"
#include "renderer.h"
#include "settings.h"
#include "tray.h"
#include "vlc_tracker.h"
#include "window_thread.h"

// The scheduler: waits for frames, commands and window events, then tracks VLC, feeds the renderer,
// lays out the overlays and draws the panel. Everything else lives in its own module.
class App {
 public:
  enum ExitCode { kOk = 0, kFailed = 1, kRestart = 2 };

  int Run(HINSTANCE inst, const std::string& command, bool background);

 private:
  static LRESULT CALLBACK MsgProc(HWND, UINT, WPARAM, LPARAM);
  static void CALLBACK WinEventProc(HWINEVENTHOOK, DWORD event, HWND, LONG idObject, LONG, DWORD, DWORD);

  bool Setup(HINSTANCE inst);
  void Teardown();
  void Tick();
  void Track();
  void UpdateOverlays(bool newFrame);
  void DrawOverlay(Overlay& ov, GlowTargets& targets, const GlowLayout& layout, bool hdr);
  void HideOverlays();
  void HandleCommand(const std::string& cmd);
  void ShowPanel();
  void HookVlc(DWORD pid);
  bool Animating(ULONGLONG now) const;
  bool EnsureInner();
  void KeepOverlaysAboveVlc();

  static App* s_self;

  HINSTANCE m_inst = nullptr;
  std::wstring m_exe, m_settingsPath, m_luaPath;
  bool m_luaInstalled = false;
  Settings m_settings;
  Renderer m_renderer;
  WindowThread m_windows;  // owns the overlay windows (declared before them: destroyed after them)
  Overlay m_inner, m_outer;
  GlowTargets m_innerTargets, m_outerTargets;
  Panel m_panel;
  VlcTracker m_tracker;
  VlcState m_vlc;
  FrameSource m_frames;
  BarDetector m_bars;
  LumaGrid m_luma;
  Ipc m_ipc;
  TrayIcon m_tray;

  HWND m_msgWnd = nullptr;
  HWINEVENTHOOK m_fgHook = nullptr, m_locHook = nullptr, m_orderHook = nullptr;
  DWORD m_hookPid = 0;
  HMONITOR m_monitor = nullptr;

  bool m_running = true;
  bool m_restart = false;
  bool m_geometryDirty = true;
  bool m_redraw = true;
  ULONGLONG m_lastTrack = 0, m_saveAt = 0, m_whiteTick = 0;
  ULONGLONG m_retryCaptureAt = 0, m_hdrUntil = 0, m_lastRenderTick = 0;
  LONGLONG m_lastProjQpc = 0, m_qpcFreq = 1;
  float m_renderMs = 0, m_latencyMs = 0;
};
