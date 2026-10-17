#pragma once
#include <windows.h>

// SDR white of a monitor in scRGB units (1.0 = 80 nits). 1.0 unless Windows HDR is on for it.
float SdrWhiteLevel(HMONITOR monitor);
