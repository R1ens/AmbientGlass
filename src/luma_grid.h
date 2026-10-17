#pragma once
#include <vector>

// Downscaled luminance of the current picture, for black bar and HDR detection.
struct LumaGrid {
  std::vector<float> luma;  // linear luma (1.0 = SDR white), row-major
  int w = 0, h = 0;
  float maxLinear = 0;      // brightest channel value, linear (1.0 = SDR white)
};
