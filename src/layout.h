#pragma once
#include <windows.h>

#include "settings.h"
#include "vlc_tracker.h"

// Where one overlay draws, in screen coordinates.
struct GlowLayout {
  RECT overlay{};  // the overlay window
  RECT frame{};    // the rectangle the glow radiates from
  RECT scale{};    // the whole picture: its size sets the glow step and blur
  RECT hole{};     // left fully transparent (the picture itself)
  RECT pass{};     // full picture rect; burned-in bar content is copied through here when usePass
  bool usePass = false;
  bool opaque = false;  // drawn over VLC's black bars rather than over the desktop
};

// How far the inner overlay reaches into the picture on sides with glow, covering the outermost row
// that VLC often draws darker than the picture.
constexpr LONG kSeamCoverPx = 1;

// Burned-in bars as fractions of the picture, per side.
struct Bars {
  float vertical = 0;    // top and bottom
  float horizontal = 0;  // left and right
};

struct Layouts {
  RECT content{};         // the picture minus burned-in bars
  float contentUV[4]{};   // the same as a fraction of the picture: l, t, r, b
  bool inner = false;     // glow in the black areas of VLC's video area (and burned-in bars)
  bool outer = false;     // glow around the VLC window on the desktop
  GlowLayout innerL, outerL;
};

// Pure: what to draw where for the current VLC state. Needs vlc.Active().
Layouts ComputeLayouts(const VlcState& vlc, const Bars& bars, const Settings& s);

// Pure: the part of a captured window frame that holds the picture, in frame pixels.
// False when the frame does not match the window bounds (a frame behind while resizing) or is too small.
bool CaptureBox(const VlcState& vlc, SIZE frameSize, UINT texW, UINT texH, RECT& box);
