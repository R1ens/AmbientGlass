#include "settings.h"

#include <windows.h>
#include <algorithm>
#include <fstream>
#include <sstream>

#include "common.h"

void Settings::Clamp() {
#define VA_CLAMP(t, n, d, lo, hi) n = (t)std::clamp((int)n, (int)(lo), (int)(hi));
  VA_SETTINGS(VA_CLAMP)
#undef VA_CLAMP
}

bool LoadSettings(const std::wstring& path, Settings& s) {
  std::ifstream f(path);
  if (!f) return false;
  std::string line;
  bool first = true;
  while (std::getline(f, line)) {
    if (first && line.rfind("\xEF\xBB\xBF", 0) == 0) line.erase(0, 3);  // a BOM from Notepad
    first = false;
    auto eq = line.find('=');
    if (eq == std::string::npos) continue;
    std::string key = Trim(line.substr(0, eq));
    std::string val = Trim(line.substr(eq + 1));
    int v = 0;
    try {
      v = std::stoi(val);
    } catch (...) {
      continue;
    }
    // Bools: any non-zero means on, so clamping before the cast keeps "-5" from becoming true.
#define VA_LOAD(t, n, d, lo, hi)                                  \
  if (key == #n) {                                                \
    s.n = (t)std::clamp(v, (int)(lo), (int)(hi));                 \
    continue;                                                     \
  }
    VA_SETTINGS(VA_LOAD)
#undef VA_LOAD
  }
  s.Clamp();
  return true;
}

bool SaveSettings(const std::wstring& path, const Settings& s) {
  std::ostringstream o;
#define VA_SAVE(t, n, d, lo, hi) o << #n << '=' << (int)s.n << '\n';
  VA_SETTINGS(VA_SAVE)
#undef VA_SAVE
  std::wstring tmp = path + L".tmp";
  {
    std::ofstream f(tmp, std::ios::binary | std::ios::trunc);
    if (!f) return false;
    f << o.str();
    if (!f) return false;
  }
  return MoveFileExW(tmp.c_str(), path.c_str(), MOVEFILE_REPLACE_EXISTING) != 0;
}
