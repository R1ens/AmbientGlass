param([string]$Exe = "$PSScriptRoot\..\build\VlcAmbient.exe", [switch]$Update, [double]$MinPsnr = 45)
# Golden-image tests of the --render mode. Renders every case of tests\golden\cases.txt; -Update writes
# the results to tests\golden\expected\, otherwise they go to %TEMP%\vlcambient_golden\ and are compared
# with the expected images by PSNR over 8-bit RGBA. Exit code 1 when any case fails.
$ErrorActionPreference = "Stop"
$golden = Resolve-Path "$PSScriptRoot\..\tests\golden"
$expected = Join-Path $golden "expected"
$outDir = if ($Update) { $expected } else { Join-Path $env:TEMP "vlcambient_golden" }
New-Item -ItemType Directory -Force $outDir | Out-Null
if (-not (Test-Path $Exe)) { Write-Error "no exe at $Exe; run build.cmd first" }
$Exe = (Resolve-Path $Exe).Path

Add-Type -AssemblyName System.Drawing
Add-Type -ReferencedAssemblies System.Drawing -TypeDefinition @"
using System;
using System.Drawing;
using System.Drawing.Imaging;
using System.Runtime.InteropServices;
public static class GoldenPsnr {
  static byte[] Load(string path, out int w, out int h) {
    using (var bmp = new Bitmap(path)) {
      w = bmp.Width; h = bmp.Height;
      var data = bmp.LockBits(new Rectangle(0, 0, w, h), ImageLockMode.ReadOnly, PixelFormat.Format32bppArgb);
      var bytes = new byte[data.Stride * h];
      Marshal.Copy(data.Scan0, bytes, 0, bytes.Length);
      bmp.UnlockBits(data);
      return bytes;
    }
  }
  // PSNR in dB over all four 8-bit channels; +Infinity for identical images, -1 for a size mismatch.
  public static double Compare(string a, string b) {
    int wa, ha, wb, hb;
    var pa = Load(a, out wa, out ha); var pb = Load(b, out wb, out hb);
    if (wa != wb || ha != hb) return -1;
    double sum = 0;
    for (int i = 0; i < pa.Length; i++) { double d = (double)pa[i] - pb[i]; sum += d * d; }
    if (sum == 0) return double.PositiveInfinity;
    return 10 * Math.Log10(255.0 * 255.0 * pa.Length / sum);
  }
}
"@

$cases = Get-Content (Join-Path $golden "cases.txt") | Where-Object { $_ -match '\|' -and $_ -notmatch '^\s*#' }
$rows = @()
$failed = 0
foreach ($line in $cases) {
  $name, $argText = ($line -split '\|', 2) | ForEach-Object { $_.Trim() }
  $out = Join-Path $outDir "$name.png"
  if (Test-Path $out) { Remove-Item $out }
  $parts = @($argText -split '\s+')
  $args = @("--render", (Join-Path $golden $parts[0]), $out) + @($parts | Select-Object -Skip 1)
  $quoted = $args | ForEach-Object { '"' + $_ + '"' }
  $p = Start-Process -FilePath $Exe -ArgumentList $quoted -WorkingDirectory $golden -NoNewWindow -Wait -PassThru
  $status = "ok"; $psnr = ""
  if ($p.ExitCode -ne 0 -or -not (Test-Path $out)) {
    $status = "RENDER FAILED (exit $($p.ExitCode))"
  } elseif (-not $Update) {
    $ref = Join-Path $expected "$name.png"
    if (-not (Test-Path $ref)) {
      $status = "NO EXPECTED"
    } else {
      $db = [GoldenPsnr]::Compare($ref, $out)
      if ($db -lt 0) { $status = "SIZE MISMATCH" }
      else {
        $psnr = if ([double]::IsPositiveInfinity($db)) { "identical" } else { "{0:N1} dB" -f $db }
        if ($db -lt $MinPsnr) { $status = "FAIL" }
      }
    }
  }
  if ($status -ne "ok") { $failed++ }
  $rows += [pscustomobject]@{ Case = $name; Status = $status; PSNR = $psnr }
}
$rows | Format-Table -AutoSize | Out-String -Width 120 | Write-Host
$where = if ($Update) { "written to $expected" } else { "compared against $expected (threshold $MinPsnr dB)" }
Write-Host "$($rows.Count) cases, $failed failed; $where"
if ($failed -gt 0) { exit 1 }
exit 0
