# =============================================================================
#  Build AWTK with the SOFTWARE renderer (nanovg AGGE) -- for machines without
#  OpenGL 3 (virtual machines, RDP sessions, "Microsoft Basic Display Adapter").
#
#  The existing GL build (D:\Work\AWTK\awtk-master\build_mingw) is left untouched.
#  Output goes to <workspace>\out\awtk_soft\  (inside the workspace on purpose:
#  no admin rights / no writes outside the repo needed).
#
#  Usage:
#    powershell -NoProfile -ExecutionPolicy Bypass -File .\build_awtk_soft.ps1
#    powershell -NoProfile -ExecutionPolicy Bypass -File .\build_awtk_soft.ps1 -Reconfigure
#
#  This file is intentionally PURE ASCII (PowerShell 5.1 decodes BOM-less files
#  as ANSI).
# =============================================================================

[CmdletBinding()]
param(
    # Wipe the build directory and configure from scratch.
    [switch]$Reconfigure
)

$ErrorActionPreference = 'Stop'

$AwtkRoot  = 'D:\Work\AWTK\awtk-master'
$Here      = Split-Path -Parent $MyInvocation.MyCommand.Path
$BuildDir  = Join-Path $Here 'out\awtk_soft'
$CMake     = 'D:\w64devkit\bin\cmake.exe'
$CC        = 'D:/w64devkit/bin/gcc.exe'
$CXX       = 'D:/w64devkit/bin/g++.exe'

Write-Host '============================================================='
Write-Host ' Building AWTK (software renderer / nanovg AGGE)'
Write-Host '============================================================='
Write-Host "  source : $AwtkRoot"
Write-Host "  build  : $BuildDir"
Write-Host ''

if (-not (Test-Path -LiteralPath (Join-Path $AwtkRoot 'CMakeLists.txt'))) {
    Write-Host "ERROR: AWTK source not found: $AwtkRoot" -ForegroundColor Red
    exit 1
}
if (-not (Test-Path -LiteralPath $CMake)) {
    Write-Host "ERROR: cmake not found: $CMake" -ForegroundColor Red
    exit 1
}

if ($Reconfigure -and (Test-Path -LiteralPath $BuildDir)) {
    Write-Host '  wiping build directory (-Reconfigure)'
    Remove-Item -LiteralPath $BuildDir -Recurse -Force
}
if (-not (Test-Path -LiteralPath $BuildDir)) {
    New-Item -ItemType Directory -Force -Path $BuildDir | Out-Null
}

$configureArgs = @(
    '-S', $AwtkRoot,
    '-B', $BuildDir,
    '-G', 'Ninja',
    "-DCMAKE_C_COMPILER=$CC",
    "-DCMAKE_CXX_COMPILER=$CXX",
    '-DAWTK_TOOLS_NAME=mingw',
    '-DAWTK_DEBUG=OFF',
    '-DCMAKE_BUILD_TYPE=Release',
    '-DAWTK_BUILD_SHARED=ON',
    '-DAWTK_BUILD_DEMOS=OFF',
    '-DAWTK_BUILD_TESTS=OFF',
    '-DAWTK_BUILD_CMAKE_FIND_DEMO=ON',
    '-DCMAKE_POLICY_VERSION_MINIMUM=3.10',
    '-DDIRECTX=OFF',
    # ---- the only difference from the GL build ----
    '-DAWTK_VGCANVAS=NANOVG_PLUS',
    '-DAWTK_NANOVG_BACKEND=AGGE'
)

Write-Host '==> cmake configure'
Write-Host ("    $CMake " + ($configureArgs -join ' ')) -ForegroundColor DarkGray
& $CMake @configureArgs
if ($LASTEXITCODE -ne 0) {
    Write-Host "ERROR: cmake configure failed ($LASTEXITCODE)" -ForegroundColor Red
    exit $LASTEXITCODE
}

Write-Host ''
Write-Host '==> cmake build (this takes a few minutes)'
& $CMake --build $BuildDir
$buildRc = $LASTEXITCODE

$dll = Join-Path $BuildDir 'awtk.dll'
if (-not (Test-Path -LiteralPath $dll)) {
    # Known gap in AWTK's CMake port: src/lcd/lcd_sdl2.c is never compiled, so the
    # AGGE link fails on lcd_sdl2_init / lcd_sdl2_texture_reset. fix_awtk_soft_link.ps1
    # compiles that file and re-runs the same link with the extra object.
    Write-Host ''
    Write-Host '==> build did not produce awtk.dll, applying the lcd_sdl2 relink fix' -ForegroundColor Yellow
    & powershell -NoProfile -ExecutionPolicy Bypass -File (Join-Path $Here 'fix_awtk_soft_link.ps1')
    if ($LASTEXITCODE -ne 0) {
        Write-Host 'ERROR: relink fix failed' -ForegroundColor Red
        exit 1
    }
}

Write-Host ''
$lib = Join-Path $BuildDir 'libawtk.dll.a'
$sdl = Join-Path $BuildDir 'third_party\SDL2\SDL2.dll'
foreach ($f in @($dll, $lib, $sdl)) {
    if (Test-Path -LiteralPath $f) {
        $item = Get-Item -LiteralPath $f
        Write-Host ("  OK: {0}  ({1} bytes)" -f $item.FullName, $item.Length) -ForegroundColor Green
    } else {
        Write-Host ("  MISSING: {0}" -f $f) -ForegroundColor Red
    }
}
if (-not (Test-Path -LiteralPath $dll)) {
    Write-Host 'ERROR: awtk.dll was not produced' -ForegroundColor Red
    exit 1
}

Write-Host ''
Write-Host 'Done. Build the app against this with:'
Write-Host '  powershell -NoProfile -ExecutionPolicy Bypass -File .\build.ps1 -Backend soft -Out out\soft\ScreenTime.exe'
Write-Host 'and package it with:'
Write-Host '  powershell -NoProfile -ExecutionPolicy Bypass -File .\installer\make_installer.ps1 -Backend soft -PayloadExe out\soft\ScreenTime.exe'
exit 0
