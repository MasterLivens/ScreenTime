# =============================================================================
#  Build AWTK (git submodule at third_party/awtk) into out/awtk_<backend>.
#
#  -Backend gl    -> OpenGL build            (out/awtk_gl/awtk.dll|libawtk.so|libawtk.dylib)
#  -Backend soft  -> software renderer AGGE  (out/awtk_soft/..., no OpenGL needed)
#
#  Build output goes into THIS repo (out/), never into the submodule, so
#  third_party/awtk only ever differs by the one-line CMake patch below.
#
#  The patch: AWTK's CMake port forgets to compile src/lcd/lcd_sdl2.c, which the
#  AGGE/software build needs (AWTK's SCons build does compile it).
#
#  Portability: toolchain paths are resolved from PATH when the local
#  D:\w64devkit install is absent, so the same script works on the GitHub
#  runners (MSYS2 mingw64 / Ubuntu / macOS) -- see .github/workflows/build.yml.
#
#  This file is intentionally PURE ASCII (PowerShell 5.1 decodes BOM-less files
#  as ANSI).
#
#  Usage:
#    pwsh ./build_awtk.ps1 -Backend gl
#    pwsh ./build_awtk.ps1 -Backend soft -Reconfigure
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
$AwtkRoot = Join-Path (Join-Path $Here 'third_party') 'awtk'
$OutRoot  = Join-Path $Here 'out'
$BuildDir = Join-Path $OutRoot ("awtk_" + $Backend)
$Patch    = Join-Path (Join-Path $Here 'patches') 'awtk-cmake-lcd-sdl2.patch'
$CmakeFile = Join-Path (Join-Path $AwtkRoot 'cmake') 'src_libs.cmake'
$OnWindows = ($env:OS -eq 'Windows_NT')

# Toolchain: prefer the local w64devkit, otherwise use whatever is on PATH
# (GitHub runners provide cmake/ninja/gcc via MSYS2, apt or Xcode tools).
function Resolve-Tool {
    param([string]$Preferred, [string]$Name)
    if ($Preferred -and (Test-Path -LiteralPath $Preferred -PathType Leaf)) { return $Preferred }
    $cmd = Get-Command $Name -ErrorAction SilentlyContinue
    if ($cmd) { return $cmd.Source }
    throw ("{0} not found (looked for '{1}' and on PATH)" -f $Name, $Preferred)
}

$CMake = Resolve-Tool 'D:\w64devkit\bin\cmake.exe' 'cmake'
$CC    = Resolve-Tool 'D:\w64devkit\bin\gcc.exe'   'gcc'
$CXX   = Resolve-Tool 'D:\w64devkit\bin\g++.exe'   'g++'

Write-Host '============================================================='
Write-Host (" Building AWTK ({0} backend)" -f $Backend)
Write-Host '============================================================='
Write-Host "  source : $AwtkRoot"
Write-Host "  build  : $BuildDir"
Write-Host ("  tools  : cmake={0}" -f $CMake)
Write-Host ("           cc={0}  cxx={1}" -f $CC, $CXX)

if (-not (Test-Path -LiteralPath (Join-Path $AwtkRoot 'CMakeLists.txt'))) {
    Write-Host "ERROR: AWTK submodule missing: $AwtkRoot" -ForegroundColor Red
    Write-Host '       run: git submodule update --init --recursive' -ForegroundColor Red
    exit 1
}

# ---- 1. make sure the lcd_sdl2 CMake patch is applied (idempotent) ----------
$libsText = Get-Content -LiteralPath $CmakeFile -Raw
if ($libsText -notmatch 'lcd_sdl2\.c') {
    Write-Host ''
    Write-Host '==> applying patches/awtk-cmake-lcd-sdl2.patch'
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
if ($OnWindows) {
    $configureArgs += '-DAWTK_TOOLS_NAME=mingw'
}

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
$awtkNames = @('awtk.dll', 'libawtk.so', 'libawtk.dylib')
$awtkLib = Get-ChildItem -LiteralPath $BuildDir -File -ErrorAction SilentlyContinue |
           Where-Object { $awtkNames -contains $_.Name } |
           Select-Object -First 1
$sdlDir = Join-Path (Join-Path $BuildDir 'third_party') 'SDL2'
# prefer the runtime library (SDL2.dll / libSDL2*.so* / libSDL2*.dylib), not the import lib
$sdl = Get-ChildItem -LiteralPath $sdlDir -File -ErrorAction SilentlyContinue |
       Where-Object { $_.Name -match 'SDL2' -and ($_.Extension -in '.dll', '.dylib' -or $_.Name -like '*.so*') } |
       Select-Object -First 1

$missing = 0
if ($awtkLib) {
    Write-Host ("  OK: {0}  ({1} bytes)" -f $awtkLib.Name, $awtkLib.Length) -ForegroundColor Green
} else {
    Write-Host '  MISSING: awtk library (awtk.dll / libawtk.so / libawtk.dylib)' -ForegroundColor Red
    $missing++
}
if ($sdl) {
    Write-Host ("  OK: {0}  ({1} bytes)" -f $sdl.Name, $sdl.Length) -ForegroundColor Green
} else {
    Write-Host ("  WARN: no SDL2 library under {0}" -f $sdlDir) -ForegroundColor Yellow
}
if ($missing -gt 0) { exit 1 }

Write-Host ''
Write-Host ("Done ({0}). Build the app against it with:" -f $Backend)
Write-Host ("  ./build.ps1 -Backend {0} -Out out/{0}/ScreenTime.exe" -f $Backend)
exit 0
