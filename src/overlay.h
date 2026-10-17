#pragma once
#include <windows.h>
#include <d3d11.h>
#include <dcomp.h>
#include <dxgi1_4.h>
#include <wrl/client.h>

#include "window_thread.h"

using Microsoft::WRL::ComPtr;

// A click-through, borderless window that shows an FP16 scRGB swap chain with per-pixel alpha
// through DirectComposition. Never takes focus and never appears in the taskbar. The window itself
// lives on `windows` (see WindowThread); drawing and presenting happen on the caller's thread.
class Overlay {
 public:
  bool Create(ID3D11Device* device, IDCompositionDevice* dcomp, WindowThread& windows);
  void Destroy();
  // False once the window is gone: Windows destroys owned windows together with their owner.
  bool Alive() const { return m_hwnd && IsWindow(m_hwnd); }
  // Makes the overlay an owned window of `owner` (nullptr: none). Windows then keeps it above the
  // owner and raises the two together, so activating VLC never leaves it underneath, even for a frame.
  void SetOwner(HWND owner);

  void SetRect(const RECT& r);  // screen coordinates
  void Show(bool show);
  bool Visible() const { return m_visible; }
  HWND Hwnd() const { return m_hwnd; }
  int Width() const { return m_w; }
  int Height() const { return m_h; }

  ID3D11RenderTargetView* BackBuffer();
  void Present();

 private:
  HWND m_hwnd = nullptr;
  ComPtr<ID3D11Device> m_device;
  ComPtr<IDXGISwapChain1> m_swap;
  ComPtr<ID3D11RenderTargetView> m_rtv;
  ComPtr<IDCompositionTarget> m_target;
  ComPtr<IDCompositionVisual> m_visual;
  IDCompositionDevice* m_dcomp = nullptr;
  WindowThread* m_windows = nullptr;
  int m_x = 0, m_y = 0, m_w = 0, m_h = 0;
  bool m_visible = false;
};
