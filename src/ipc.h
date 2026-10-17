#pragma once
#include <windows.h>

#include <string>

// Talks to VLC's Lua extension and to second instances through files in the data folder:
//   exe_path - where this program is, UTF-8 (so the extension can launch it);
//   alive    - unix time, refreshed every few seconds while running;
//   command  - one word ("show", "hide", "toggle", "quit") written by a sender, consumed here.
class Ipc {
 public:
  static std::wstring DataDir();

  bool Init(const std::wstring& exePath);
  void Shutdown();

  // Signalled when the data folder changes; call OnSignalled() then PollCommand().
  HANDLE WaitHandle() const { return m_change; }
  void OnSignalled();
  std::string PollCommand();  // empty when there is none yet

  void Heartbeat(ULONGLONG now);

  // From another process: hands a command to the running instance.
  static void Send(const std::string& command);

 private:
  std::wstring m_dir;
  HANDLE m_change = INVALID_HANDLE_VALUE;
  ULONGLONG m_lastAlive = 0;
};
