param([string]$Out = "shot.png", [int]$Scale = 2, [int[]]$Crop = $null)  # Crop = x, y, w, h in screen px
# Saves the primary monitor (including layered/composited windows) to a PNG, downscaled by $Scale.
Add-Type -AssemblyName System.Windows.Forms, System.Drawing
Add-Type @"
using System;
using System.Runtime.InteropServices;
public static class Shot {
  [DllImport("user32.dll")] public static extern bool SetProcessDpiAwarenessContext(IntPtr v);
  [DllImport("user32.dll")] public static extern IntPtr GetDC(IntPtr h);
  [DllImport("user32.dll")] public static extern int ReleaseDC(IntPtr h, IntPtr dc);
  [DllImport("gdi32.dll")] public static extern bool BitBlt(IntPtr dst, int x, int y, int w, int h, IntPtr src, int sx, int sy, uint rop);
}
"@
[Shot]::SetProcessDpiAwarenessContext([IntPtr]-4) | Out-Null
$b = [System.Windows.Forms.Screen]::PrimaryScreen.Bounds
if ($Crop) { $b = New-Object System.Drawing.Rectangle $Crop[0], $Crop[1], $Crop[2], $Crop[3] }
$bmp = New-Object System.Drawing.Bitmap $b.Width, $b.Height
$g = [System.Drawing.Graphics]::FromImage($bmp)
$dst = $g.GetHdc()
$src = [Shot]::GetDC([IntPtr]::Zero)
[Shot]::BitBlt($dst, 0, 0, $b.Width, $b.Height, $src, $b.X, $b.Y, 0x40CC0020) | Out-Null  # SRCCOPY | CAPTUREBLT
[Shot]::ReleaseDC([IntPtr]::Zero, $src) | Out-Null
$g.ReleaseHdc($dst)
$small = New-Object System.Drawing.Bitmap ([int]($b.Width / $Scale)), ([int]($b.Height / $Scale))
$g2 = [System.Drawing.Graphics]::FromImage($small)
$g2.InterpolationMode = [System.Drawing.Drawing2D.InterpolationMode]::HighQualityBicubic
$g2.DrawImage($bmp, 0, 0, $small.Width, $small.Height)
$small.Save($Out, [System.Drawing.Imaging.ImageFormat]::Png)
"saved $Out ($($small.Width)x$($small.Height))"
