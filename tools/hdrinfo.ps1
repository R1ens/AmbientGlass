# Prints, for every active display, whether Windows HDR (advanced color) is on and the SDR white level.
Add-Type @"
using System;
using System.Runtime.InteropServices;
public static class Hdr {
  [StructLayout(LayoutKind.Sequential)] public struct LUID { public uint Low; public int High; }
  [StructLayout(LayoutKind.Sequential)] public struct PATH {
    public LUID srcAdapter; public uint srcId; public uint srcMode; public uint srcStatus;
    public LUID tgtAdapter; public uint tgtId; public uint tgtMode; public uint tech; public uint rot; public uint scal;
    public uint rateN; public uint rateD; public uint scan; [MarshalAs(UnmanagedType.Bool)] public bool avail; public uint tgtStatus;
    public uint flags; }
  [StructLayout(LayoutKind.Sequential, Size = 64)] public struct MODE { public uint a; }
  [StructLayout(LayoutKind.Sequential)] public struct HDR { public uint type; public uint size; public LUID adapter; public uint id; }
  [StructLayout(LayoutKind.Sequential, CharSet = CharSet.Unicode)] public struct SRC { public HDR h; [MarshalAs(UnmanagedType.ByValTStr, SizeConst = 32)] public string name; }
  [StructLayout(LayoutKind.Sequential)] public struct COLOR { public HDR h; public uint value; public uint enc; public uint bpc; }
  [StructLayout(LayoutKind.Sequential)] public struct WHITE { public HDR h; public uint level; }
  [DllImport("user32.dll")] public static extern int GetDisplayConfigBufferSizes(uint f, out uint p, out uint m);
  [DllImport("user32.dll")] public static extern int QueryDisplayConfig(uint f, ref uint np, [Out] PATH[] p, ref uint nm, [Out] MODE[] m, IntPtr t);
  [DllImport("user32.dll")] public static extern int DisplayConfigGetDeviceInfo(ref SRC r);
  [DllImport("user32.dll")] public static extern int DisplayConfigGetDeviceInfo(ref COLOR r);
  [DllImport("user32.dll")] public static extern int DisplayConfigGetDeviceInfo(ref WHITE r);
  public static string Run() {
    uint np, nm; GetDisplayConfigBufferSizes(2, out np, out nm);
    var p = new PATH[np]; var m = new MODE[nm];
    QueryDisplayConfig(2, ref np, p, ref nm, m, IntPtr.Zero);
    var sb = new System.Text.StringBuilder();
    for (int i = 0; i < np; i++) {
      var s = new SRC(); s.h.type = 1; s.h.size = (uint)Marshal.SizeOf(typeof(SRC)); s.h.adapter = p[i].srcAdapter; s.h.id = p[i].srcId;
      DisplayConfigGetDeviceInfo(ref s);
      var c = new COLOR(); c.h.type = 9; c.h.size = (uint)Marshal.SizeOf(typeof(COLOR)); c.h.adapter = p[i].tgtAdapter; c.h.id = p[i].tgtId;
      DisplayConfigGetDeviceInfo(ref c);
      var w = new WHITE(); w.h.type = 11; w.h.size = (uint)Marshal.SizeOf(typeof(WHITE)); w.h.adapter = p[i].tgtAdapter; w.h.id = p[i].tgtId;
      DisplayConfigGetDeviceInfo(ref w);
      sb.AppendLine(String.Format("{0}: HDR supported={1} enabled={2} SDR white={3:0.00} ({4:0} nits)",
        s.name, (c.value & 1) != 0, (c.value & 2) != 0, w.level / 1000.0, w.level / 1000.0 * 80));
    }
    return sb.ToString();
  }
}
"@
[Hdr]::Run()
