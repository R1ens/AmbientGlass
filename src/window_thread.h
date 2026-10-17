#pragma once
#include <windows.h>

#include <functional>
#include <thread>

// A thread that only owns windows and pumps their messages, never blocking on anything else.
// The overlay windows live here: an overlay owned by VLC's window shares input state with VLC's thread
// (Windows attaches the two), so it must belong to a thread that is always responsive, never to the one
// that renders or draws the panel.
class WindowThread {
 public:
  ~WindowThread() { Stop(); }
  bool Start();
  void Stop();

  // Runs `fn` on the window thread and waits for it (window creation and destruction must happen there).
  void Run(const std::function<void()>& fn);

 private:
  static LRESULT CALLBACK Proc(HWND, UINT, WPARAM, LPARAM);
  void Main(HANDLE ready);

  std::thread m_thread;
  HWND m_wnd = nullptr;  // message-only window that receives Run requests
};
