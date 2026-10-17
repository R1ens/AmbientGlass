#include "window_thread.h"

#include "log.h"

namespace {
constexpr UINT WM_RUN = WM_APP + 1;
constexpr wchar_t kClass[] = L"VlcAmbientWindowThread";
}  // namespace

LRESULT CALLBACK WindowThread::Proc(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp) {
  if (msg == WM_RUN) {
    (*reinterpret_cast<const std::function<void()>*>(lp))();
    return 0;
  }
  return DefWindowProcW(hwnd, msg, wp, lp);
}

bool WindowThread::Start() {
  if (m_thread.joinable()) return true;
  HANDLE ready = CreateEventW(nullptr, TRUE, FALSE, nullptr);
  if (!ready) return false;
  m_thread = std::thread(&WindowThread::Main, this, ready);
  WaitForSingleObject(ready, INFINITE);
  CloseHandle(ready);
  if (!m_wnd) {
    m_thread.join();
    Log::Error("window thread: cannot create its message window");
    return false;
  }
  return true;
}

void WindowThread::Main(HANDLE ready) {
  // Above normal like the render thread: it answers VLC's input together with VLC's own thread.
  SetThreadPriority(GetCurrentThread(), THREAD_PRIORITY_ABOVE_NORMAL);
  WNDCLASSEXW wc{sizeof(wc)};
  wc.lpfnWndProc = Proc;
  wc.hInstance = GetModuleHandleW(nullptr);
  wc.lpszClassName = kClass;
  RegisterClassExW(&wc);
  m_wnd = CreateWindowExW(0, kClass, L"", 0, 0, 0, 0, 0, HWND_MESSAGE, nullptr, wc.hInstance, nullptr);
  SetEvent(ready);
  if (!m_wnd) return;
  MSG msg;
  while (GetMessageW(&msg, nullptr, 0, 0) > 0) {
    TranslateMessage(&msg);
    DispatchMessageW(&msg);
  }
}

void WindowThread::Run(const std::function<void()>& fn) {
  if (!m_wnd) {
    fn();
    return;
  }
  SendMessageW(m_wnd, WM_RUN, 0, reinterpret_cast<LPARAM>(&fn));
}

void WindowThread::Stop() {
  if (!m_thread.joinable()) return;
  HWND wnd = m_wnd;
  Run([wnd] {
    DestroyWindow(wnd);
    PostQuitMessage(0);
  });
  m_thread.join();
  m_wnd = nullptr;
}
