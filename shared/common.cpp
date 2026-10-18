#include "common.h"

#include <shlobj.h>

#include <algorithm>

std::string ToUtf8(const std::wstring& w) {
  int n = WideCharToMultiByte(CP_UTF8, 0, w.c_str(), (int)w.size(), nullptr, 0, nullptr, nullptr);
  std::string s(n, '\0');
  WideCharToMultiByte(CP_UTF8, 0, w.c_str(), (int)w.size(), s.data(), n, nullptr, nullptr);
  return s;
}

std::wstring FromUtf8(const std::string& s) {
  int n = MultiByteToWideChar(CP_UTF8, 0, s.c_str(), (int)s.size(), nullptr, 0);
  std::wstring w(n, L'\0');
  MultiByteToWideChar(CP_UTF8, 0, s.c_str(), (int)s.size(), w.data(), n);
  return w;
}

std::string Trim(std::string s) {
  auto ws = [](char c) { return c == ' ' || c == '\r' || c == '\n' || c == '\t'; };
  while (!s.empty() && ws(s.back())) s.pop_back();
  size_t i = 0;
  while (i < s.size() && ws(s[i])) i++;
  return s.substr(i);
}

std::wstring ExePath() {
  wchar_t buf[MAX_PATH * 4];
  DWORD n = GetModuleFileNameW(nullptr, buf, ARRAYSIZE(buf));
  return std::wstring(buf, n);
}

std::wstring KnownFolder(const KNOWNFOLDERID& id) {
  PWSTR p = nullptr;
  std::wstring r;
  if (SUCCEEDED(SHGetKnownFolderPath(id, 0, nullptr, &p))) r = p;
  CoTaskMemFree(p);
  return r;
}

bool ReadAll(const std::wstring& path, std::string& out) {
  HANDLE f = CreateFileW(path.c_str(), GENERIC_READ, FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE, nullptr,
                         OPEN_EXISTING, 0, nullptr);
  if (f == INVALID_HANDLE_VALUE) return false;
  LARGE_INTEGER size{};
  GetFileSizeEx(f, &size);
  out.assign((size_t)std::min<LONGLONG>(size.QuadPart, 1 << 20), '\0');
  DWORD read = 0;
  BOOL ok = out.empty() || ReadFile(f, out.data(), (DWORD)out.size(), &read, nullptr);
  out.resize(read);
  CloseHandle(f);
  return ok != FALSE;
}

bool WriteAll(const std::wstring& path, const std::string& data) {
  std::wstring tmp = path + L".tmp";
  HANDLE f = CreateFileW(tmp.c_str(), GENERIC_WRITE, 0, nullptr, CREATE_ALWAYS, 0, nullptr);
  if (f == INVALID_HANDLE_VALUE) return false;
  DWORD written = 0;
  BOOL ok = data.empty() || WriteFile(f, data.data(), (DWORD)data.size(), &written, nullptr);
  CloseHandle(f);
  return ok && MoveFileExW(tmp.c_str(), path.c_str(), MOVEFILE_REPLACE_EXISTING);
}
