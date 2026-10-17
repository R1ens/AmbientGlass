#pragma once
#include <windows.h>
#include <d3d11.h>

#include <memory>

// Captures one window through Windows.Graphics.Capture as FP16 linear (scRGB) frames.
// C++/WinRT stays inside capture.cpp.
class WindowCapture {
 public:
  WindowCapture();
  ~WindowCapture();
  WindowCapture(const WindowCapture&) = delete;
  WindowCapture& operator=(const WindowCapture&) = delete;

  // Asks Windows to drop the yellow capture border (Windows 11). Safe to call anywhere.
  static void RequestBorderless();

  bool Start(ID3D11Device* device, HWND hwnd);
  void Stop();
  HWND Target() const;
  HANDLE Event() const;  // signalled when a frame arrives

  // Takes the newest frame. The texture stays valid until the next Acquire() or Stop().
  // False also when the window was just resized: that frame is dropped and the pool resized.
  // `composedQpc` receives when Windows composed that frame, in QueryPerformanceCounter ticks.
  bool Acquire(ID3D11Texture2D** tex, SIZE* contentSize, LONGLONG* composedQpc);

 private:
  struct Impl;
  std::unique_ptr<Impl> m_impl;
};
