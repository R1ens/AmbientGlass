#pragma once
// AimpGlass settings, stored as settings.ini next to the plugin's other data (%LOCALAPPDATA%\AimpGlass).

#include "settings_ini.h"

// type, name, default, min, max
#define GLASS_SETTINGS(X)                  \
  X(bool, background, 1, 0, 1)             \
  X(bool, coverGlow, 1, 0, 1)              \
  X(bool, spectrum, 1, 0, 1)               \
  X(int, ribbons, 100, 0, 200)             \
  X(int, beat, 100, 0, 200)                \
  X(int, frost, 100, 0, 100)               \
  X(int, mono, 0, 0, 100)                  \
  X(int, theme, 0, 0, 2)                   \
  X(bool, autoScan, 1, 0, 1)               \
  X(bool, hideAimp, 1, 0, 1)               \
  X(int, windowX, -32000, -32000, 32000)   \
  X(int, windowY, -32000, -32000, 32000)   \
  X(int, windowW, 1280, 880, 16000)        \
  X(int, windowH, 820, 600, 16000)         \
  X(bool, maximized, 0, 0, 1)

namespace glass {

struct GlassSettings {
  SETTINGS_STRUCT_BODY(GLASS_SETTINGS)
};

}  // namespace glass
