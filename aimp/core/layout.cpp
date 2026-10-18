#include "layout.h"

#include <algorithm>

namespace glass {
namespace {

constexpr float kTitle = 36, kPad = 12, kGap = 12;
constexpr float kSidebar = 240, kSidebarCompact = 64;
constexpr float kBottom = 72;
constexpr float kCardPad = 22, kCardGap = 18;
constexpr float kListMin = 220, kListShare = 0.34f;
constexpr float kCoverMax = 420, kSpectrumMaxH = 120, kSpectrumMinH = 28;

Rc R(float l, float t, float r, float b) { return Rc{l, t, r, b}; }
Rc Box(float x, float y, float w, float h) { return Rc{x, y, x + w, y + h}; }

}  // namespace

Layout ComputeLayout(float width, float height) {
  Layout L;
  const float W = std::max(width, kMinWindowW), H = std::max(height, kMinWindowH);
  L.w = W;
  L.h = H;
  L.compactSidebar = W < 1080;

  // Title bar with three window buttons on the right; the rest of it drags the window.
  L.titlebar = R(0, 0, W, kTitle);
  L.btnClose = Box(W - 46, 0, 46, kTitle);
  L.btnMax = Box(W - 92, 0, 46, kTitle);
  L.btnMin = Box(W - 138, 0, 46, kTitle);
  L.caption = R(0, 0, L.btnMin.l, kTitle);

  // Bottom bar across the window, the sidebar and the centre column above it.
  L.bottom = R(kPad, H - kPad - kBottom, W - kPad, H - kPad);
  const float top = kTitle + kPad, bottom = L.bottom.t - kGap;
  const float sideW = L.compactSidebar ? kSidebarCompact : kSidebar;
  L.sidebar = R(kPad, top, kPad + sideW, bottom);

  // Sidebar: four navigation items, the add-folder box, settings at the bottom.
  {
    const Rc& s = L.sidebar;
    float y = s.t + (L.compactSidebar ? 14 : 40);  // room for the "МОЯ МУЗЫКА" heading
    const float x0 = s.l + 10, x1 = s.r - 10;
    for (Rc& n : L.nav) {
      n = R(x0, y, x1, y + 44);
      y += 48;
    }
    if (!L.compactSidebar) {
      L.addBox = R(x0, y + 8, x1, y + 8 + 104);
      L.btnAddFolder = R(L.addBox.l + 10, L.addBox.t + 10, L.addBox.r - 10, L.addBox.t + 48);
      L.btnScan = R(L.addBox.l + 10, L.btnAddFolder.b + 8, L.addBox.r - 10, L.btnAddFolder.b + 44);
    } else {
      L.addBox = R(x0, y + 8, x1, y + 8 + 44);
      L.btnAddFolder = L.addBox;
    }
    L.navSettings = R(x0, s.b - 12 - 44, x1, s.b - 12);
  }

  // Centre column: the now-playing card above the playlist panel.
  const float cx0 = L.sidebar.r + kGap, cx1 = W - kPad;
  const float colH = bottom - top;
  float listH = std::max(kListMin, colH * kListShare);
  listH = std::min(listH, colH - 290);  // the card keeps room for title, artist, waveform and controls
  listH = std::max(listH, 160.f);
  L.card = R(cx0, top, cx1, bottom - listH - kGap);
  L.list = R(cx0, L.card.b + kGap, cx1, bottom);

  // Card: square cover on the left, centred in the card's height; track info, spectrum and controls on the right.
  {
    const Rc& c = L.card;
    const float innerH = c.H() - 2 * kCardPad;
    // The info column needs ~360 px for the transport buttons plus like and more.
    float wrapW = std::min({c.W() * 0.52f, kCoverMax, c.W() - 2 * kCardPad - kCardGap - 360});
    float side = std::min(wrapW * 0.88f, innerH);
    side = std::max(side, 96.f);
    wrapW = std::min(wrapW, side / 0.88f);
    const float wrapL = c.l + kCardPad;
    const float coverL = wrapL + (wrapW - side) / 2;
    const float coverT = c.t + kCardPad + std::max(0.f, (innerH - side) / 2);
    L.cover = Box(coverL, coverT, side, side);
    L.coverGlow = Box(L.cover.Cx() - side * 0.49f, L.cover.Cy() - side * 0.49f, side * 0.98f, side * 0.98f);

    L.info = R(wrapL + wrapW + kCardGap, c.t + kCardPad + 8, c.r - kCardPad, c.b - kCardPad - 8);
    const Rc& in = L.info;
    // A short card drops the badge and keeps the title to one line, so text never runs into the waveform.
    const bool roomy = in.H() >= 290;
    float y = in.t;
    if (roomy) {
      L.badge = Box(in.l, in.t, std::min(in.W(), 260.f), 24);
      y = L.badge.b + 12;
    } else {
      L.badge = Rc{};
    }
    L.title = R(in.l, y, in.r, y + (roomy ? 72 : 40));  // up to two lines of 32 px, or one
    L.artist = R(in.l, L.title.b + 6, in.r, L.title.b + 6 + 22);
    // Controls along the bottom of the info column, the waveform above them.
    const float ctlY = in.b - 54;
    float x = in.l;
    auto round = [&](float d) {
      Rc r = Box(x, ctlY + (54 - d) / 2, d, d);
      x += d + 10;
      return r;
    };
    L.btnShuffle = round(42);
    L.btnPrev = round(42);
    L.btnPlay = round(54);
    L.btnNext = round(42);
    L.btnRepeat = round(42);
    L.btnMore = Box(in.r - 38, ctlY + 8, 38, 38);
    L.btnLike = Box(L.btnMore.l - 8 - 38, ctlY + 8, 38, 38);
    L.wave = R(in.l, ctlY - 16 - 54, in.r, ctlY - 16);
    L.waveTimes = R(in.l, L.wave.t - 6 - 14, in.r, L.wave.t - 6);
    // The file's format under the artist; a short card moves it between the two times above the waveform,
    // leaving the height to the spectrum.
    L.format = R(in.l, L.artist.b + 6, in.r, L.artist.b + 6 + 18);
    L.formatInTimes = L.waveTimes.t - 6 - (L.format.b + 10) < 60;
    if (L.formatInTimes) L.format = R(in.l + 56, L.waveTimes.t - 2, in.r - 56, L.waveTimes.b + 2);
    // The spectrum fills the free space between the text and the waveform, centred in it.
    const float freeT = (L.formatInTimes ? L.artist.b : L.format.b) + 10, freeB = L.waveTimes.t - 6;
    const float sh = std::min(kSpectrumMaxH, freeB - freeT);
    L.spectrum = sh >= kSpectrumMinH ? R(in.l, freeT + (freeB - freeT - sh) / 2, in.r, freeT + (freeB - freeT - sh) / 2 + sh)
                                     : Rc{};
  }

  // Playlist panel: title and search on top, column headings, then the rows.
  {
    const Rc& p = L.list;
    L.listHead = R(p.l + 12, p.t + 14, p.r - 12, p.t + 14 + 30);
    L.search = R(L.listHead.r - 220, L.listHead.t, L.listHead.r, L.listHead.b);
    L.listTitle = R(L.listHead.l, L.listHead.t, L.search.l - 12, L.listHead.b);
    // A low panel drops the column headings so at least two rows stay visible.
    if (p.H() >= 200) {
      L.tableHead = R(p.l + 12, L.listHead.b + 6, p.r - 12, L.listHead.b + 6 + 24);
      L.rows = R(p.l + 12, L.tableHead.b + 2, p.r - 12, p.b - 12);
    } else {
      L.tableHead = Rc{};
      L.rows = R(p.l + 12, L.listHead.b + 6, p.r - 12, p.b - 12);
    }
  }

  // Bottom bar.
  {
    const Rc& b = L.bottom;
    const float cy = b.Cy();
    L.miniCover = Box(b.l + 14, cy - 24, 48, 48);
    L.volume = Box(b.r - 18 - 120, cy - 10, 120, 20);
    L.bTime = Box(L.volume.l - 14 - 96, cy - 10, 96, 20);
    L.bNext = Box(L.bTime.l - 8 - 36, cy - 18, 36, 36);
    L.bPlay = Box(L.bNext.l - 8 - 44, cy - 22, 44, 44);
    L.bPrev = Box(L.bPlay.l - 8 - 36, cy - 18, 36, 36);
    L.miniText = R(L.miniCover.r + 14, cy - 20, L.bPrev.l - 14, cy + 20);
  }

  // Quick settings: a modal panel on the right; low windows let it reach over the bottom bar.
  L.quick = R(W - kPad - 360, kTitle + kPad, W - kPad, H < 760 ? H - kPad : L.bottom.t - kGap);
  return L;
}

}  // namespace glass
