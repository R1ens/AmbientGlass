#pragma once
// Interaction state of the window (what the mouse is over, scrolling, the search box) and hit testing: which
// control is at a point. Pure, so tests can click through the layout.

#include <string>
#include <vector>

#include "layout.h"
#include "model.h"

namespace glass {

enum class Hit {
  None,
  Min, Max, Close, Caption,
  Nav0, Nav1, Nav2, Nav3, AddFolder, Scan, NavSettings,
  Cover, Shuffle, Prev, Play, Next, Repeat, Like, More, Wave,
  Search, Row, ListBackground, ListTitle,
  BPrev, BPlay, BNext, Volume,
  // Quick settings panel
  QuickPanel, QuickClose, QuickToggle, QuickSlider, QuickTheme, QuickAimpSettings, QuickClassic,
  // An open popup menu: an item, the menu's own background, anywhere else (closes it)
  MenuItem, MenuPanel, MenuClose,
};

struct HitResult {
  Hit hit = Hit::None;
  int index = -1;  // Row: index into Model::visible; quick settings: item index
};

// The quick settings panel lists these items, top to bottom.
enum class QuickItem {
  Fading, PauseBetween, Equalizer,   // AIMP playback (toggles, through the player's documented properties)
  Background, CoverGlow, Spectrum,   // appearance (toggles)
  Ribbons, Beat, Frost, Mono,        // appearance (sliders)
  Theme,                             // three chips
  AutoScan,                          // library (toggle)
  Count
};

struct QuickRow {
  QuickItem item;
  Rc rc;          // the whole row
  Rc control;     // the switch, slider track or chips
};

// Rows of the quick settings panel inside L.quick (section headings sit between them). A low panel packs
// the rows tighter so they always end above the two buttons at the bottom.
int QuickRows(const Layout& L, QuickRow* out, int max);
bool IsQuickSlider(QuickItem i);
// The setting a slider edits and its range (0..max).
int* QuickSliderValue(GlassSettings& s, QuickItem i);
int QuickSliderMax(QuickItem i);
Rc QuickCloseRect(const Layout& L);
Rc QuickAimpSettingsRect(const Layout& L);
Rc QuickClassicRect(const Layout& L);

// A small menu drawn inside the window (the "⋯" button, the playlist chooser). No modal loop, so the window
// keeps animating while it is open, and it is glass like the rest of the window.
struct PopupItem {
  std::wstring text;
  std::wstring hint;  // right-aligned, dimmer ("играет")
  std::wstring key;   // what the item refers to (a playlist id)
  int id = 0;
  bool checked = false, enabled = true, separator = false;
};
enum class MenuKind { None, More, Playlists };
struct PopupMenu {
  MenuKind kind = MenuKind::None;
  std::vector<PopupItem> items;
  Rc anchor;                // the control it opened from
  bool alignRight = false;  // its right edge under the anchor's right edge
  int hot = -1;             // the keyboard's item
};
// The menu under its anchor (above it when there is no room below), inside the window.
Rc MenuRect(const Layout& L, const PopupMenu& m);
Rc MenuItemRect(const Layout& L, const PopupMenu& m, int i);
// The next selectable item after i in direction dir (+1 / -1), wrapping around; -1 when there is none.
int MenuStep(const PopupMenu& m, int i, int dir);

struct UiState {
  HitResult hover, pressed;
  float listScroll = 0;      // px scrolled in the list, layout units
  bool searchFocus = false;
  size_t caret = 0;          // in Model::query
  float likeFlash = 0;       // 1 right after a like, decays
  bool maximized = false;
  bool draggingWave = false, draggingVolume = false, draggingSlider = false;
  float dragValue = 0;       // 0..1 while dragging the wave or volume
  float listTitleR = 0;      // right edge of the drawn playlist chooser button; 0 = all of L.listTitle
  PopupMenu menu;            // kind None while closed
};

HitResult HitTest(const Layout& L, const Model& m, const UiState& s, float x, float y);

// The row index (into Model::visible) at y inside L.rows, or -1.
int RowAt(const Layout& L, const UiState& s, int rowCount, float y);
// Scroll limit for a list of rowCount rows.
float MaxScroll(const Layout& L, int rowCount);

// Position along a horizontal control, 0..1.
float Fraction(const Rc& r, float x);


}  // namespace glass
