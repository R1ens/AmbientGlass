#include "tray.h"

#include <shellapi.h>

#include "resource.h"

bool TrayIcon::Create(HINSTANCE inst, HWND msgWnd, UINT callbackMsg) {
  m_inst = inst;
  m_wnd = msgWnd;
  m_msg = callbackMsg;
  if (!m_taskbarCreated) m_taskbarCreated = RegisterWindowMessageW(L"TaskbarCreated");
  NOTIFYICONDATAW nid{sizeof(nid)};
  nid.hWnd = msgWnd;
  nid.uID = 1;
  nid.uFlags = NIF_ICON | NIF_MESSAGE | NIF_TIP;
  nid.uCallbackMessage = callbackMsg;
  nid.hIcon = (HICON)LoadImageW(inst, MAKEINTRESOURCEW(IDI_APP), IMAGE_ICON, GetSystemMetrics(SM_CXSMICON),
                                GetSystemMetrics(SM_CYSMICON), 0);
  if (!nid.hIcon) nid.hIcon = LoadIconW(nullptr, IDI_APPLICATION);
  wcscpy_s(nid.szTip, L"VLC Ambient Light");
  return Shell_NotifyIconW(NIM_ADD, &nid) != FALSE;
}

void TrayIcon::Remove() {
  NOTIFYICONDATAW nid{sizeof(nid)};
  nid.hWnd = m_wnd;
  nid.uID = 1;
  Shell_NotifyIconW(NIM_DELETE, &nid);
}

TrayAction TrayIcon::OnMessage(LPARAM lp, bool glowEnabled) {
  switch (LOWORD(lp)) {
    case WM_LBUTTONUP:
      return TrayAction::TogglePanel;
    case WM_RBUTTONUP:
    case WM_CONTEXTMENU:
      return ShowMenu(glowEnabled);
  }
  return TrayAction::None;
}

TrayAction TrayIcon::ShowMenu(bool glowEnabled) {
  enum { kPanel = 1, kToggle, kQuit };
  HMENU menu = CreatePopupMenu();
  AppendMenuW(menu, MF_STRING, kPanel, L"Открыть настройки");
  AppendMenuW(menu, MF_STRING | (glowEnabled ? MF_CHECKED : 0), kToggle, L"Свечение включено");
  AppendMenuW(menu, MF_SEPARATOR, 0, nullptr);
  AppendMenuW(menu, MF_STRING, kQuit, L"Выход");
  SetMenuDefaultItem(menu, kPanel, FALSE);
  POINT pt;
  GetCursorPos(&pt);
  SetForegroundWindow(m_wnd);
  int cmd = TrackPopupMenu(menu, TPM_RETURNCMD | TPM_RIGHTBUTTON | TPM_NONOTIFY, pt.x, pt.y, 0, m_wnd, nullptr);
  PostMessageW(m_wnd, WM_NULL, 0, 0);  // lets the menu close properly when clicking elsewhere
  DestroyMenu(menu);
  if (cmd == kPanel) return TrayAction::OpenPanel;
  if (cmd == kToggle) return TrayAction::ToggleGlow;
  if (cmd == kQuit) return TrayAction::Quit;
  return TrayAction::None;
}
