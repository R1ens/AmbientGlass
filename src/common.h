#pragma once
#include <windows.h>
#include <dxgiformat.h>
#include <shtypes.h>  // KNOWNFOLDERID

#include <string>

// Every GPU texture and swap chain in the glow pipeline: linear scRGB with alpha.
constexpr DXGI_FORMAT kGpuFormat = DXGI_FORMAT_R16G16B16A16_FLOAT;

inline int RectW(const RECT& r) { return r.right - r.left; }
inline int RectH(const RECT& r) { return r.bottom - r.top; }
inline bool SameRect(const RECT& a, const RECT& b) {
  return a.left == b.left && a.top == b.top && a.right == b.right && a.bottom == b.bottom;
}

std::string ToUtf8(const std::wstring& w);
std::wstring FromUtf8(const std::string& s);
std::string Trim(std::string s);

std::wstring ExePath();
std::wstring KnownFolder(const KNOWNFOLDERID& id);

// Whole-file helpers. WriteAll goes through a temp file and a rename, so a reader never sees a partial file.
bool ReadAll(const std::wstring& path, std::string& out);
bool WriteAll(const std::wstring& path, const std::string& data);
