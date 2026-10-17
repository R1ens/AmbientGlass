#pragma once
#include <windows.h>

#include <string>

// Puts the VLC menu item (the Lua extension embedded as a resource) into VLC's extensions folder.
// `path` receives where it went. Without `force` an identical file is left untouched.
bool InstallLuaExtension(HINSTANCE inst, bool force, std::wstring& path);

// Registers or removes the "run at sign-in" entry for this exe.
bool SetAutostart(bool on, const std::wstring& exePath);
