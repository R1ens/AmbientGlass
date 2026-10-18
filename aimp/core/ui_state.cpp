#include "ui_state.h"

#include <algorithm>
#include <cmath>

namespace glass {
namespace {

// A low quick settings panel puts its two buttons side by side to leave the rows more room.
bool QuickLow(const Layout& L) { return L.quick.H() < 640; }

// Section headings in the quick settings panel come before these items.
bool StartsSection(QuickItem i) {
  return i == QuickItem::Fading || i == QuickItem::Background || i == QuickItem::AutoScan;
}

}  // namespace

bool IsQuickSlider(QuickItem i) {
  return i == QuickItem::Ribbons || i == QuickItem::Beat || i == QuickItem::Frost || i == QuickItem::Mono;
}

int* QuickSliderValue(GlassSettings& s, QuickItem i) {
  switch (i) {
    case QuickItem::Ribbons: return &s.ribbons;
    case QuickItem::Beat: return &s.beat;
    case QuickItem::Frost: return &s.frost;
    case QuickItem::Mono: return &s.mono;
    default: return nullptr;
  }
}

int QuickSliderMax(QuickItem i) { return i == QuickItem::Ribbons || i == QuickItem::Beat ? 200 : 100; }

int QuickRows(const Layout& L, QuickRow* out, int max) {
  const Rc& q = L.quick;
  // Natural sizes, scaled down together when the panel is too low for them.
  const float top = q.t + (QuickLow(L) ? 56 : 64), bottom = QuickAimpSettingsRect(L).t - 10;
  float rowH = 40, themeH = 44, sectionH = 30, gap = 4;
  float need = 3 * sectionH + ((int)QuickItem::Count - 1) * (rowH + gap) + themeH + gap;
  float k = std::clamp((bottom - top) / need, 0.6f, 1.f);
  rowH *= k, themeH *= k, sectionH *= k, gap *= k;
  float y = top;
  int n = 0;
  for (int i = 0; i < (int)QuickItem::Count && n < max; i++) {
    QuickItem it = (QuickItem)i;
    if (StartsSection(it)) y += sectionH;  // section title
    const bool slider = IsQuickSlider(it);
    const float h = it == QuickItem::Theme ? themeH : rowH;
    QuickRow r;
    r.item = it;
    r.rc = Rc{q.l + 16, y, q.r - 16, y + h};
    if (slider)
      r.control = Rc{r.rc.r - 150, r.rc.Cy() - 10, r.rc.r, r.rc.Cy() + 10};
    else if (it == QuickItem::Theme)
      r.control = Rc{r.rc.l, r.rc.t + 6, r.rc.r, r.rc.b - 6};
    else
      r.control = Rc{r.rc.r - 40, r.rc.Cy() - 11, r.rc.r, r.rc.Cy() + 11};
    out[n++] = r;
    y += h + gap;
  }
  return n;
}

Rc QuickCloseRect(const Layout& L) { return Rc{L.quick.r - 16 - 32, L.quick.t + 16, L.quick.r - 16, L.quick.t + 48}; }

Rc QuickAimpSettingsRect(const Layout& L) {
  const Rc& q = L.quick;
  if (QuickLow(L)) return Rc{q.l + 16, q.b - 14 - 34, q.Cx() - 4, q.b - 14};
  return Rc{q.l + 16, q.b - 16 - 40 - 8 - 40, q.r - 16, q.b - 16 - 40 - 8};
}
Rc QuickClassicRect(const Layout& L) {
  const Rc& q = L.quick;
  if (QuickLow(L)) return Rc{q.Cx() + 4, q.b - 14 - 34, q.r - 16, q.b - 14};
  return Rc{q.l + 16, q.b - 16 - 40, q.r - 16, q.b - 16};
}

float MaxScroll(const Layout& L, int rowCount) { return std::max(0.f, rowCount * L.rowHeight - L.rows.H()); }

int RowAt(const Layout& L, const UiState& s, int rowCount, float y) {
  if (y < L.rows.t || y >= L.rows.b) return -1;
  int i = (int)std::floor((y - L.rows.t + s.listScroll) / L.rowHeight);
  return i >= 0 && i < rowCount ? i : -1;
}

float Fraction(const Rc& r, float x) { return r.W() > 0 ? std::clamp((x - r.l) / r.W(), 0.f, 1.f) : 0.f; }

std::wstring MenuText(const std::wstring& s) {
  std::wstring out;
  out.reserve(s.size());
  for (wchar_t c : s) {
    if (c == L'\t') c = L' ';  // a tab would start the right-aligned column
    out += c;
    if (c == L'&') out += L'&';
  }
  return out;
}

HitResult HitTest(const Layout& L, const Model& m, const UiState& s, float x, float y) {
  auto in = [&](const Rc& r) { return r.Valid() && r.Contains(x, y); };
  auto round = [&](const Rc& r) {  // circular buttons: corners do not count
    float dx = x - r.Cx(), dy = y - r.Cy(), rad = r.W() / 2;
    return r.Valid() && dx * dx + dy * dy <= rad * rad;
  };

  if (in(L.btnClose)) return {Hit::Close};
  if (in(L.btnMax)) return {Hit::Max};
  if (in(L.btnMin)) return {Hit::Min};

  // The open quick settings panel takes every click; outside it a click closes it.
  if (m.quickSettingsOpen && y >= L.titlebar.b) {
    if (!in(L.quick)) return {Hit::QuickClose};
    if (in(QuickCloseRect(L))) return {Hit::QuickClose};
    if (in(QuickAimpSettingsRect(L))) return {Hit::QuickAimpSettings};
    if (in(QuickClassicRect(L))) return {Hit::QuickClassic};
    QuickRow rows[(int)QuickItem::Count];
    int n = QuickRows(L, rows, (int)QuickItem::Count);
    for (int i = 0; i < n; i++) {
      if (!in(rows[i].rc)) continue;
      if (rows[i].item == QuickItem::Theme) {
        int chip = (int)std::floor(Fraction(rows[i].control, x) * 3);
        return {Hit::QuickTheme, std::clamp(chip, 0, 2)};
      }
      if (IsQuickSlider(rows[i].item))
        return in(rows[i].control) ? HitResult{Hit::QuickSlider, (int)rows[i].item} : HitResult{Hit::QuickPanel};
      return {Hit::QuickToggle, (int)rows[i].item};  // the whole row toggles
    }
    return {Hit::QuickPanel};
  }

  if (in(L.caption)) return {Hit::Caption};

  for (int i = 0; i < 4; i++)
    if (in(L.nav[i])) return {(Hit)((int)Hit::Nav0 + i)};
  if (in(L.btnAddFolder)) return {Hit::AddFolder};
  if (in(L.btnScan)) return {Hit::Scan};
  if (in(L.navSettings)) return {Hit::NavSettings};

  if (in(L.cover)) return {Hit::Cover};
  if (round(L.btnShuffle)) return {Hit::Shuffle};
  if (round(L.btnPrev)) return {Hit::Prev};
  if (round(L.btnPlay)) return {Hit::Play};
  if (round(L.btnNext)) return {Hit::Next};
  if (round(L.btnRepeat)) return {Hit::Repeat};
  if (in(L.btnLike)) return {Hit::Like};
  if (in(L.btnMore)) return {Hit::More};
  if (in(L.wave)) return {Hit::Wave};

  if (in(L.search)) return {Hit::Search};
  // The list title chooses among AIMP's playlists (only in the Playlists view, when there is a choice).
  if (m.nav == Nav::Playlists && m.playlists.size() > 1 && in(L.listTitle) && (s.listTitleR <= 0 || x <= s.listTitleR))
    return {Hit::ListTitle};
  if (in(L.rows)) {
    int r = RowAt(L, s, (int)m.visible.size(), y);
    return r >= 0 ? HitResult{Hit::Row, r} : HitResult{Hit::ListBackground};
  }

  if (round(L.bPrev)) return {Hit::BPrev};
  if (round(L.bPlay)) return {Hit::BPlay};
  if (round(L.bNext)) return {Hit::BNext};
  // The volume track is thin: accept a taller strip around it.
  Rc vol = L.volume;
  vol.t -= 6, vol.b += 6;
  if (in(vol)) return {Hit::Volume};
  if (in(L.btnSettings)) return {Hit::Settings};
  return {Hit::None};
}

}  // namespace glass
