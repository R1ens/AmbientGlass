#include "system_integration.h"

#include <shlobj.h>

#include "common.h"
#include "log.h"
#include "resource.h"

namespace {
constexpr wchar_t kRunKey[] = L"Software\\Microsoft\\Windows\\CurrentVersion\\Run";
constexpr wchar_t kRunValue[] = L"VlcAmbient";
}  // namespace

bool InstallLuaExtension(HINSTANCE inst, bool force, std::wstring& path) {
  std::wstring dir = KnownFolder(FOLDERID_RoamingAppData) + L"\\vlc\\lua\\extensions";
  path = dir + L"\\vlc_ambient.lua";
  HRSRC res = FindResourceW(inst, MAKEINTRESOURCEW(IDR_LUA), RT_RCDATA);
  HGLOBAL mem = res ? LoadResource(inst, res) : nullptr;
  if (!mem) {
    Log::Error("lua: extension resource missing from the exe");
    return false;
  }
  std::string data(static_cast<const char*>(LockResource(mem)), SizeofResource(inst, res));
  std::string existing;
  if (!force && ReadAll(path, existing) && existing == data) return true;
  SHCreateDirectoryExW(nullptr, dir.c_str(), nullptr);
  bool ok = WriteAll(path, data);
  if (ok) Log::Info("lua: installed %s", ToUtf8(path).c_str());
  else Log::Error("lua: cannot write %s (%lu)", ToUtf8(path).c_str(), GetLastError());
  return ok;
}

bool SetAutostart(bool on, const std::wstring& exePath) {
  HKEY key;
  if (RegCreateKeyExW(HKEY_CURRENT_USER, kRunKey, 0, nullptr, 0, KEY_SET_VALUE, nullptr, &key, nullptr) !=
      ERROR_SUCCESS) {
    Log::Error("autostart: cannot open the Run key");
    return false;
  }
  LSTATUS st;
  if (on) {
    std::wstring value = L"\"" + exePath + L"\" --background";
    st = RegSetValueExW(key, kRunValue, 0, REG_SZ, reinterpret_cast<const BYTE*>(value.c_str()),
                        (DWORD)((value.size() + 1) * sizeof(wchar_t)));
  } else {
    st = RegDeleteValueW(key, kRunValue);
    if (st == ERROR_FILE_NOT_FOUND) st = ERROR_SUCCESS;
  }
  RegCloseKey(key);
  if (st != ERROR_SUCCESS) Log::Error("autostart: registry error %ld", (long)st);
  return st == ERROR_SUCCESS;
}
