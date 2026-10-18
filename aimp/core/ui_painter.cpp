#include "ui_painter.h"

#include <dwrite_1.h>
#include <dxgi1_2.h>

#include <algorithm>
#include <cmath>
#include <vector>

#include "log.h"
#include "ui_text.h"
#include "waveform.h"

namespace glass {
namespace {

// Segoe Fluent Icons / Segoe MDL2 Assets code points.
constexpr wchar_t kIcPlay = 0xE768, kIcPause = 0xE769, kIcPrev = 0xE892, kIcNext = 0xE893;
constexpr wchar_t kIcShuffle = 0xE8B1, kIcRepeat = 0xE8EE, kIcRepeatOne = 0xE8ED;
constexpr wchar_t kIcHeart = 0xEB51, kIcHeartFill = 0xEB52, kIcMore = 0xE712, kIcSettings = 0xE713;
constexpr wchar_t kIcSearch = 0xE721, kIcVolume = 0xE767, kIcMute = 0xE74F, kIcFolder = 0xE8B7;
constexpr wchar_t kIcPlaylist = 0xE90B, kIcRecent = 0xE81C, kIcAdd = 0xE710, kIcRefresh = 0xE72C;
constexpr wchar_t kIcMin = 0xE921, kIcMax = 0xE922, kIcRestore = 0xE923, kIcClose = 0xE8BB, kIcMusic = 0xE8D6;
constexpr wchar_t kIcCancel = 0xE711, kIcAudio = 0xE189;

D2D1_COLOR_F C(float r, float g, float b, float a = 1) { return D2D1::ColorF(r, g, b, a); }
D2D1_COLOR_F Hex(uint32_t v, float a = 1) {
  return C(((v >> 16) & 255) / 255.f, ((v >> 8) & 255) / 255.f, (v & 255) / 255.f, a);
}
D2D1_COLOR_F WithA(D2D1_COLOR_F c, float a) { return C(c.r, c.g, c.b, a); }
D2D1_COLOR_F FromRgb(const Rgb& c, float a = 1) { return C(c.r, c.g, c.b, a); }
D2D1_RECT_F Rf(const Rc& r) { return D2D1::RectF(r.l, r.t, r.r, r.b); }
Rc Inset(const Rc& r, float d) { return Rc{r.l + d, r.t + d, r.r - d, r.b - d}; }
Rc Box(float x, float y, float w, float h) { return Rc{x, y, x + w, y + h}; }

bool HasFamily(IDWriteFactory* dw, const wchar_t* name) {
  ComPtr<IDWriteFontCollection> fonts;
  if (FAILED(dw->GetSystemFontCollection(&fonts))) return false;
  UINT32 idx = 0;
  BOOL exists = FALSE;
  return SUCCEEDED(fonts->FindFamilyName(name, &idx, &exists)) && exists;
}

const wchar_t* kNavNames[4] = {L"Плейлисты", L"Папки", L"Недавнее", L"Избранное"};
const wchar_t kNavIcons[4] = {kIcPlaylist, kIcFolder, kIcRecent, kIcHeart};

}  // namespace

bool UiPainter::Init(ID3D11Device* device, std::wstring& error) {
  D2D1_FACTORY_OPTIONS fo{};
  HRESULT hr = D2D1CreateFactory(D2D1_FACTORY_TYPE_SINGLE_THREADED, __uuidof(ID2D1Factory1), &fo, &m_factory);
  ComPtr<IDXGIDevice> dxgi;
  if (SUCCEEDED(hr)) hr = device->QueryInterface(IID_PPV_ARGS(&dxgi));
  if (SUCCEEDED(hr)) hr = m_factory->CreateDevice(dxgi.Get(), &m_d2dDevice);
  if (SUCCEEDED(hr)) hr = m_d2dDevice->CreateDeviceContext(D2D1_DEVICE_CONTEXT_OPTIONS_NONE, &m_dc);
  if (SUCCEEDED(hr))
    hr = DWriteCreateFactory(DWRITE_FACTORY_TYPE_SHARED, __uuidof(IDWriteFactory),
                             reinterpret_cast<IUnknown**>(m_dw.GetAddressOf()));
  if (SUCCEEDED(hr)) hr = m_dc->CreateSolidColorBrush(C(1, 1, 1), &m_solid);
  if (FAILED(hr)) {
    error = L"Direct2D: " + FromUtf8(Log::HrText(hr));
    return false;
  }
  m_textFamily = HasFamily(m_dw.Get(), L"Segoe UI Variable Text") ? L"Segoe UI Variable Text" : L"Segoe UI";
  m_displayFamily = HasFamily(m_dw.Get(), L"Segoe UI Variable Display") ? L"Segoe UI Variable Display" : L"Segoe UI";
  m_iconFamily = HasFamily(m_dw.Get(), L"Segoe Fluent Icons") ? L"Segoe Fluent Icons" : L"Segoe MDL2 Assets";
  m_dc->SetTextAntialiasMode(D2D1_TEXT_ANTIALIAS_MODE_GRAYSCALE);
  return true;
}

void UiPainter::ReleaseTarget() {
  if (m_dc) m_dc->SetTarget(nullptr);
  m_target.Reset();
  m_targetTex = nullptr;
}

IDWriteTextFormat* UiPainter::Format(float size, DWRITE_FONT_WEIGHT weight, bool icon) {
  Font key{size, weight, icon};
  auto it = m_formats.find(key);
  if (it != m_formats.end()) return it->second.Get();
  const std::wstring& family = icon ? m_iconFamily : size >= 20 ? m_displayFamily : m_textFamily;
  ComPtr<IDWriteTextFormat> f;
  if (FAILED(m_dw->CreateTextFormat(family.c_str(), nullptr, weight, DWRITE_FONT_STYLE_NORMAL,
                                    DWRITE_FONT_STRETCH_NORMAL, size, L"ru-ru", &f)))
    return nullptr;
  f->SetParagraphAlignment(DWRITE_PARAGRAPH_ALIGNMENT_CENTER);
  m_formats[key] = f;
  return f.Get();
}

ID2D1SolidColorBrush* UiPainter::Solid(D2D1_COLOR_F c) {
  m_solid->SetColor(c);
  return m_solid.Get();
}

ComPtr<ID2D1LinearGradientBrush> UiPainter::Gradient(const Rc& r, D2D1_COLOR_F a, D2D1_COLOR_F b, bool vertical) {
  D2D1_GRADIENT_STOP stops[2] = {{0.f, a}, {1.f, b}};
  ComPtr<ID2D1GradientStopCollection> coll;
  ComPtr<ID2D1LinearGradientBrush> br;
  if (SUCCEEDED(m_dc->CreateGradientStopCollection(stops, 2, &coll)))
    m_dc->CreateLinearGradientBrush(
        D2D1::LinearGradientBrushProperties(D2D1::Point2F(r.l, r.t), vertical ? D2D1::Point2F(r.l, r.b)
                                                                               : D2D1::Point2F(r.r, r.t)),
        coll.Get(), &br);
  return br;
}

void UiPainter::RoundFill(const Rc& r, float rad, ID2D1Brush* b) {
  if (b && r.Valid()) m_dc->FillRoundedRectangle(D2D1::RoundedRect(Rf(r), rad, rad), b);
}

void UiPainter::RoundStroke(const Rc& r, float rad, D2D1_COLOR_F c, float w) {
  if (r.Valid()) m_dc->DrawRoundedRectangle(D2D1::RoundedRect(Rf(Inset(r, w / 2)), rad, rad), Solid(c), w);
}

void UiPainter::Text(const std::wstring& s, const Rc& r, float size, D2D1_COLOR_F c, DWRITE_FONT_WEIGHT w,
                     DWRITE_TEXT_ALIGNMENT align, bool wrap, float spacing) {
  if (s.empty() || !r.Valid()) return;
  IDWriteTextFormat* f = Format(size, w);
  if (!f) return;
  ComPtr<IDWriteTextLayout> lay;
  if (FAILED(m_dw->CreateTextLayout(s.c_str(), (UINT32)s.size(), f, r.W(), r.H(), &lay))) return;
  lay->SetTextAlignment(align);
  lay->SetWordWrapping(wrap ? DWRITE_WORD_WRAPPING_WRAP : DWRITE_WORD_WRAPPING_NO_WRAP);
  DWRITE_TRIMMING trim{DWRITE_TRIMMING_GRANULARITY_CHARACTER, 0, 0};
  ComPtr<IDWriteInlineObject> ellipsis;
  if (SUCCEEDED(m_dw->CreateEllipsisTrimmingSign(f, &ellipsis))) lay->SetTrimming(&trim, ellipsis.Get());
  if (spacing != 0) {
    ComPtr<IDWriteTextLayout1> l1;
    if (SUCCEEDED(lay.As(&l1))) l1->SetCharacterSpacing(0, spacing, 0, DWRITE_TEXT_RANGE{0, (UINT32)s.size()});
  }
  m_dc->DrawTextLayout(D2D1::Point2F(r.l, r.t), lay.Get(), Solid(c), D2D1_DRAW_TEXT_OPTIONS_CLIP);
}

void UiPainter::Icon(wchar_t glyph, const Rc& r, float size, D2D1_COLOR_F c) {
  IDWriteTextFormat* f = Format(size, DWRITE_FONT_WEIGHT_NORMAL, true);
  if (!f) return;
  f->SetTextAlignment(DWRITE_TEXT_ALIGNMENT_CENTER);
  m_dc->DrawText(&glyph, 1, f, Rf(r), Solid(c));
}

void UiPainter::CircleButton(const Rc& r, wchar_t glyph, float iconSize, bool hover, bool active, bool primary) {
  D2D1_ELLIPSE e = D2D1::Ellipse(D2D1::Point2F(r.Cx(), r.Cy()), r.W() / 2, r.H() / 2);
  if (primary) {
    auto g = Gradient(r, m_a0, m_a1);
    if (g) m_dc->FillEllipse(e, g.Get());
    if (hover) m_dc->FillEllipse(e, Solid(C(1, 1, 1, 0.12f)));
    m_dc->DrawEllipse(D2D1::Ellipse(e.point, e.radiusX - 0.5f, e.radiusY - 0.5f), Solid(C(1, 1, 1, 0.25f)), 1);
    Icon(glyph, r, iconSize, C(1, 1, 1));
    return;
  }
  D2D1_COLOR_F fill = active ? WithA(m_a0, 0.30f) : C(1, 1, 1, hover ? 0.10f : 0.05f);
  if (m_light) fill = active ? WithA(m_a0, 0.25f) : C(0, 0, 0, hover ? 0.08f : 0.04f);
  m_dc->FillEllipse(e, Solid(fill));
  D2D1_COLOR_F border = active ? WithA(m_a0, 0.70f) : m_light ? C(0, 0, 0, 0.12f) : C(1, 1, 1, 0.14f);
  m_dc->DrawEllipse(D2D1::Ellipse(e.point, e.radiusX - 0.5f, e.radiusY - 0.5f), Solid(border), 1);
  Icon(glyph, r, iconSize, active ? C(1, 1, 1) : m_text);
}

ID2D1Bitmap1* UiPainter::CoverBitmap(const Cover* c) {
  uint64_t id = c && !c->Empty() ? c->id : 0;
  if (id == m_coverId) return m_cover.Get();
  m_coverId = id;
  m_cover.Reset();
  if (!id) return nullptr;
  // RGBA (straight) to BGRA premultiplied; covers are opaque in practice, alpha is kept anyway.
  std::vector<uint8_t> bgra(c->rgba.size());
  for (size_t i = 0; i + 3 < c->rgba.size(); i += 4) {
    unsigned a = c->rgba[i + 3];
    bgra[i] = (uint8_t)(c->rgba[i + 2] * a / 255);
    bgra[i + 1] = (uint8_t)(c->rgba[i + 1] * a / 255);
    bgra[i + 2] = (uint8_t)(c->rgba[i] * a / 255);
    bgra[i + 3] = (uint8_t)a;
  }
  D2D1_BITMAP_PROPERTIES1 bp = D2D1::BitmapProperties1(
      D2D1_BITMAP_OPTIONS_NONE, D2D1::PixelFormat(DXGI_FORMAT_B8G8R8A8_UNORM, D2D1_ALPHA_MODE_PREMULTIPLIED));
  m_dc->CreateBitmap(D2D1::SizeU(c->w, c->h), bgra.data(), c->w * 4, bp, &m_cover);
  return m_cover.Get();
}

bool UiPainter::Begin(ID3D11Texture2D* target, float scale, const Model& m) {
  if (target != m_targetTex || !m_target) {
    m_dc->SetTarget(nullptr);
    m_target.Reset();
    ComPtr<IDXGISurface> surf;
    if (FAILED(target->QueryInterface(IID_PPV_ARGS(&surf)))) return false;
    D2D1_BITMAP_PROPERTIES1 bp = D2D1::BitmapProperties1(
        D2D1_BITMAP_OPTIONS_TARGET | D2D1_BITMAP_OPTIONS_CANNOT_DRAW,
        D2D1::PixelFormat(DXGI_FORMAT_B8G8R8A8_UNORM, D2D1_ALPHA_MODE_PREMULTIPLIED));
    if (FAILED(m_dc->CreateBitmapFromDxgiSurface(surf.Get(), &bp, &m_target))) return false;
    m_targetTex = target;
  }
  m_dc->SetTarget(m_target.Get());
  m_dc->SetDpi(96.f * scale, 96.f * scale);  // draw in layout units

  m_light = m.theme == Theme::Light;
  m_a0 = FromRgb(m.palette.accent[0]);
  m_a1 = FromRgb(m.palette.accent[1]);
  m_a2 = FromRgb(m.palette.accent[2]);
  m_text = m_light ? Hex(0x1A1A22) : Hex(0xF2F2F5);
  m_dim = m_light ? Hex(0x1A1A22, 0.55f) : Hex(0xF2F2F5, 0.55f);
  m_time = m.time;

  m_dc->BeginDraw();
  m_dc->SetTransform(D2D1::Matrix3x2F::Identity());
  return true;
}

void UiPainter::End() {
  HRESULT hr = m_dc->EndDraw();
  if (FAILED(hr)) {
    Log::Warn("d2d: EndDraw %s", Log::HrText(hr).c_str());
    ReleaseTarget();
  }
}

void UiPainter::Paint(ID3D11Texture2D* target, float scale, const Model& m, const Layout& L, const UiState& s) {
  if (!Begin(target, scale, m)) return;
  TitleBar(L, s);
  Sidebar(m, L, s);
  Card(m, L, s);
  List(m, L, s);
  Bottom(m, L, s);
  End();
}

void UiPainter::PaintQuick(ID3D11Texture2D* target, float scale, const Model& m, const Layout& L, const UiState& s) {
  if (!Begin(target, scale, m)) return;
  Quick(m, L, s);
  End();
}

// ---------------------------------------------------------------------------------------------------------

void UiPainter::TitleBar(const Layout& L, const UiState& s) {
  // A thin glass strip with the app name; window buttons on the right.
  m_dc->FillRectangle(Rf(L.titlebar), Solid(m_light ? C(1, 1, 1, 0.35f) : C(1, 1, 1, 0.035f)));
  m_dc->FillRectangle(D2D1::RectF(0, L.titlebar.b - 1, L.w, L.titlebar.b), Solid(m_light ? C(0, 0, 0, 0.08f) : C(1, 1, 1, 0.08f)));
  Rc logo = Box(14, L.titlebar.Cy() - 9, 18, 18);
  auto g = Gradient(logo, m_a0, m_a1);
  RoundFill(logo, 5, g.Get());
  Icon(kIcAudio, logo, 11, C(1, 1, 1));
  Text(L"AIMP  GLASS", Rc{logo.r + 10, L.titlebar.t, 300, L.titlebar.b}, 11, WithA(m_text, 0.75f),
       DWRITE_FONT_WEIGHT_SEMI_BOLD, DWRITE_TEXT_ALIGNMENT_LEADING, false, 0.9f);
  struct B {
    const Rc& r;
    wchar_t g;
    Hit h;
  } bs[] = {{L.btnMin, kIcMin, Hit::Min}, {L.btnMax, s.maximized ? kIcRestore : kIcMax, Hit::Max},
            {L.btnClose, kIcClose, Hit::Close}};
  for (const B& b : bs) {
    bool hov = s.hover.hit == b.h;
    if (hov) m_dc->FillRectangle(Rf(b.r), Solid(b.h == Hit::Close ? Hex(0xE81123) : C(1, 1, 1, m_light ? 0.4f : 0.08f)));
    Icon(b.g, b.r, 10, hov && b.h == Hit::Close ? C(1, 1, 1) : WithA(m_text, 0.8f));
  }
}

void UiPainter::Sidebar(const Model& m, const Layout& L, const UiState& s) {
  const bool compact = L.compactSidebar;
  if (!compact)
    Text(L"МОЯ МУЗЫКА", Rc{L.sidebar.l + 18, L.sidebar.t + 14, L.sidebar.r - 10, L.sidebar.t + 34}, 10, m_dim,
         DWRITE_FONT_WEIGHT_SEMI_BOLD, DWRITE_TEXT_ALIGNMENT_LEADING, false, 1.2f);
  for (int i = 0; i < 4; i++) {
    const Rc& r = L.nav[i];
    bool active = (int)m.nav == i, hov = s.hover.hit == (Hit)((int)Hit::Nav0 + i);
    if (active) {
      auto g = Gradient(r, WithA(m_a0, 0.28f), WithA(m_a1, 0.10f));
      RoundFill(r, 12, g.Get());
      RoundStroke(r, 12, WithA(m_a0, 0.30f));
    } else if (hov) {
      RoundFill(r, 12, Solid(m_light ? C(0, 0, 0, 0.05f) : C(1, 1, 1, 0.06f)));
    }
    Rc ic = compact ? Box(r.Cx() - 13, r.Cy() - 13, 26, 26) : Box(r.l + 10, r.Cy() - 13, 26, 26);
    RoundFill(ic, 8, active ? Solid(m_a0) : Solid(m_light ? C(0, 0, 0, 0.06f) : C(1, 1, 1, 0.06f)));
    Icon(kNavIcons[i], ic, 13, active ? C(1, 1, 1) : m_text);
    if (compact) continue;
    Text(kNavNames[i], Rc{ic.r + 10, r.t, r.r - 44, r.b}, 13, active ? m_text : m_dim,
         active ? DWRITE_FONT_WEIGHT_SEMI_BOLD : DWRITE_FONT_WEIGHT_MEDIUM);
    if (m.navCount[i] > 0) {
      std::wstring n = std::to_wstring(m.navCount[i]);
      float pw = 14 + 7.f * n.size();
      Rc pill = Box(r.r - 10 - pw, r.Cy() - 10, pw, 20);
      RoundFill(pill, 10, Solid(active ? C(1, 1, 1, 0.18f) : m_light ? C(0, 0, 0, 0.06f) : C(1, 1, 1, 0.08f)));
      Text(n, pill, 11, active ? m_text : m_dim, DWRITE_FONT_WEIGHT_NORMAL, DWRITE_TEXT_ALIGNMENT_CENTER);
    }
  }

  // Add a folder / scan: a dashed box with a gradient primary button.
  if (!compact) {
    ComPtr<ID2D1StrokeStyle> dash;
    float dashes[] = {4, 3};
    m_factory->CreateStrokeStyle(D2D1::StrokeStyleProperties(D2D1_CAP_STYLE_FLAT, D2D1_CAP_STYLE_FLAT,
                                                             D2D1_CAP_STYLE_FLAT, D2D1_LINE_JOIN_ROUND, 10,
                                                             D2D1_DASH_STYLE_CUSTOM, 0),
                                 dashes, 2, &dash);
    RoundFill(L.addBox, 14, Solid(m_light ? C(0, 0, 0, 0.03f) : C(1, 1, 1, 0.04f)));
    m_dc->DrawRoundedRectangle(D2D1::RoundedRect(Rf(Inset(L.addBox, 0.5f)), 14, 14),
                               Solid(m_light ? C(0, 0, 0, 0.15f) : C(1, 1, 1, 0.12f)), 1, dash.Get());
    auto g = Gradient(L.btnAddFolder, m_a0, m_a1);
    RoundFill(L.btnAddFolder, 11, g.Get());
    if (s.hover.hit == Hit::AddFolder) RoundFill(L.btnAddFolder, 11, Solid(C(1, 1, 1, 0.12f)));
    Rc a = L.btnAddFolder;
    Icon(kIcAdd, Box(a.Cx() - 70, a.t, 20, a.H()), 12, C(1, 1, 1));
    Text(L"Добавить папку", Rc{a.Cx() - 48, a.t, a.r - 6, a.b}, 12.5f, C(1, 1, 1), DWRITE_FONT_WEIGHT_SEMI_BOLD);
    Rc sc = L.btnScan;
    RoundFill(sc, 11, Solid(s.hover.hit == Hit::Scan ? C(1, 1, 1, 0.10f) : C(1, 1, 1, 0.05f)));
    RoundStroke(sc, 11, m_light ? C(0, 0, 0, 0.12f) : C(1, 1, 1, 0.14f));
    Icon(kIcRefresh, Box(sc.Cx() - 56, sc.t, 18, sc.H()), 11, m_text);
    Text(L"Сканировать", Rc{sc.Cx() - 34, sc.t, sc.r - 6, sc.b}, 12, m_text, DWRITE_FONT_WEIGHT_MEDIUM);
  } else {
    auto g = Gradient(L.btnAddFolder, m_a0, m_a1);
    RoundFill(L.btnAddFolder, 12, g.Get());
    Icon(kIcAdd, L.btnAddFolder, 13, C(1, 1, 1));
  }

  const Rc& st = L.navSettings;
  RoundFill(st, 12, Solid(s.hover.hit == Hit::NavSettings ? C(1, 1, 1, 0.09f) : C(1, 1, 1, m_light ? 0.3f : 0.05f)));
  Rc ic = compact ? Box(st.Cx() - 13, st.Cy() - 13, 26, 26) : Box(st.l + 10, st.Cy() - 13, 26, 26);
  Icon(kIcSettings, ic, 14, m_text);
  if (!compact) Text(L"Быстрые настройки", Rc{ic.r + 10, st.t, st.r - 8, st.b}, 13, m_text, DWRITE_FONT_WEIGHT_MEDIUM);
}

void UiPainter::Card(const Model& m, const Layout& L, const UiState& s) {
  // Cover: rounded, with a soft shine; it breathes slightly on the beat.
  const float k = 1 + 0.012f * m.pulse;
  Rc cv = L.cover;
  float dx = cv.W() * (k - 1) / 2;
  cv = Rc{cv.l - dx, cv.t - dx, cv.r + dx, cv.b + dx};
  ComPtr<ID2D1RoundedRectangleGeometry> clip;
  m_factory->CreateRoundedRectangleGeometry(D2D1::RoundedRect(Rf(cv), 24, 24), &clip);
  if (ID2D1Bitmap1* bmp = CoverBitmap(m.cover.get())) {
    m_dc->PushLayer(D2D1::LayerParameters1(D2D1::InfiniteRect(), clip.Get()), nullptr);
    D2D1_SIZE_F bs = bmp->GetSize();
    // Cover-fit: crop the longer side.
    float side = std::min(bs.width, bs.height);
    D2D1_RECT_F src = D2D1::RectF((bs.width - side) / 2, (bs.height - side) / 2, (bs.width + side) / 2,
                                  (bs.height + side) / 2);
    m_dc->DrawBitmap(bmp, Rf(cv), 1, D2D1_INTERPOLATION_MODE_HIGH_QUALITY_CUBIC, &src);
    // A soft sheen from the top that fades out smoothly by the middle (no visible edge).
    auto shine = Gradient(Rc{cv.l, cv.t, cv.r, cv.t + cv.H() * 0.6f}, C(1, 1, 1, 0.12f), C(1, 1, 1, 0), true);
    m_dc->FillRectangle(Rf(cv), shine.Get());
    m_dc->PopLayer();
  } else {
    auto g = Gradient(cv, WithA(m_a0, 0.85f), WithA(m_a2, 0.65f));
    m_dc->FillGeometry(clip.Get(), g.Get());
    Icon(kIcMusic, cv, cv.W() * 0.28f, C(1, 1, 1, 0.75f));
  }
  m_dc->DrawGeometry(clip.Get(), Solid(C(1, 1, 1, 0.12f)), 1);
  if (s.hover.hit == Hit::Cover) m_dc->FillGeometry(clip.Get(), Solid(C(1, 1, 1, 0.05f)));

  // Spectrum: light bars under the cover, the real spectrum of the music.
  if (m.settings.spectrum) {
    const Rc& sp = L.spectrum;
    const int n = SpectrumBars::kCount;
    const float gap = 3, bw = std::min(10.f, (sp.W() - 16 - gap * (n - 1)) / n);
    const float total = n * bw + (n - 1) * gap, x0 = sp.Cx() - total / 2;
    const bool playing = m.state == PlayState::Playing;
    auto g = Gradient(sp, m_a0, m_a1, true);
    for (int i = 0; i < n; i++) {
      float lv = playing ? m.spectrum.level[i] : 0.12f;
      float h = std::max(6.f, lv * sp.H());
      Rc b = Box(x0 + i * (bw + gap), sp.b - h, bw, h);
      g->SetOpacity(playing ? 0.95f : 0.4f);
      RoundFill(b, std::min(4.f, bw / 2), g.Get());
    }
  }

  // Badge, title, artist.
  if (L.badge.Valid()) {
    const wchar_t* label = m.state == PlayState::Playing ? L"СЕЙЧАС ИГРАЕТ" : m.state == PlayState::Paused ? L"ПАУЗА"
                                                                                                         : L"ОСТАНОВЛЕНО";
    Rc b = L.badge;
    b.r = b.l + 30 + 8.2f * (float)wcslen(label);
    RoundFill(b, 12, Solid(WithA(m_a0, 0.16f)));
    RoundStroke(b, 12, WithA(m_a0, 0.30f));
    float blink = m.state == PlayState::Playing ? 0.6f + 0.4f * std::cos(m_time * 5.2f) : 0.5f;
    m_dc->FillEllipse(D2D1::Ellipse(D2D1::Point2F(b.l + 13, b.Cy()), 6, 6), Solid(WithA(m_a0, 0.35f * blink)));
    m_dc->FillEllipse(D2D1::Ellipse(D2D1::Point2F(b.l + 13, b.Cy()), 3, 3), Solid(C(1, 1, 1, 0.5f + 0.5f * blink)));
    Text(label, Rc{b.l + 22, b.t, b.r, b.b}, 11, m_text, DWRITE_FONT_WEIGHT_SEMI_BOLD,
         DWRITE_TEXT_ALIGNMENT_LEADING, false, 0.8f);
  }
  std::wstring title = m.track.title.empty() ? L"Ничего не играет" : m.track.title;
  bool twoLines = L.title.H() > 50;
  Text(title, L.title, 32, m_text, DWRITE_FONT_WEIGHT_EXTRA_BOLD, DWRITE_TEXT_ALIGNMENT_LEADING, twoLines, -0.8f);
  std::wstring artist = ArtistLine(m.track, L" • ");
  if (artist.empty() && m.track.title.empty()) artist = L"Выберите трек в плейлисте";
  Text(artist, L.artist, 15, m_dim, DWRITE_FONT_WEIGHT_MEDIUM);

  // Times above the waveform.
  double pos = s.draggingWave ? s.dragValue * m.track.duration : m.position;
  Text(FormatTime(pos), L.waveTimes, 11, m_dim);
  Text(FormatTime(m.track.duration), L.waveTimes, 11, m_dim, DWRITE_FONT_WEIGHT_NORMAL, DWRITE_TEXT_ALIGNMENT_TRAILING);
  WaveBar(m, L, s);

  // Transport.
  auto hov = [&](Hit h) { return s.hover.hit == h; };
  CircleButton(L.btnShuffle, kIcShuffle, 14, hov(Hit::Shuffle), m.shuffle, false);
  CircleButton(L.btnPrev, kIcPrev, 14, hov(Hit::Prev), false, false);
  CircleButton(L.btnPlay, m.state == PlayState::Playing ? kIcPause : kIcPlay, 18, hov(Hit::Play), false, true);
  CircleButton(L.btnNext, kIcNext, 14, hov(Hit::Next), false, false);
  CircleButton(L.btnRepeat, m.repeat == Repeat::One ? kIcRepeatOne : kIcRepeat, 14, hov(Hit::Repeat),
               m.repeat != Repeat::Off, false);

  // Like: a heart that flashes when set.
  const Rc& lk = L.btnLike;
  bool liked = m.track.favorite;
  RoundFill(lk, 11, Solid(liked ? Hex(0xFF5A7A, 0.14f) : C(1, 1, 1, hov(Hit::Like) ? 0.10f : 0.04f)));
  RoundStroke(lk, 11, liked ? Hex(0xFF5A7A, 0.30f) : m_light ? C(0, 0, 0, 0.12f) : C(1, 1, 1, 0.14f));
  if (s.likeFlash > 0)
    m_dc->FillEllipse(D2D1::Ellipse(D2D1::Point2F(lk.Cx(), lk.Cy()), 19 + 26 * (1 - s.likeFlash), 19 + 26 * (1 - s.likeFlash)),
                      Solid(Hex(0xFF5A7A, 0.35f * s.likeFlash)));
  Icon(liked ? kIcHeartFill : kIcHeart, lk, 15, liked ? Hex(0xFF5A7A) : m_dim);
  const Rc& mo = L.btnMore;
  RoundFill(mo, 11, Solid(C(1, 1, 1, hov(Hit::More) ? 0.10f : 0.04f)));
  RoundStroke(mo, 11, m_light ? C(0, 0, 0, 0.12f) : C(1, 1, 1, 0.14f));
  Icon(kIcMore, mo, 15, m_dim);
}

void UiPainter::WaveBar(const Model& m, const Layout& L, const UiState& s) {
  const Rc& w = L.wave;
  RoundFill(w, 14, Solid(m_light ? C(0, 0, 0, 0.04f) : C(1, 1, 1, 0.04f)));
  RoundStroke(w, 14, m_light ? C(0, 0, 0, 0.08f) : C(1, 1, 1, 0.08f));
  Rc in = Rc{w.l + 14, w.t + 9, w.r - 14, w.b - 9};
  double frac = m.track.duration > 0 ? std::clamp(m.position / m.track.duration, 0.0, 1.0) : 0.0;
  if (s.draggingWave) frac = s.dragValue;
  const float px = in.l + (float)frac * in.W();
  const int bars = std::max(8, (int)(in.W() / 5.f));
  std::vector<WaveColumn> cols;
  float maxRms = 1e-4f;
  if (!m.wave.columns.empty()) {
    cols = ResampleWave(m.wave.columns, bars);
    for (const WaveColumn& c : cols) maxRms = std::max(maxRms, c.rms);
  }
  auto played = Gradient(in, m_a0, m_a1);
  const float bw = in.W() / bars;
  for (int i = 0; i < bars; i++) {
    float x = in.l + i * bw;
    float v;
    if (!cols.empty()) {
      // Loudness relative to the loudest part of this track, so quiet and loud recordings both show their
      // shape; a floor keeps silence visible as a thin line.
      v = 0.10f + 0.90f * std::pow(std::clamp(cols[i].rms / maxRms, 0.f, 1.f), 0.85f);
    } else {
      // Not built yet: a calm placeholder that fills in as the waveform builds.
      v = 0.18f + 0.08f * std::sin(i * 0.45f + m_time * 1.5f);
      if ((float)i / bars < m.wave.progress) v += 0.12f;
    }
    float h = std::max(2.f, v * in.H());
    Rc b = Rc{x + bw * 0.22f, in.Cy() - h / 2, x + bw * 0.78f, in.Cy() + h / 2};
    bool done = x + bw * 0.5f <= px;
    RoundFill(b, std::min(1.5f, b.W() / 2), done ? (ID2D1Brush*)played.Get()
                                                  : Solid(m_light ? C(0, 0, 0, 0.18f) : C(1, 1, 1, 0.22f)));
  }
  // The knob with a soft halo.
  bool hot = s.hover.hit == Hit::Wave || s.draggingWave;
  m_dc->FillEllipse(D2D1::Ellipse(D2D1::Point2F(px, in.Cy()), hot ? 13.f : 11.f, hot ? 13.f : 11.f), Solid(WithA(m_a0, 0.25f)));
  m_dc->FillEllipse(D2D1::Ellipse(D2D1::Point2F(px, in.Cy()), 7, 7), Solid(C(1, 1, 1)));
  m_dc->DrawEllipse(D2D1::Ellipse(D2D1::Point2F(px, in.Cy()), 7, 7), Solid(WithA(m_a0, 0.9f)), 2);
}

void UiPainter::List(const Model& m, const Layout& L, const UiState& s) {
  const Rc& h = L.listHead;
  std::wstring title = m.listTitle.empty() ? L"ПЛЕЙЛИСТ" : m.listTitle;
  Rc tr = Rc{h.l + 4, h.t, L.search.l - 12, h.b};
  Text(title, tr, 12, m_text, DWRITE_FONT_WEIGHT_BOLD, DWRITE_TEXT_ALIGNMENT_LEADING, false, 0.5f);
  // Summary after the title: measure the title to place it.
  if (IDWriteTextFormat* f = Format(12, DWRITE_FONT_WEIGHT_BOLD)) {
    ComPtr<IDWriteTextLayout> lay;
    if (SUCCEEDED(m_dw->CreateTextLayout(title.c_str(), (UINT32)title.size(), f, tr.W(), tr.H(), &lay))) {
      DWRITE_TEXT_METRICS tm{};
      lay->GetMetrics(&tm);
      Text(ListSummary(m), Rc{tr.l + tm.width + 10, h.t, tr.r, h.b}, 11, m_dim);
    }
  }

  // Search box.
  const Rc& sb = L.search;
  bool focus = s.searchFocus;
  RoundFill(sb, 10, Solid(m_light ? C(0, 0, 0, 0.05f) : C(1, 1, 1, focus ? 0.10f : 0.06f)));
  RoundStroke(sb, 10, focus ? WithA(m_a0, 0.6f) : m_light ? C(0, 0, 0, 0.12f) : C(1, 1, 1, 0.14f));
  Icon(kIcSearch, Box(sb.l + 6, sb.t, 20, sb.H()), 11, m_dim);
  Rc tx = Rc{sb.l + 28, sb.t, sb.r - (m.query.empty() ? 8 : 28), sb.b};
  if (m.query.empty() && !focus)
    Text(L"Поиск по плейлисту…", tx, 12, m_dim);
  else
    Text(m.query, tx, 12, m_text);
  if (!m.query.empty()) Icon(kIcCancel, Box(sb.r - 26, sb.t, 20, sb.H()), 9, m_dim);
  if (focus && std::fmod(m_time, 1.0f) < 0.6f) {
    float cx = tx.l;
    if (!m.query.empty())
      if (IDWriteTextFormat* f = Format(12)) {
        ComPtr<IDWriteTextLayout> lay;
        if (SUCCEEDED(m_dw->CreateTextLayout(m.query.c_str(), (UINT32)m.query.size(), f, 4000, tx.H(), &lay))) {
          FLOAT x = 0, y = 0;
          DWRITE_HIT_TEST_METRICS hm{};
          lay->HitTestTextPosition((UINT32)std::min(s.caret, m.query.size()), FALSE, &x, &y, &hm);
          cx = tx.l + x;
        }
      }
    m_dc->FillRectangle(D2D1::RectF(cx, tx.Cy() - 8, cx + 1.2f, tx.Cy() + 8), Solid(m_text));
  }

  // Column headings.
  const float numW = L.colNum, albW = L.colAlbum, durW = L.colDur, gap = L.colGap;
  if (L.tableHead.Valid()) {
    const Rc& th = L.tableHead;
    D2D1_COLOR_F c = m_dim;
    Text(L"#", Rc{th.l + 8, th.t, th.l + 8 + numW, th.b}, 10, c, DWRITE_FONT_WEIGHT_SEMI_BOLD, DWRITE_TEXT_ALIGNMENT_CENTER);
    Text(L"НАЗВАНИЕ", Rc{th.l + 8 + numW + gap, th.t, th.r - 8 - durW - gap - albW - gap, th.b}, 10, c,
         DWRITE_FONT_WEIGHT_SEMI_BOLD, DWRITE_TEXT_ALIGNMENT_LEADING, false, 0.6f);
    Text(L"АЛЬБОМ", Rc{th.r - 8 - durW - gap - albW, th.t, th.r - 8 - durW - gap, th.b}, 10, c,
         DWRITE_FONT_WEIGHT_SEMI_BOLD, DWRITE_TEXT_ALIGNMENT_LEADING, false, 0.6f);
    Text(L"ДЛИТ.", Rc{th.r - 8 - durW, th.t, th.r - 8, th.b}, 10, c, DWRITE_FONT_WEIGHT_SEMI_BOLD,
         DWRITE_TEXT_ALIGNMENT_TRAILING, false, 0.6f);
  }

  // Rows: only the visible ones are laid out.
  const Rc& rows = L.rows;
  const int n = (int)m.visible.size();
  if (n == 0) {
    const wchar_t* empty = !m.query.empty() ? L"Ничего не найдено" : m.nav == Nav::Favorites ? L"Отмечайте треки сердцем — они появятся здесь"
                         : m.nav == Nav::Folders ? L"Добавьте папку с музыкой"
                                                 : L"Здесь пока пусто";
    Text(empty, rows, 13, m_dim, DWRITE_FONT_WEIGHT_NORMAL, DWRITE_TEXT_ALIGNMENT_CENTER);
    return;
  }
  m_dc->PushAxisAlignedClip(Rf(rows), D2D1_ANTIALIAS_MODE_ALIASED);
  const float rh = L.rowHeight;
  int first = std::max(0, (int)std::floor(s.listScroll / rh));
  int last = std::min(n - 1, (int)std::ceil((s.listScroll + rows.H()) / rh));
  const bool wide = rows.W() > 520;
  for (int vi = first; vi <= last; vi++) {
    const ListRow& r = m.rows[m.visible[vi]];
    float y = rows.t + vi * rh - s.listScroll;
    Rc rr = Rc{rows.l, y + 2, rows.r - 8, y + rh - 2};
    bool hov = s.hover.hit == Hit::Row && s.hover.index == vi;
    if (r.playing) {
      RoundFill(rr, 10, Solid(WithA(m_a0, 0.14f)));
      RoundStroke(rr, 10, WithA(m_a0, 0.18f));
    } else if (hov) {
      RoundFill(rr, 10, Solid(m_light ? C(0, 0, 0, 0.05f) : C(1, 1, 1, 0.06f)));
    }
    Rc num = Rc{rr.l + 8, rr.t, rr.l + 8 + numW, rr.b};
    if (r.playing && m.state != PlayState::Stopped)
      Icon(m.state == PlayState::Playing ? kIcPlay : kIcPause, num, 10, m_a0);
    else if (r.folder)
      Icon(kIcFolder, num, 12, m_dim);
    else
      Text(vi < 99 ? (vi < 9 ? L"0" : L"") + std::to_wstring(vi + 1) : std::to_wstring(vi + 1), num, 11, m_dim,
           DWRITE_FONT_WEIGHT_NORMAL, DWRITE_TEXT_ALIGNMENT_CENTER);
    float nameR = rr.r - 8 - durW - gap - (wide ? albW + gap : 0);
    Rc name = Rc{num.r + gap, rr.t + 3, nameR, rr.Cy() + 1};
    Rc art = Rc{num.r + gap, rr.Cy() + 1, nameR, rr.b - 3};
    if (r.artist.empty()) name = Rc{num.r + gap, rr.t, nameR, rr.b};
    Text(r.title, name, 12.5f, m_text, DWRITE_FONT_WEIGHT_MEDIUM);
    Text(r.artist, art, 11, m_dim);
    if (wide) Text(r.album, Rc{nameR + gap, rr.t, nameR + gap + albW, rr.b}, 12, m_dim);
    Rc dur = Rc{rr.r - 8 - durW, rr.t, rr.r - 8, rr.b};
    if (r.duration > 0) Text(FormatTime(r.duration), dur, 11, m_dim, DWRITE_FONT_WEIGHT_NORMAL, DWRITE_TEXT_ALIGNMENT_TRAILING);
    if (r.playing && m.state == PlayState::Playing) {
      // Three small level bars next to the duration of the playing row.
      for (int k = 0; k < 3; k++) {
        float lv = 0.35f + 0.65f * (0.5f + 0.5f * std::sin(m_time * 9.0f + k * 1.9f));
        float bh = 4 + 6 * lv;
        m_dc->FillRectangle(D2D1::RectF(dur.l - 16 + k * 4, rr.Cy() + 5 - bh, dur.l - 14 + k * 4, rr.Cy() + 5), Solid(m_a0));
      }
    }
  }
  m_dc->PopAxisAlignedClip();

  // Thin scrollbar.
  float total = n * rh;
  if (total > rows.H()) {
    float th = std::max(24.f, rows.H() * rows.H() / total);
    float ty = rows.t + (rows.H() - th) * (s.listScroll / (total - rows.H()));
    RoundFill(Rc{rows.r - 4, ty, rows.r - 1, ty + th}, 2, Solid(m_light ? C(0, 0, 0, 0.18f) : C(1, 1, 1, 0.16f)));
  }
}

void UiPainter::Bottom(const Model& m, const Layout& L, const UiState& s) {
  ComPtr<ID2D1RoundedRectangleGeometry> clip;
  m_factory->CreateRoundedRectangleGeometry(D2D1::RoundedRect(Rf(L.miniCover), 10, 10), &clip);
  if (ID2D1Bitmap1* bmp = CoverBitmap(m.cover.get())) {
    m_dc->PushLayer(D2D1::LayerParameters1(D2D1::InfiniteRect(), clip.Get()), nullptr);
    D2D1_SIZE_F bs = bmp->GetSize();
    float side = std::min(bs.width, bs.height);
    D2D1_RECT_F src = D2D1::RectF((bs.width - side) / 2, (bs.height - side) / 2, (bs.width + side) / 2, (bs.height + side) / 2);
    m_dc->DrawBitmap(bmp, Rf(L.miniCover), 1, D2D1_INTERPOLATION_MODE_HIGH_QUALITY_CUBIC, &src);
    m_dc->PopLayer();
  } else {
    auto g = Gradient(L.miniCover, m_a0, m_a2);
    m_dc->FillGeometry(clip.Get(), g.Get());
    Icon(kIcMusic, L.miniCover, 16, C(1, 1, 1, 0.8f));
  }
  m_dc->DrawGeometry(clip.Get(), Solid(C(1, 1, 1, 0.10f)), 1);

  const Rc& t = L.miniText;
  Text(m.track.title.empty() ? L"—" : m.track.title, Rc{t.l, t.t, t.r, t.Cy()}, 13, m_text, DWRITE_FONT_WEIGHT_SEMI_BOLD);
  Text(ArtistLine(m.track, L" — "), Rc{t.l, t.Cy(), t.r, t.b}, 11, m_dim);

  auto hov = [&](Hit h) { return s.hover.hit == h; };
  CircleButton(L.bPrev, kIcPrev, 11, hov(Hit::BPrev), false, false);
  CircleButton(L.bPlay, m.state == PlayState::Playing ? kIcPause : kIcPlay, 14, hov(Hit::BPlay), false, true);
  CircleButton(L.bNext, kIcNext, 11, hov(Hit::BNext), false, false);
  Text(FormatTime(m.position) + L" / " + FormatTime(m.track.duration), L.bTime, 11, m_dim, DWRITE_FONT_WEIGHT_NORMAL,
       DWRITE_TEXT_ALIGNMENT_CENTER);

  // Volume: icon and a thin gradient track.
  const Rc& v = L.volume;
  float vol = s.draggingVolume ? s.dragValue : m.volume;
  Icon(vol <= 0.001f ? kIcMute : kIcVolume, Box(v.l - 22, v.t, 18, v.H()), 13, m_dim);
  Rc track = Rc{v.l + 4, v.Cy() - 2, v.r, v.Cy() + 2};
  RoundFill(track, 2, Solid(m_light ? C(0, 0, 0, 0.12f) : C(1, 1, 1, 0.10f)));
  Rc fill = track;
  fill.r = track.l + track.W() * std::clamp(vol, 0.f, 1.f);
  auto g = Gradient(track, m_a0, m_a1);
  RoundFill(fill, 2, g.Get());
  if (hov(Hit::Volume) || s.draggingVolume)
    m_dc->FillEllipse(D2D1::Ellipse(D2D1::Point2F(fill.r, track.Cy()), 6, 6), Solid(C(1, 1, 1)));

  const Rc& st = L.btnSettings;
  RoundFill(st, 11, Solid(C(1, 1, 1, hov(Hit::Settings) || m.quickSettingsOpen ? 0.12f : 0.04f)));
  RoundStroke(st, 11, m_light ? C(0, 0, 0, 0.12f) : C(1, 1, 1, 0.14f));
  Icon(kIcSettings, st, 15, m_dim);
}

void UiPainter::Quick(const Model& m, const Layout& L, const UiState& s) {
  // The panel's glass is drawn by the GPU scene; here: heading, sections, switches, sliders, buttons.
  const Rc& q = L.quick;
  Text(L"Быстрые настройки", Rc{q.l + 18, q.t + 16, q.r - 60, q.t + 48}, 15, m_text, DWRITE_FONT_WEIGHT_SEMI_BOLD);
  Rc cl = QuickCloseRect(L);
  RoundFill(cl, 10, Solid(C(1, 1, 1, s.hover.hit == Hit::QuickClose ? 0.12f : 0.05f)));
  Icon(kIcClose, cl, 10, m_text);

  QuickRow rows[(int)QuickItem::Count];
  int n = QuickRows(L, rows, (int)QuickItem::Count);
  for (int i = 0; i < n; i++) {
    const QuickRow& r = rows[i];
    const wchar_t* section = r.item == QuickItem::Fading ? L"ВОСПРОИЗВЕДЕНИЕ"
                           : r.item == QuickItem::Background ? L"ВНЕШНИЙ ВИД"
                           : r.item == QuickItem::AutoScan   ? L"БИБЛИОТЕКА"
                                                             : nullptr;
    if (section)
      Text(section, Rc{r.rc.l + 2, r.rc.t - 28, r.rc.r, r.rc.t - 6}, 10, m_dim, DWRITE_FONT_WEIGHT_SEMI_BOLD,
           DWRITE_TEXT_ALIGNMENT_LEADING, false, 1.1f);
    bool hov = s.hover.index == (int)r.item &&
               (s.hover.hit == Hit::QuickToggle || s.hover.hit == Hit::QuickSlider || s.hover.hit == Hit::QuickTheme);
    if (hov && r.item != QuickItem::Theme) RoundFill(r.rc, 10, Solid(C(1, 1, 1, 0.05f)));

    const wchar_t* label = L"";
    bool on = false;
    int value = 0;
    switch (r.item) {
      case QuickItem::Fading: label = L"Плавные переходы"; on = m.aimpFading; break;
      case QuickItem::Background: label = L"Живой фон"; on = m.settings.background; break;
      case QuickItem::CoverGlow: label = L"Свечение обложки"; on = m.settings.coverGlow; break;
      case QuickItem::Spectrum: label = L"Спектр под обложкой"; on = m.settings.spectrum; break;
      case QuickItem::Ribbons: label = L"Яркость лент"; value = m.settings.ribbons; break;
      case QuickItem::Beat: label = L"Реакция на бит"; value = m.settings.beat; break;
      case QuickItem::AutoScan: label = L"Сканировать папки при запуске"; on = m.settings.autoScan; break;
      default: break;
    }
    if (r.item == QuickItem::Theme) {
      const wchar_t* names[3] = {L"Тёмная", L"Светлая", L"AMOLED"};
      float cw = (r.control.W() - 16) / 3;
      for (int k = 0; k < 3; k++) {
        Rc chip = Box(r.control.l + k * (cw + 8), r.control.t, cw, r.control.H());
        bool act = (int)m.theme == k;
        RoundFill(chip, 10, Solid(act ? WithA(m_a0, 0.22f) : C(1, 1, 1, s.hover.hit == Hit::QuickTheme && s.hover.index == k ? 0.09f : 0.04f)));
        RoundStroke(chip, 10, act ? WithA(m_a0, 0.5f) : C(1, 1, 1, 0.12f));
        Text(names[k], chip, 12, m_text, act ? DWRITE_FONT_WEIGHT_SEMI_BOLD : DWRITE_FONT_WEIGHT_NORMAL, DWRITE_TEXT_ALIGNMENT_CENTER);
      }
      continue;
    }
    Text(label, Rc{r.rc.l + 10, r.rc.t, r.control.l - 8, r.rc.b}, 13, m_text, DWRITE_FONT_WEIGHT_MEDIUM);
    if (r.item == QuickItem::Ribbons || r.item == QuickItem::Beat) {
      Rc tr = Rc{r.control.l, r.control.Cy() - 2, r.control.r, r.control.Cy() + 2};
      RoundFill(tr, 2, Solid(C(1, 1, 1, 0.12f)));
      Rc f = tr;
      f.r = tr.l + tr.W() * value / 200.f;
      auto g = Gradient(tr, m_a0, m_a1);
      RoundFill(f, 2, g.Get());
      m_dc->FillEllipse(D2D1::Ellipse(D2D1::Point2F(f.r, tr.Cy()), 7, 7), Solid(C(1, 1, 1)));
    } else {
      const Rc& sw = r.control;
      RoundFill(sw, sw.H() / 2, on ? (ID2D1Brush*)Gradient(sw, m_a0, m_a1).Get() : Solid(C(1, 1, 1, 0.14f)));
      float kx = on ? sw.r - sw.H() / 2 : sw.l + sw.H() / 2;
      m_dc->FillEllipse(D2D1::Ellipse(D2D1::Point2F(kx, sw.Cy()), sw.H() / 2 - 3, sw.H() / 2 - 3), Solid(C(1, 1, 1)));
    }
  }

  Rc a = QuickAimpSettingsRect(L);
  RoundFill(a, 11, Solid(C(1, 1, 1, s.hover.hit == Hit::QuickAimpSettings ? 0.10f : 0.05f)));
  RoundStroke(a, 11, C(1, 1, 1, 0.14f));
  Text(L"Все настройки AIMP…", a, 12.5f, m_text, DWRITE_FONT_WEIGHT_MEDIUM, DWRITE_TEXT_ALIGNMENT_CENTER);
  Rc c = QuickClassicRect(L);
  RoundFill(c, 11, Solid(C(1, 1, 1, s.hover.hit == Hit::QuickClassic ? 0.10f : 0.05f)));
  RoundStroke(c, 11, C(1, 1, 1, 0.14f));
  Text(L"Вернуть классический AIMP", c, 12.5f, m_text, DWRITE_FONT_WEIGHT_MEDIUM, DWRITE_TEXT_ALIGNMENT_CENTER);
}

}  // namespace glass
