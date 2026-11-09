# =============================================================================
#  Build AWTK (git submodule at third_party\awtk) into out\awtk_<backend>.
#
#  -Backend gl    -> OpenGL build            (out\awtk_gl\awtk.dll)
#  -Backend soft  -> software renderer AGGE  (out\awtk_soft\awtk.dll, no OpenGL needed)
#
#  Build output goes into THIS repo (out\), never into the submodule, so
#  third_party\awtk only ever differs by the one-line CMake patch below.
#
#  The patch: AWTK's CMake port forgets to compile src/lcd/lcd_sdl2.c, which the
#  AGGE/software build needs (AWTK's SCons build does compile it). patches\
#  awtk-cmake-lcd-sdl2.patch adds it to _base_src; harmless for the GL build too.
#
#  This file is intentionally PURE ASCII (PowerShell 5.1 decodes BOM-less files
#  as ANSI).
#
#  Usage:
#    powershell -NoProfile -ExecutionPolicy Bypass -File .\build_awtk.ps1 -Backend gl
#    powershell -NoProfile -ExecutionPolicy Bypass -File .\build_awtk.ps1 -Backend soft -Reconfigure
# =============================================================================

[CmdletBinding()]
param(
    [ValidateSet('gl','soft')]
    [string]$Backend = 'gl',
    # Wipe the build directory and configure from scratch.
    [switch]$Reconfigure
)

$ErrorActionPreference = 'Stop'

$Here     = Split-Path -Parent $MyInvocation.MyCommand.Path
$AwtkRoot = Join-Path $Here 'third_party\awtk'
$BuildDir = Join-Path $Here ("out\awtk_" + $Backend)
$Patch    = Join-Path $Here 'patches\awtk-cmake-lcd-sdl2.patch'
$CMake    = 'D:\w64devkit\bin\cmake.exe'
$CC       = 'D:/w64devkit/bin/gcc.exe'
$CXX      = 'D:/w64devkit/bin/g++.exe'
$CmakeFile = Join-Path $AwtkRoot 'cmake\src_libs.cmake'

Write-Host '============================================================='
Write-Host (" Building AWTK ({0} backend)" -f $Backend)
Write-Host '============================================================='
Write-Host "  source : $AwtkRoot"
Write-Host "  build  : $BuildDir"

if (-not (Test-Path -LiteralPath (Join-Path $AwtkRoot 'CMakeLists.txt'))) {
    Write-Host "ERROR: AWTK submodule missing: $AwtkRoot" -ForegroundColor Red
    Write-Host '       run: git submodule update --init --recursive' -ForegroundColor Red
    exit 1
}
if (-not (Test-Path -LiteralPath $CMake)) {
    Write-Host "ERROR: cmake not found: $CMake" -ForegroundColor Red
    exit 1
}

# ---- 1. make sure the lcd_sdl2 CMake patch is applied (idempotent) ----------
$libsText = Get-Content -LiteralPath $CmakeFile -Raw
if ($libsText -notmatch 'lcd_sdl2\.c') {
    Write-Host ''
    Write-Host '==> applying patches\awtk-cmake-lcd-sdl2.patch'
    if (-not (Test-Path -LiteralPath $Patch)) {
        Write-Host "ERROR: patch missing: $Patch" -ForegroundColor Red
        exit 1
    }
    & git -C $AwtkRoot apply --ignore-whitespace $Patch
    if ($LASTEXITCODE -ne 0) {
        Write-Host 'ERROR: patch failed to apply' -ForegroundColor Red
        exit 1
    }
    $libsText = Get-Content -LiteralPath $CmakeFile -Raw
    if ($libsText -notmatch 'lcd_sdl2\.c') {
        Write-Host 'ERROR: patch applied but lcd_sdl2.c still not listed' -ForegroundColor Red
        exit 1
    }
    Write-Host '    OK: cmake/src_libs.cmake now builds src/lcd/lcd_sdl2.c' -ForegroundColor Green
} else {
    Write-Host '    cmake patch already applied'
}

# ---- 2. configure -----------------------------------------------------------
if ($Reconfigure -and (Test-Path -LiteralPath $BuildDir)) {
    Write-Host ("==> wiping {0} (-Reconfigure)" -f $BuildDir)
    Remove-Item -LiteralPath $BuildDir -Recurse -Force
}
if (-not (Test-Path -LiteralPath $BuildDir)) {
    New-Item -ItemType Directory -Force -Path $BuildDir | Out-Null
}

$nanovgBackend = 'GL3'
if ($Backend -eq 'soft') { $nanovgBackend = 'AGGE' }

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
    '-DAWTK_VGCANVAS=NANOVG_PLUS',
    ("-DAWTK_NANOVG_BACKEND=" + $nanovgBackend)
)

Write-Host ''
Write-Host '==> cmake configure'
Write-Host ("    $CMake " + ($configureArgs -join ' ')) -ForegroundColor DarkGray
& $CMake @configureArgs
if ($LASTEXITCODE -ne 0) {
    Write-Host "ERROR: cmake configure failed ($LASTEXITCODE)" -ForegroundColor Red
    exit $LASTEXITCODE
}

# ---- 3. build ---------------------------------------------------------------
Write-Host ''
Write-Host '==> cmake build (a few minutes)'
& $CMake --build $BuildDir
if ($LASTEXITCODE -ne 0) {
    Write-Host "ERROR: build failed ($LASTEXITCODE)" -ForegroundColor Red
    exit $LASTEXITCODE
}

# ---- 4. report --------------------------------------------------------------
Write-Host ''
$dll = Join-Path $BuildDir 'awtk.dll'
$lib = Join-Path $BuildDir 'libawtk.dll.a'
$sdl = Join-Path $BuildDir 'third_party\SDL2\SDL2.dll'
$missing = 0
foreach ($f in @($dll, $lib, $sdl)) {
    if (Test-Path -LiteralPath $f) {
        $item = Get-Item -LiteralPath $f
        Write-Host ("  OK: {0}  ({1} bytes)" -f $item.Name, $item.Length) -ForegroundColor Green
    } else {
        Write-Host ("  MISSING: {0}" -f $f) -ForegroundColor Red
        $missing++
    }
}
if ($missing -gt 0) { exit 1 }

Write-Host ''
Write-Host ("Done ({0}). Build the app against it with:" -f $Backend)
Write-Host ("  .\build.ps1 -Backend {0} -Out out\{0}\ScreenTime.exe" -f $Backend)
exit 0
