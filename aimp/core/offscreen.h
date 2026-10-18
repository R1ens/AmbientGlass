#pragma once
// Renders whole frames without a window: for golden tests and for looking at changes. Uses WARP (software,
// identical everywhere) or the hardware device.

#include <cstdint>
#include <memory>
#include <string>
#include <vector>

#include "gpu_scene.h"
#include "model.h"

namespace glass {

class UiPainter;
struct UiState;

class Offscreen {
 public:
  Offscreen();
  ~Offscreen();
  bool Init(bool warp, std::wstring& error);
  // One frame of `m` at w x h px (scale px per layout unit); BGRA8 rows of w * 4 bytes. `state` (hover, an open
  // menu...) defaults to a window nobody touches.
  bool Render(const Model& m, UINT w, UINT h, float scale, const SceneOptions& o, bool ui,
              std::vector<uint8_t>& bgra, const UiState* state = nullptr);
  bool SavePng(const std::wstring& path, UINT w, UINT h, const std::vector<uint8_t>& bgra, std::string& err);
  // Renders `frames` frames of `m` (the background moving as in the window) and reports the average GPU time of
  // each pass and the CPU time of a frame, as text.
  std::wstring Bench(const Model& m, UINT w, UINT h, float scale, int frames);

 private:
  ComPtr<ID3D11Device> m_device;
  ComPtr<ID3D11DeviceContext> m_ctx;
  GpuScene m_scene;
  std::unique_ptr<UiPainter> m_painter;
};

}  // namespace glass
