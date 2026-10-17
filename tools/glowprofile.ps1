param([string]$Png, [int]$Y = 540, [int]$X0 = 0, [int]$X1 = 1200, [int]$Step = 20)
# Prints the luma (0..255) along one screen row of a screenshot, to measure how far the glow reaches.
Add-Type -AssemblyName System.Drawing
$bmp = New-Object System.Drawing.Bitmap $Png
$vals = @()
for ($x = $X0; $x -lt $X1; $x += $Step) {
  $c = $bmp.GetPixel($x, $Y)
  $vals += [int](0.2126 * $c.R + 0.7152 * $c.G + 0.0722 * $c.B)
}
$bmp.Dispose()
$first = -1
for ($i = 0; $i -lt $vals.Count; $i++) { if ($vals[$i] -gt 8) { $first = $X0 + $i * $Step; break } }
"glow starts at x=$first; luma: " + ($vals -join ' ')
