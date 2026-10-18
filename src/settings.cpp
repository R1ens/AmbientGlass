#include "settings.h"

void Settings::Clamp() { ClampSettings(*this); }

bool LoadSettings(const std::wstring& path, Settings& s) { return LoadSettingsIni(path, s); }

bool SaveSettings(const std::wstring& path, const Settings& s) { return SaveSettingsIni(path, s); }
