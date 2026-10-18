#include "settings_ini.h"

#include <windows.h>
#include <fstream>

#include "common.h"

bool ReadIniInts(const std::wstring& path, const std::function<void(const std::string& key, int value)>& onValue) {
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
    onValue(key, v);
  }
  return true;
}

bool WriteIniText(const std::wstring& path, const std::string& text) {
  std::wstring tmp = path + L".tmp";
  {
    std::ofstream f(tmp, std::ios::binary | std::ios::trunc);
    if (!f) return false;
    f << text;
    if (!f) return false;
  }
  return MoveFileExW(tmp.c_str(), path.c_str(), MOVEFILE_REPLACE_EXISTING) != 0;
}
