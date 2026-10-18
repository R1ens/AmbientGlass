#pragma once
#include <string>

#include "settings_ini.h"

// All user settings: type, name, default, min, max.
#define VA_SETTINGS(X)                      \
  X(bool, enabled,        1,    0,   1)     \
  X(int,  blur,           30,   0,   100)   \
  X(int,  edge,           12,   2,   50)    \
  X(int,  spread,         17,   0,   400)   \
  X(int,  fadeStart,      15,  -50,  100)   \
  X(int,  fadeCurve,      35,   1,   100)   \
  X(int,  edgeBlend,      40,   0,   100)   \
  X(int,  smoothingMs,    0,    0,   3000)  \
  X(int,  cutFadeMs,      150,  0,   1000)  \
  X(int,  debanding,      0,    0,   100)   \
  X(int,  brightness,     100,  0,   200)   \
  X(int,  contrast,       100,  0,   200)   \
  X(int,  saturation,     100,  0,   200)   \
  X(int,  vibrance,       100,  0,   200)   \
  X(int,  hdrBrightness,  100,  0,   200)   \
  X(int,  hdrContrast,    100,  0,   200)   \
  X(int,  hdrSaturation,  100,  0,   200)   \
  X(bool, dirTop,         1,    0,   1)     \
  X(bool, dirRight,       1,    0,   1)     \
  X(bool, dirBottom,      1,    0,   1)     \
  X(bool, dirLeft,        1,    0,   1)     \
  X(bool, aroundWindow,   0,    0,   1)     \
  X(bool, inBars,         1,    0,   1)     \
  X(bool, detectBars,     0,    0,   1)     \
  X(bool, detectSideBars, 0,    0,   1)     \
  X(bool, autostart,      0,    0,   1)

struct Settings {
  SETTINGS_STRUCT_BODY(VA_SETTINGS)

  void Clamp();
  bool operator==(const Settings&) const = default;
};

bool LoadSettings(const std::wstring& path, Settings& s);
bool SaveSettings(const std::wstring& path, const Settings& s);
