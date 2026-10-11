# ScreenTime install script (AWTK UI build)
#   1. build ScreenTime.exe (needs gcc, e.g. D:\w64devkit\bin\gcc.exe, and a built AWTK tree)
#   2. copy exe + awtk.dll + SDL2.dll + the AWTK style resource to %LOCALAPPDATA%\ScreenTime
#   3. write the HKCU Run key so it starts at logon
#   4. start it now
#
# Runtime layout in the install dir (the AWTK resource root is the exe dir):
#   ScreenTime.exe
#   awtk.dll, SDL2.dll
#   res\assets\default\raw\styles\default.bin      (theme)
#   res\assets\default\raw\images\xN\cursor*.png   (mouse cursors, optional)
#   config.dat (created on first run; encrypted), screenshots\, logs\
#
# Usage:
#   powershell -ExecutionPolicy Bypass -File install.ps1
#   powershell -ExecutionPolicy Bypass -File install.ps1 -NoAutoStart   # no autostart
#   powershell -ExecutionPolicy Bypass -File install.ps1 -SkipBuild     # copy only
#   powershell -ExecutionPolicy Bypass -File install.ps1 -NoStart       # do not launch now

param(
    [string]$InstallDir = (Join-Path $env:LOCALAPPDATA 'ScreenTime'),
    [switch]$NoAutoStart,
    [switch]$SkipBuild,
    [switch]$NoStart
)

$ErrorActionPreference = 'Stop'
$src = $PSScriptRoot
$exeName = 'ScreenTime.exe'
$exe = Join-Path $src $exeName

if (-not $SkipBuild) {
    Write-Host "[1/5] building $exeName ..."
    & (Join-Path $src 'build.ps1')
    if ($LASTEXITCODE -ne 0) { throw "build failed (exit $LASTEXITCODE)" }
}
if (-not (Test-Path $exe)) { throw "$exeName not found: $exe" }

# --- AWTK runtime files (referenced in place, never copied into this workspace) ---
. (Join-Path $src 'awtk_env.ps1')
$awtkDll = Join-Path $AwtkBuild 'awtk.dll'
$sdlDll  = Join-Path $AwtkBuild 'third_party\SDL2\SDL2.dll'
$style   = Join-Path $AwtkRes   'assets\default\raw\styles\default.bin'
foreach ($f in @($awtkDll, $sdlDll, $style)) {
    if (-not (Test-Path $f)) { throw "AWTK runtime file missing: $f (build AWTK first, see README)" }
}

Write-Host "[2/5] installing to $InstallDir"
New-Item -ItemType Directory -Force -Path $InstallDir | Out-Null
Copy-Item $exe     (Join-Path $InstallDir $exeName) -Force
Copy-Item $awtkDll (Join-Path $InstallDir 'awtk.dll') -Force
Copy-Item $sdlDll  (Join-Path $InstallDir 'SDL2.dll') -Force

Write-Host '[3/5] installing AWTK resources (style + cursors, about 80 KB)'
$resDst = Join-Path $InstallDir 'res\assets\default\raw'
New-Item -ItemType Directory -Force -Path (Join-Path $resDst 'styles') | Out-Null
Copy-Item $style (Join-Path $resDst 'styles\default.bin') -Force
foreach ($x in @('x1', 'x2', 'x3', 'x4')) {
    $from = Join-Path $AwtkRes "assets\default\raw\images\$x"
    if (-not (Test-Path $from)) { continue }
    $to = Join-Path $resDst "images\$x"
    New-Item -ItemType Directory -Force -Path $to | Out-Null
    Get-ChildItem $from -Filter 'cursor*.png' -File |
        ForEach-Object { Copy-Item $_.FullName (Join-Path $to $_.Name) -Force }
}

# Config is not shipped any more: the app creates an ENCRYPTED config.dat on first run.
# A plaintext config.ini left by an older version is read once, re-saved as config.dat
# and renamed to config.ini.bak (it stops being the effective config).
$datPath = Join-Path $InstallDir 'config.dat'
if (Test-Path $datPath) {
    Write-Host '      existing config.dat kept'
} elseif (Test-Path (Join-Path $InstallDir 'config.ini')) {
    Write-Host '      plaintext config.ini found: it will be migrated to config.dat on first run'
} else {
    Write-Host '      no config yet: config.dat (encrypted, default password 642101) is created on first run'
}

if (-not $NoAutoStart) {
    Write-Host "[4/5] enabling autostart (HKCU\...\Run\ScreenTime)"
    $key = 'HKCU:\Software\Microsoft\Windows\CurrentVersion\Run'
    Set-ItemProperty -Path $key -Name 'ScreenTime' `
        -Value ('"' + (Join-Path $InstallDir $exeName) + '" --config "' + $datPath + '"')
} else {
    Write-Host '[4/5] autostart skipped (-NoAutoStart)'
}

if (-not $NoStart) {
    Write-Host '[5/5] starting ScreenTime'
    Start-Process -FilePath (Join-Path $InstallDir $exeName) `
        -ArgumentList '--config', $datPath `
        -WorkingDirectory $InstallDir
} else {
    Write-Host '[5/5] not started (-NoStart)'
}

Write-Host ''
Write-Host 'Done.'
Write-Host "  exe     : $(Join-Path $InstallDir $exeName)"
Write-Host "  runtime : awtk.dll, SDL2.dll, res\assets\default\raw\styles\default.bin"
Write-Host "  config  : $datPath  (encrypted; edit it from the in-app Parent Settings screen)"
Write-Host "  shots   : $(Join-Path $InstallDir 'screenshots')"
Write-Host "  logs    : $(Join-Path $InstallDir 'logs')"
Write-Host 'To stop for good: Task Manager -> End task -> ScreenTime.exe'
Write-Host 'To remove autostart: uninstall.ps1   (or Remove-ItemProperty HKCU:\...\Run -Name ScreenTime)'
