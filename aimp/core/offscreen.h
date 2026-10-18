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

class Offscreen {
 public:
  Offscreen();
  ~Offscreen();
  bool Init(bool warp, std::wstring& error);
  // One frame of `m` at w x h px (scale px per layout unit); BGRA8 rows of w * 4 bytes.
  bool Render(const Model& m, UINT w, UINT h, float scale, const SceneOptions& o, bool ui,
              std::vector<uint8_t>& bgra);
  bool SavePng(const std::wstring& path, UINT w, UINT h, const std::vector<uint8_t>& bgra, std::string& err);

 private:
  ComPtr<ID3D11Device> m_device;
  ComPtr<ID3D11DeviceContext> m_ctx;
  GpuScene m_scene;
  std::unique_ptr<UiPainter> m_painter;
};

}  // namespace glass
