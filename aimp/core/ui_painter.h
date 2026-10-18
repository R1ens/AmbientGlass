#pragma once
// Text, icons, the cover, the spectrum, the waveform and every control, drawn with Direct2D and DirectWrite on
// top of the GPU scene, into the same BGRA texture.

#include <d2d1_1.h>
#include <d3d11.h>
#include <dwrite.h>

#include <cstdint>
#include <map>
#include <string>

#include "d3d_helpers.h"
#include "layout.h"
#include "model.h"
#include "ui_state.h"

namespace glass {

class UiPainter {
 public:
  bool Init(ID3D11Device* device, std::wstring& error);
  // `target` must be a B8G8R8A8 texture with render-target binding; scale = px per layout unit.
  void Paint(ID3D11Texture2D* target, float scale, const Model& m, const Layout& L, const UiState& s);
  // The quick settings panel's contents, after GpuScene::RenderOverlay drew its glass.
  void PaintQuick(ID3D11Texture2D* target, float scale, const Model& m, const Layout& L, const UiState& s);
  // The open menu's items, after GpuScene::RenderOverlay drew its glass at MenuRect.
  void PaintMenu(ID3D11Texture2D* target, float scale, const Model& m, const Layout& L, const UiState& s);
  // Drop the cached target bitmap (call before resizing the swap chain).
  void ReleaseTarget();
  // Right edge of the playlist chooser button in the last Paint (layout units), 0 when there is none.
  float ListTitleRight() const { return m_listTitleR; }

 private:
  bool Begin(ID3D11Texture2D* target, float scale, const Model& m);
  void End();

  struct Font {
    float size;
    DWRITE_FONT_WEIGHT weight;
    bool icon;
    bool operator<(const Font& o) const {
      return size != o.size ? size < o.size : weight != o.weight ? weight < o.weight : icon < o.icon;
    }
  };
  IDWriteTextFormat* Format(float size, DWRITE_FONT_WEIGHT weight = DWRITE_FONT_WEIGHT_NORMAL, bool icon = false);
  void Text(const std::wstring& s, const Rc& r, float size, D2D1_COLOR_F c,
            DWRITE_FONT_WEIGHT w = DWRITE_FONT_WEIGHT_NORMAL,
            DWRITE_TEXT_ALIGNMENT align = DWRITE_TEXT_ALIGNMENT_LEADING, bool wrap = false, float spacing = 0);
  void Icon(wchar_t glyph, const Rc& r, float size, D2D1_COLOR_F c);
  ID2D1SolidColorBrush* Solid(D2D1_COLOR_F c);
  ComPtr<ID2D1LinearGradientBrush> Gradient(const Rc& r, D2D1_COLOR_F a, D2D1_COLOR_F b, bool vertical = false);
  void RoundFill(const Rc& r, float rad, ID2D1Brush* b);
  void RoundStroke(const Rc& r, float rad, D2D1_COLOR_F c, float w = 1);
  void CircleButton(const Rc& r, wchar_t glyph, float iconSize, bool hover, bool active, bool primary);
  ID2D1Bitmap1* CoverBitmap(const Cover* c);

  void TitleBar(const Layout& L, const UiState& s);
  void Sidebar(const Model& m, const Layout& L, const UiState& s);
  void Card(const Model& m, const Layout& L, const UiState& s);
  void WaveBar(const Model& m, const Layout& L, const UiState& s);
  void Spectrum(const Model& m, const Rc& sp);
  void Menu(const Layout& L, const UiState& s);
  void List(const Model& m, const Layout& L, const UiState& s);
  void Bottom(const Model& m, const Layout& L, const UiState& s);
  void Quick(const Model& m, const Layout& L, const UiState& s);

  ComPtr<ID2D1Factory1> m_factory;
  ComPtr<ID2D1Device> m_d2dDevice;
  ComPtr<ID2D1DeviceContext> m_dc;
  ComPtr<IDWriteFactory> m_dw;
  ComPtr<ID2D1SolidColorBrush> m_solid;
  std::map<Font, ComPtr<IDWriteTextFormat>> m_formats;
  std::wstring m_textFamily, m_displayFamily, m_iconFamily;
  ID3D11Texture2D* m_targetTex = nullptr;
  ComPtr<ID2D1Bitmap1> m_target;
  ComPtr<ID2D1Bitmap1> m_cover;
  uint64_t m_coverId = ~0ull;
  D2D1_COLOR_F m_a0{}, m_a1{}, m_a2{}, m_text{}, m_dim{};
  bool m_light = false;
  float m_listTitleR = 0;  // right edge of the playlist chooser button as last drawn, 0 = none
  float m_time = 0;
};

}  // namespace glass
