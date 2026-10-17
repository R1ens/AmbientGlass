#pragma once
#include <windows.h>
#include <d3d11.h>

#include "capture.h"
#include "renderer.h"
#include "vlc_tracker.h"

// Feeds the renderer with VLC's picture: owns the window capture, cuts the picture out of each
// frame, paces the luma readback and keeps frame statistics.
class FrameSource {
 public:
  bool Start(ID3D11Device* device, HWND hwnd);
  void Stop();
  HWND Target() const { return m_capture.Target(); }
  HANDLE Event() const { return m_capture.Event(); }

  // Takes the newest captured frame into the renderer. True when a new picture was ingested;
  // `stale` is set when a frame had to be skipped because it predates the current window size.
  bool Pump(Renderer& renderer, const VlcState& vlc, ULONGLONG now, bool& stale);

  bool HasFrame() const { return m_hasFrame; }
  ULONGLONG LastFrameTick() const { return m_lastFrameTick; }
  LONGLONG LastComposedQpc() const { return m_composedQpc; }  // when Windows showed the last ingested frame
  float Fps(ULONGLONG now);  // frames per second over the last second

 private:
  static constexpr ULONGLONG kLumaIntervalMs = 100;

  WindowCapture m_capture;
  bool m_hasFrame = false;
  ULONGLONG m_lastFrameTick = 0, m_lastLuma = 0, m_fpsTick = 0;
  LONGLONG m_composedQpc = 0;
  int m_fpsFrames = 0;
  float m_fps = 0;
};
