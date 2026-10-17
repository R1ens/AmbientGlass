#include "vlc_tracker.h"

#include <dwmapi.h>
#include <cwchar>

namespace {

bool StartsWith(const wchar_t* s, const wchar_t* prefix) {
  return wcsncmp(s, prefix, wcslen(prefix)) == 0;
}

struct VideoWindows {
  HWND videoMain = nullptr;
  HWND videoOut = nullptr;
};

BOOL CALLBACK ChildProc(HWND hwnd, LPARAM lp) {
  auto* f = reinterpret_cast<VideoWindows*>(lp);
  if (!IsWindowVisible(hwnd)) return TRUE;
  wchar_t cls[256];
  GetClassNameW(hwnd, cls, 256);
  if (!f->videoMain && StartsWith(cls, L"VLC video main")) f->videoMain = hwnd;
  if (!f->videoOut && StartsWith(cls, L"VLC video output")) f->videoOut = hwnd;
  return TRUE;
}

bool IsCloaked(HWND hwnd) {
  DWORD cloaked = 0;
  return SUCCEEDED(DwmGetWindowAttribute(hwnd, DWMWA_CLOAKED, &cloaked, sizeof(cloaked))) && cloaked;
}

struct EnumCtx {
  class VlcTracker* self;
  VlcState* out;
};

}  // namespace

bool VlcTracker::IsVlc(DWORD pid) {
  auto it = m_pidCache.find(pid);
  if (it != m_pidCache.end()) return it->second;
  bool vlc = false;
  if (HANDLE p = OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION, FALSE, pid)) {
    wchar_t path[MAX_PATH * 2];
    DWORD n = ARRAYSIZE(path);
    if (QueryFullProcessImageNameW(p, 0, path, &n)) {
      const wchar_t* name = wcsrchr(path, L'\\');
      name = name ? name + 1 : path;
      vlc = _wcsicmp(name, L"vlc.exe") == 0;
    }
    CloseHandle(p);
  }
  m_pidCache[pid] = vlc;
  return vlc;
}

BOOL CALLBACK VlcTracker::TopLevelProc(HWND hwnd, LPARAM lp) {
  auto* ctx = reinterpret_cast<EnumCtx*>(lp);
  if (!IsWindowVisible(hwnd) || IsIconic(hwnd) || IsCloaked(hwnd)) return TRUE;
  DWORD pid = 0;
  GetWindowThreadProcessId(hwnd, &pid);
  if (!ctx->self->IsVlc(pid)) return TRUE;

  VideoWindows f;
  wchar_t cls[256];
  GetClassNameW(hwnd, cls, 256);
  if (StartsWith(cls, L"VLC video main")) f.videoMain = hwnd;  // video not embedded in the interface
  EnumChildWindows(hwnd, ChildProc, reinterpret_cast<LPARAM>(&f));
  if (!f.videoOut) return TRUE;
  if (!f.videoMain) f.videoMain = GetParent(f.videoOut);

  VlcState& s = *ctx->out;
  s.main = hwnd;
  s.videoMain = f.videoMain;
  s.videoOut = f.videoOut;
  s.pid = pid;
  if (FAILED(DwmGetWindowAttribute(hwnd, DWMWA_EXTENDED_FRAME_BOUNDS, &s.mainRect, sizeof(RECT))))
    GetWindowRect(hwnd, &s.mainRect);
  GetWindowRect(f.videoMain, &s.videoMainRect);
  GetWindowRect(f.videoOut, &s.pictureRect);
  IntersectRect(&s.pictureRect, &s.pictureRect, &s.videoMainRect);  // VLC zoom can push it past the video area
  MONITORINFO mi{sizeof(mi)};
  GetMonitorInfoW(MonitorFromWindow(hwnd, MONITOR_DEFAULTTONEAREST), &mi);
  s.monitorRect = mi.rcMonitor;
  s.fullscreen = s.mainRect.left <= mi.rcMonitor.left && s.mainRect.top <= mi.rcMonitor.top &&
                 s.mainRect.right >= mi.rcMonitor.right && s.mainRect.bottom >= mi.rcMonitor.bottom;
  return FALSE;  // EnumWindows walks top-down, so the first match is the top-most VLC
}

VlcState VlcTracker::Find() {
  ULONGLONG now = GetTickCount64();
  if (now - m_cacheTime > 10000) {  // pids get reused
    m_pidCache.clear();
    m_cacheTime = now;
  }
  VlcState s;
  EnumCtx ctx{this, &s};
  EnumWindows(TopLevelProc, reinterpret_cast<LPARAM>(&ctx));
  return s;
}
