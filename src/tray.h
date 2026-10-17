#pragma once
#include <windows.h>

enum class TrayAction { None, TogglePanel, OpenPanel, ToggleGlow, Quit };

// The notification-area icon and its menu.
class TrayIcon {
 public:
  // `callbackMsg` is the message the icon sends to `msgWnd`; hand it to OnMessage().
  bool Create(HINSTANCE inst, HWND msgWnd, UINT callbackMsg);
  void Remove();
  // After Explorer restarts (the TaskbarCreated message) the icon must be added again.
  UINT TaskbarCreatedMessage() const { return m_taskbarCreated; }
  void Recreate() { Create(m_inst, m_wnd, m_msg); }

  TrayAction OnMessage(LPARAM lp, bool glowEnabled);

 private:
  TrayAction ShowMenu(bool glowEnabled);

  HINSTANCE m_inst = nullptr;
  HWND m_wnd = nullptr;
  UINT m_msg = 0, m_taskbarCreated = 0;
};
