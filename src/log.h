#pragma once
#include <windows.h>

#include <string>

// A small append-only log file, size-capped, with the last error kept for the panel.
// Safe to call from any thread. Before Init() messages only update the last error.
namespace Log {

void Init(const std::wstring& path);
void Info(const char* fmt, ...);
void Warn(const char* fmt, ...);
void Error(const char* fmt, ...);

// The most recent Error() message, UTF-8; empty when none.
std::string LastError();
void ClearLastError();

// "0x8007000E: Ran out of memory" for an HRESULT, UTF-8.
std::string HrText(HRESULT hr);

}  // namespace Log
