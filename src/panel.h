#pragma once
#include <windows.h>
#include <d3d11.h>
#include <dxgi1_2.h>
#include <wrl/client.h>

#include <string>

#include "settings.h"

// What the panel shows in its status block. Filled by the app every frame.
struct PanelStatus {
  bool vlcFound = false;      // a VLC window with video is visible
  bool captureOk = false;     // frames are arriving from that window
  bool fullscreen = false;
  int pictureW = 0, pictureH = 0;  // picture size on screen, px
  float captureFps = 0.f;     // frames per second received from VLC
  float renderMs = 0.f;       // CPU time of the last glow update, ms
  float latencyMs = 0.f;      // from the composition of VLC's frame to the glow's Present, ms
  bool hdr = false;           // HDR values detected in the video (HDR filters are in use)
  float bars[4] = {0, 0, 0, 0};  // detected burned-in bars: top, bottom, left, right (fraction of picture)
  bool luaInstalled = false;  // the VLC menu item file is in place
  std::wstring luaPath;       // where it is installed
  std::string lastError;      // the most recent logged error, UTF-8; empty when none
};

enum PanelAction : int {
  PanelAction_None = 0,
  PanelAction_Quit = 1 << 0,          // user pressed "Выход"
  PanelAction_ReinstallLua = 1 << 1,  // user pressed "Переустановить пункт меню"
};

// Settings window with sliders (Dear ImGui on its own D3D11 swap chain).
// Closing the window only hides it; the app keeps running in the tray.
class Panel {
 public:
  bool Create(HINSTANCE inst, ID3D11Device* device, ID3D11DeviceContext* ctx);
  void Destroy();

  void Show();    // shows, restores if minimized, brings to front
  void Hide();
  bool Visible() const;
  HWND Hwnd() const { return m_hwnd; }

  // Draws one frame if visible. Edits `s` in place; sets `changed` when any setting changed.
  // Returns a combination of PanelAction flags.
  int Frame(Settings& s, const PanelStatus& status, bool& changed);

 private:
  static LRESULT CALLBACK WndProc(HWND, UINT, WPARAM, LPARAM);
  LRESULT HandleMessage(HWND, UINT, WPARAM, LPARAM);
  void CreateRenderTarget();
  void LoadFonts(float dpiScale);

  HWND m_hwnd = nullptr;
  ID3D11Device* m_device = nullptr;
  ID3D11DeviceContext* m_ctx = nullptr;
  Microsoft::WRL::ComPtr<IDXGISwapChain1> m_swap;
  Microsoft::WRL::ComPtr<ID3D11RenderTargetView> m_rtv;
  UINT m_resizeW = 0, m_resizeH = 0;
  float m_dpiScale = 1.f;
  bool m_fontsDirty = false;
};
