param([string]$Overrides, [string]$Out, [int[]]$Crop = $null, [int]$Scale = 1, [int]$Wait = 3)
# Restarts VlcAmbient with settings overrides ("key=value;key=value"), then takes a screenshot.
$ini = "$env:LOCALAPPDATA\VlcAmbient\settings.ini"
$pairs = $Overrides -split ';' | Where-Object { $_ -match '=' }
$keys = $pairs | ForEach-Object { ($_ -split '=')[0] }
$lines = @()
if (Test-Path $ini) { $lines = Get-Content $ini | Where-Object { $l = $_; -not ($keys | Where-Object { $l -match "^$_=" }) } }
($lines + $pairs) | Set-Content $ini -Encoding ascii
Get-Process VlcAmbient -ErrorAction SilentlyContinue | Stop-Process
Start-Sleep -Milliseconds 700
Start-Process "$PSScriptRoot\..\build\VlcAmbient.exe" -ArgumentList "--background"
Start-Sleep -Seconds $Wait
if ($Crop) { & "$PSScriptRoot\screenshot.ps1" -Out $Out -Scale $Scale -Crop $Crop } else { & "$PSScriptRoot\screenshot.ps1" -Out $Out -Scale $Scale }
