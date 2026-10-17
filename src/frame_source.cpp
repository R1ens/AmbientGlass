#include "frame_source.h"

#include "layout.h"

bool FrameSource::Start(ID3D11Device* device, HWND hwnd) {
  m_hasFrame = false;
  return m_capture.Start(device, hwnd);
}

void FrameSource::Stop() {
  m_capture.Stop();
  m_hasFrame = false;
}

bool FrameSource::Pump(Renderer& renderer, const VlcState& vlc, ULONGLONG now, bool& stale) {
  stale = false;
  if (!m_capture.Target()) return false;
  ComPtr<ID3D11Texture2D> tex;
  SIZE size{};
  LONGLONG composed = 0;
  if (!m_capture.Acquire(&tex, &size, &composed)) return false;

  D3D11_TEXTURE2D_DESC desc{};
  tex->GetDesc(&desc);
  RECT box;
  if (!CaptureBox(vlc, size, desc.Width, desc.Height, box)) {
    stale = true;
    return false;
  }
  D3D11_BOX b{(UINT)box.left, (UINT)box.top, 0, (UINT)box.right, (UINT)box.bottom, 1};
  bool wantLuma = now - m_lastLuma >= kLumaIntervalMs;
  if (!renderer.Ingest(tex.Get(), b, wantLuma)) return false;
  if (wantLuma) m_lastLuma = now;
  m_hasFrame = true;
  m_lastFrameTick = now;
  m_composedQpc = composed;
  m_fpsFrames++;
  return true;
}

float FrameSource::Fps(ULONGLONG now) {
  if (now - m_fpsTick >= 1000) {
    m_fps = m_fpsTick ? m_fpsFrames * 1000.f / (float)(now - m_fpsTick) : 0.f;
    m_fpsFrames = 0;
    m_fpsTick = now;
  }
  return m_fps;
}
