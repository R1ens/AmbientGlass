param([int]$Rounds = 5)
# Checks that the glow overlay stays directly above VLC when focus moves away from VLC and back:
# makes VLC's window wider than the video (so there are black areas to glow in), then alternates the
# foreground between another window and VLC and reads the z-order right after each switch.
Add-Type @"
using System;
using System.Text;
using System.Runtime.InteropServices;
public static class Z {
  public delegate bool EnumProc(IntPtr h, IntPtr l);
  [DllImport("user32.dll")] public static extern bool EnumWindows(EnumProc cb, IntPtr l);
  [DllImport("user32.dll")] public static extern uint GetWindowThreadProcessId(IntPtr h, out uint pid);
  [DllImport("user32.dll", CharSet=CharSet.Unicode)] public static extern int GetClassName(IntPtr h, StringBuilder s, int n);
  [DllImport("user32.dll")] public static extern bool IsWindowVisible(IntPtr h);
  [DllImport("user32.dll")] public static extern IntPtr GetWindow(IntPtr h, uint cmd);
  [DllImport("user32.dll")] public static extern IntPtr GetForegroundWindow();
  [DllImport("user32.dll")] public static extern bool SetForegroundWindow(IntPtr h);
  [DllImport("user32.dll")] public static extern bool BringWindowToTop(IntPtr h);
  [DllImport("user32.dll")] public static extern bool SetWindowPos(IntPtr h, IntPtr a, int x, int y, int w, int hh, uint f);
  [DllImport("user32.dll")] public static extern bool AttachThreadInput(uint a, uint b, bool f);
  [DllImport("kernel32.dll")] public static extern uint GetCurrentThreadId();
  [DllImport("user32.dll")] public static extern void keybd_event(byte vk, byte scan, uint flags, UIntPtr extra);
  public static string Cls(IntPtr h) { var s = new StringBuilder(256); GetClassName(h, s, 256); return s.ToString(); }
  public static IntPtr Find(string procClassPrefix, uint pid) {
    IntPtr found = IntPtr.Zero;
    EnumWindows((h, l) => { uint p; GetWindowThreadProcessId(h, out p);
      if (p == pid && IsWindowVisible(h) && Cls(h).StartsWith(procClassPrefix)) { found = h; return false; } return true; }, IntPtr.Zero);
    return found;
  }
  public static void Activate(IntPtr h) {
    // Alt tap lets a background process move the foreground, like a user switching windows.
    keybd_event(0x12, 0, 0, UIntPtr.Zero); keybd_event(0x12, 0, 2, UIntPtr.Zero);
    SetForegroundWindow(h); BringWindowToTop(h);
  }
}
"@
$vlcProc = Get-Process vlc -ErrorAction Stop | Select-Object -First 1
$ovProc = Get-Process VlcAmbient -ErrorAction Stop | Select-Object -First 1
$vlc = [Z]::Find("Qt5QWindowIcon", [uint32]$vlcProc.Id)
$inner = [Z]::Find("VlcAmbientOverlay", [uint32]$ovProc.Id)
$other = [Z]::Find("VlcAmbientPanel", [uint32]$ovProc.Id)
if ($vlc -eq [IntPtr]::Zero -or $inner -eq [IntPtr]::Zero) { "vlc or overlay window not found"; exit 1 }
"owner of overlay is VLC: " + ([Z]::GetWindow($inner, 4) -eq $vlc)
$bad = 0
for ($i = 0; $i -lt $Rounds; $i++) {
  if ($other -ne [IntPtr]::Zero) { [Z]::Activate($other); Start-Sleep -Milliseconds 300 }
  [Z]::Activate($vlc)
  $prevNow = [Z]::GetWindow($vlc, 3)   # GW_HWNDPREV, read immediately after the switch
  $ok = $prevNow -eq $inner
  if (-not $ok) { $bad++ }
  "round {0}: foreground is VLC: {1}; window right above VLC is the overlay: {2}" -f $i, ([Z]::GetForegroundWindow() -eq $vlc), $ok
  Start-Sleep -Milliseconds 300
}
"rounds with VLC above the overlay right after the switch: $bad of $Rounds"
