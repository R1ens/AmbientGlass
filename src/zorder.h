#pragma once
#include "overlay.h"

// Keeps the inner overlay directly above the VLC window and the outer one directly below it,
// in the same band (normal or "always on top") as VLC. Hidden overlays are left alone.
void PlaceOverlays(HWND vlc, Overlay& inner, Overlay& outer);
