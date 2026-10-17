#pragma once
#include <windows.h>
#include <unordered_map>

// Where VLC is drawing video right now. All rects are screen coordinates in physical pixels.
struct VlcState {
  HWND main = nullptr;       // top-level window (what we capture)
  HWND videoMain = nullptr;  // "VLC video main": the whole video area, VLC's own black bars included
  HWND videoOut = nullptr;   // "VLC video output": exactly the picture
  DWORD pid = 0;
  RECT mainRect{};           // visible window bounds (no invisible resize borders)
  RECT videoMainRect{};
  RECT pictureRect{};
  RECT monitorRect{};
  bool fullscreen = false;

  bool Active() const { return videoOut != nullptr; }
};

class VlcTracker {
 public:
  // Finds the top-most visible VLC window that is showing video.
  VlcState Find();

 private:
  bool IsVlc(DWORD pid);
  static BOOL CALLBACK TopLevelProc(HWND hwnd, LPARAM lp);

  std::unordered_map<DWORD, bool> m_pidCache;
  ULONGLONG m_cacheTime = 0;
};
