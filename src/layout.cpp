#include "layout.h"

#include <algorithm>
#include <cmath>

#include "common.h"
#include "glow_math.h"

Layouts ComputeLayouts(const VlcState& vlc, const Bars& bars, const Settings& s) {
  Layouts out;
  const RECT pic = vlc.pictureRect;
  const float bv = std::clamp(bars.vertical, 0.f, 0.49f), bh = std::clamp(bars.horizontal, 0.f, 0.49f);
  out.content = RECT{pic.left + std::lround(bh * RectW(pic)), pic.top + std::lround(bv * RectH(pic)),
                     pic.right - std::lround(bh * RectW(pic)), pic.bottom - std::lround(bv * RectH(pic))};
  out.contentUV[0] = bh;
  out.contentUV[1] = bv;
  out.contentUV[2] = 1.f - bh;
  out.contentUV[3] = 1.f - bv;

  // Inner: VLC's own black areas plus burned-in bars, everything in the video area except the picture.
  const RECT& area = vlc.videoMainRect;
  const RECT& content = out.content;
  out.inner = s.inBars && (content.left - area.left > 1 || content.top - area.top > 1 ||
                           area.right - content.right > 1 || area.bottom - content.bottom > 1);
  if (out.inner) {
    GlowLayout& L = out.innerL;
    L.overlay = area;
    L.frame = content;
    L.scale = pic;
    // VLC often leaves its outermost picture row or column darker than the picture (a fractional
    // viewport blends it with the black behind), which shows as a thin dark line along the seam. On
    // every side with glow next to it, the overlay covers that row with the edge colour.
    L.hole = content;
    if (content.left - area.left > 1) L.hole.left += kSeamCoverPx;
    if (content.top - area.top > 1) L.hole.top += kSeamCoverPx;
    if (area.right - content.right > 1) L.hole.right -= kSeamCoverPx;
    if (area.bottom - content.bottom > 1) L.hole.bottom -= kSeamCoverPx;
    L.pass = pic;
    L.usePass = !SameRect(content, pic);
    L.opaque = true;
  }

  // Outer: around the whole VLC window, on the desktop behind it. No hole: VLC covers that area
  // anyway, and a hole would show notches at rounded window corners.
  if (s.aroundWindow && !vlc.fullscreen) {
    const RECT& win = vlc.mainRect;
    LONG reach = (LONG)std::ceil(GlowReach(s, (float)RectW(pic), (float)RectH(pic)));
    RECT o{win.left - reach, win.top - reach, win.right + reach, win.bottom + reach};
    IntersectRect(&o, &o, &vlc.monitorRect);
    if (RectW(o) > 0 && RectH(o) > 0) {
      out.outer = true;
      GlowLayout& L = out.outerL;
      L.overlay = o;
      L.frame = win;
      L.scale = pic;
    }
  }
  return out;
}

bool CaptureBox(const VlcState& vlc, SIZE frameSize, UINT texW, UINT texH, RECT& box) {
  const RECT& win = vlc.mainRect;
  const RECT& pic = vlc.pictureRect;
  // The capture is 1:1 with the window's visible bounds.
  if (frameSize.cx != RectW(win) || frameSize.cy != RectH(win)) return false;
  LONG maxX = std::min<LONG>(frameSize.cx, (LONG)texW), maxY = std::min<LONG>(frameSize.cy, (LONG)texH);
  box.left = std::clamp<LONG>(pic.left - win.left, 0, maxX);
  box.right = std::clamp<LONG>(pic.right - win.left, 0, maxX);
  box.top = std::clamp<LONG>(pic.top - win.top, 0, maxY);
  box.bottom = std::clamp<LONG>(pic.bottom - win.top, 0, maxY);
  return box.right > box.left + 8 && box.bottom > box.top + 8;
}
