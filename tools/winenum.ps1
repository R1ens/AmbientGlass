param([string]$ProcName = "vlc")
Add-Type @"
using System;
using System.Text;
using System.Runtime.InteropServices;
using System.Collections.Generic;
public static class WE {
  public delegate bool EnumProc(IntPtr h, IntPtr l);
  [DllImport("user32.dll")] public static extern bool EnumWindows(EnumProc cb, IntPtr l);
  [DllImport("user32.dll")] public static extern bool EnumChildWindows(IntPtr p, EnumProc cb, IntPtr l);
  [DllImport("user32.dll")] public static extern uint GetWindowThreadProcessId(IntPtr h, out uint pid);
  [DllImport("user32.dll", CharSet=CharSet.Unicode)] public static extern int GetClassName(IntPtr h, StringBuilder s, int n);
  [DllImport("user32.dll", CharSet=CharSet.Unicode)] public static extern int GetWindowText(IntPtr h, StringBuilder s, int n);
  [DllImport("user32.dll")] public static extern bool IsWindowVisible(IntPtr h);
  [DllImport("user32.dll")] public static extern bool GetWindowRect(IntPtr h, out RECT r);
  [DllImport("user32.dll")] public static extern IntPtr GetParent(IntPtr h);
  [DllImport("user32.dll")] public static extern long GetWindowLongPtr(IntPtr h, int i);
  [StructLayout(LayoutKind.Sequential)] public struct RECT { public int L, T, R, B; }
  static string Desc(IntPtr h, int depth) {
    var c = new StringBuilder(256); GetClassName(h, c, 256);
    var t = new StringBuilder(256); GetWindowText(h, t, 256);
    RECT r; GetWindowRect(h, out r);
    long ex = GetWindowLongPtr(h, -20);
    return new string(' ', depth*2) + String.Format("0x{0:X} vis={1} [{2},{3} {4}x{5}] ex=0x{6:X} class='{7}' title='{8}'",
      h.ToInt64(), IsWindowVisible(h) ? 1 : 0, r.L, r.T, r.R - r.L, r.B - r.T, ex, c, t);
  }
  public static List<string> Dump(uint[] pids) {
    var outp = new List<string>();
    var set = new HashSet<uint>(pids);
    EnumWindows((h, l) => {
      uint pid; GetWindowThreadProcessId(h, out pid);
      if (!set.Contains(pid) || !IsWindowVisible(h)) return true;
      outp.Add(Desc(h, 0));
      EnumChildWindows(h, (c, l2) => {
        int d = 1; IntPtr p = GetParent(c); while (p != IntPtr.Zero && p != h) { d++; p = GetParent(p); }
        outp.Add(Desc(c, d)); return true; }, IntPtr.Zero);
      return true; }, IntPtr.Zero);
    return outp;
  }
}
"@
$pids = @(Get-Process $ProcName -ErrorAction SilentlyContinue | ForEach-Object { [uint32]$_.Id })
if ($pids.Count -eq 0) { "no $ProcName process"; exit }
[WE]::Dump($pids) | ForEach-Object { $_ }
