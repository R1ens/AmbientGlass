#include "zorder.h"

#include <initializer_list>

static bool IsTopmost(HWND hwnd) { return (GetWindowLongPtrW(hwnd, GWL_EXSTYLE) & WS_EX_TOPMOST) != 0; }

void PlaceOverlays(HWND vlc, Overlay& inner, Overlay& outer) {
  if (!vlc) return;
  const UINT flags = SWP_NOMOVE | SWP_NOSIZE | SWP_NOACTIVATE | SWP_NOOWNERZORDER;
  const bool vlcTop = IsTopmost(vlc);
  for (Overlay* ov : {&outer, &inner})
    if (ov->Visible() && IsTopmost(ov->Hwnd()) != vlcTop)
      SetWindowPos(ov->Hwnd(), vlcTop ? HWND_TOPMOST : HWND_NOTOPMOST, 0, 0, 0, 0, flags);

  if (outer.Visible() && GetWindow(vlc, GW_HWNDNEXT) != outer.Hwnd())
    SetWindowPos(outer.Hwnd(), vlc, 0, 0, 0, 0, flags);
  if (inner.Visible()) {
    HWND prev = GetWindow(vlc, GW_HWNDPREV);
    if (prev != inner.Hwnd()) {
      HWND after = prev;
      if (!after || IsTopmost(after) != vlcTop) after = vlcTop ? HWND_TOPMOST : HWND_TOP;
      SetWindowPos(inner.Hwnd(), after, 0, 0, 0, 0, flags);
    }
  }
}
