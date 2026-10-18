#include "log.h"

#include <cstdarg>
#include <cstdio>
#include <mutex>

#include "common.h"

namespace {

constexpr LONGLONG kMaxBytes = 512 * 1024;  // beyond this the first half is dropped
std::mutex g_mutex;
std::wstring g_path;
std::string g_lastError;

void Write(const char* level, const char* fmt, va_list args) {
  char msg[2048];
  vsnprintf(msg, sizeof(msg), fmt, args);
  std::lock_guard<std::mutex> lock(g_mutex);
  if (level[0] == 'E') g_lastError = msg;
  if (g_path.empty()) return;

  SYSTEMTIME t;
  GetLocalTime(&t);
  char line[2200];
  int n = snprintf(line, sizeof(line), "%04u-%02u-%02u %02u:%02u:%02u.%03u %s %s\n", t.wYear, t.wMonth, t.wDay, t.wHour,
                   t.wMinute, t.wSecond, t.wMilliseconds, level, msg);
  HANDLE f = CreateFileW(g_path.c_str(), FILE_APPEND_DATA, FILE_SHARE_READ | FILE_SHARE_DELETE, nullptr, OPEN_ALWAYS,
                         0, nullptr);
  if (f == INVALID_HANDLE_VALUE) return;
  DWORD written = 0;
  WriteFile(f, line, (DWORD)n, &written, nullptr);
  LARGE_INTEGER size{};
  GetFileSizeEx(f, &size);
  CloseHandle(f);

  if (size.QuadPart > kMaxBytes) {
    std::string all;
    if (ReadAll(g_path, all)) {
      size_t cut = all.size() / 2;
      size_t nl = all.find('\n', cut);
      if (nl != std::string::npos) WriteAll(g_path, all.substr(nl + 1));
    }
  }
}

}  // namespace

namespace Log {

void Init(const std::wstring& path) {
  std::lock_guard<std::mutex> lock(g_mutex);
  g_path = path;
}

void Info(const char* fmt, ...) {
  va_list args;
  va_start(args, fmt);
  Write("INFO ", fmt, args);
  va_end(args);
}

void Warn(const char* fmt, ...) {
  va_list args;
  va_start(args, fmt);
  Write("WARN ", fmt, args);
  va_end(args);
}

void Error(const char* fmt, ...) {
  va_list args;
  va_start(args, fmt);
  Write("ERROR", fmt, args);
  va_end(args);
}

std::string LastError() {
  std::lock_guard<std::mutex> lock(g_mutex);
  return g_lastError;
}

void ClearLastError() {
  std::lock_guard<std::mutex> lock(g_mutex);
  g_lastError.clear();
}

std::string HrText(HRESULT hr) {
  wchar_t* text = nullptr;
  DWORD n = FormatMessageW(FORMAT_MESSAGE_ALLOCATE_BUFFER | FORMAT_MESSAGE_FROM_SYSTEM | FORMAT_MESSAGE_IGNORE_INSERTS,
                           nullptr, (DWORD)hr, 0, reinterpret_cast<LPWSTR>(&text), 0, nullptr);
  std::wstring msg = n ? std::wstring(text, n) : L"";
  if (text) LocalFree(text);
  while (!msg.empty() && (msg.back() == L'\r' || msg.back() == L'\n' || msg.back() == L' ')) msg.pop_back();
  char code[16];
  snprintf(code, sizeof(code), "0x%08lX", (unsigned long)hr);
  return std::string(code) + (msg.empty() ? "" : ": " + ToUtf8(msg));
}

}  // namespace Log
