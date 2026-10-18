#pragma once
// Where every part of the window goes, in device-independent pixels (1/96 inch) from the window's top left.
// Pure function of the window size, so it is tested without a window.

namespace glass {

struct Rc {
  float l = 0, t = 0, r = 0, b = 0;
  float W() const { return r - l; }
  float H() const { return b - t; }
  float Cx() const { return (l + r) / 2; }
  float Cy() const { return (t + b) / 2; }
  bool Contains(float x, float y) const { return x >= l && x < r && y >= t && y < b; }
  bool Valid() const { return r > l && b > t; }
};

constexpr float kMinWindowW = 880, kMinWindowH = 600;
constexpr float kPanelRadius = 20;

struct Layout {
  float w = 0, h = 0;
  bool compactSidebar = false;  // narrow windows: icons only

  Rc titlebar, btnMin, btnMax, btnClose, caption;  // caption: the draggable part of the title bar

  Rc sidebar;  // glass panel
  Rc nav[4];   // Плейлисты, Папки, Недавнее, Избранное
  Rc addBox, btnAddFolder, btnScan, navSettings;

  Rc card;  // "now playing" glass panel
  Rc cover, coverGlow, spectrum;
  Rc info, badge, title, artist;
  Rc waveTimes, wave;
  Rc btnShuffle, btnPrev, btnPlay, btnNext, btnRepeat, btnLike, btnMore;

  Rc list;  // playlist glass panel
  Rc listHead, listTitle, search, tableHead, rows;  // listTitle: the clickable title (playlist chooser)
  float rowHeight = 40;
  float colNum = 28, colAlbum = 120, colDur = 48, colGap = 8;

  Rc bottom;  // bottom bar glass panel
  Rc miniCover, miniText, bPrev, bPlay, bNext, bTime, volume, btnSettings;

  Rc quick;  // quick settings panel (when open)
};

Layout ComputeLayout(float width, float height);

}  // namespace glass
