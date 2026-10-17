#include "ipc.h"

#include <shlobj.h>

#include <ctime>

#include "common.h"
#include "log.h"

namespace {
constexpr ULONGLONG kHeartbeatMs = 5000;
}

std::wstring Ipc::DataDir() { return KnownFolder(FOLDERID_LocalAppData) + L"\\VlcAmbient"; }

bool Ipc::Init(const std::wstring& exePath) {
  m_dir = DataDir();
  SHCreateDirectoryExW(nullptr, m_dir.c_str(), nullptr);
  DeleteFileW((m_dir + L"\\command").c_str());  // a leftover from a previous run means nothing now
  if (!WriteAll(m_dir + L"\\exe_path", ToUtf8(exePath))) Log::Warn("ipc: could not write exe_path");
  m_change = FindFirstChangeNotificationW(m_dir.c_str(), FALSE,
                                          FILE_NOTIFY_CHANGE_FILE_NAME | FILE_NOTIFY_CHANGE_LAST_WRITE);
  if (m_change == INVALID_HANDLE_VALUE) Log::Error("ipc: cannot watch %s (%lu)", ToUtf8(m_dir).c_str(), GetLastError());
  return m_change != INVALID_HANDLE_VALUE;
}

void Ipc::Shutdown() {
  if (m_change != INVALID_HANDLE_VALUE) FindCloseChangeNotification(m_change);
  m_change = INVALID_HANDLE_VALUE;
  DeleteFileW((m_dir + L"\\alive").c_str());
}

void Ipc::OnSignalled() {
  if (m_change != INVALID_HANDLE_VALUE) FindNextChangeNotification(m_change);
}

std::string Ipc::PollCommand() {
  std::wstring path = m_dir + L"\\command";
  std::string cmd;
  if (!ReadAll(path, cmd)) return "";
  cmd = Trim(cmd);
  if (cmd.empty()) return "";  // still being written; the next change notification brings the content
  DeleteFileW(path.c_str());
  Log::Info("ipc: command '%s'", cmd.c_str());
  return cmd;
}

void Ipc::Heartbeat(ULONGLONG now) {
  if (now - m_lastAlive < kHeartbeatMs) return;
  WriteAll(m_dir + L"\\alive", std::to_string((long long)_time64(nullptr)));
  m_lastAlive = now;
}

void Ipc::Send(const std::string& command) {
  std::wstring dir = DataDir();
  CreateDirectoryW(dir.c_str(), nullptr);
  WriteAll(dir + L"\\command", command);
}
