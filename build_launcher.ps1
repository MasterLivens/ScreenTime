# =============================================================================
#  Build the ScreenTime launcher (launcher\launcher.c).
#
#  The launcher is a tiny, AWTK-free exe that probes OpenGL at startup and runs
#  either gl\ScreenTime.exe (AWTK OpenGL build) or soft\ScreenTime.exe (AWTK
#  software-renderer build). It needs no AWTK headers/libraries at all.
#
#  Output: out\launcher\ScreenTime.exe
#
#  This file is intentionally PURE ASCII.
# =============================================================================

$ErrorActionPreference = 'Stop'

$Here    = Split-Path -Parent $MyInvocation.MyCommand.Path
$Gcc     = 'D:\w64devkit\bin\gcc.exe'
$SrcFile = Join-Path $Here 'launcher\launcher.c'
$OutDir  = Join-Path $Here 'out\launcher'
$OutExe  = Join-Path $OutDir 'ScreenTime.exe'

if (-not (Test-Path -LiteralPath $SrcFile -PathType Leaf)) {
    Write-Host "ERROR: source not found: $SrcFile" -ForegroundColor Red
    exit 1
}
if (-not (Test-Path -LiteralPath $Gcc -PathType Leaf)) {
    Write-Host "ERROR: gcc not found: $Gcc" -ForegroundColor Red
    exit 1
}
if (-not (Test-Path -LiteralPath $OutDir)) {
    New-Item -ItemType Directory -Force -Path $OutDir | Out-Null
}

$flags = @(
    '-O2', '-std=gnu11', '-Wall', '-Wextra',
    '-finput-charset=UTF-8', '-fexec-charset=UTF-8',
    '-DUNICODE', '-D_UNICODE',
    '-D_WIN32_WINNT=0x0601',
    '-mwindows', '-municode', '-static',
    $SrcFile, '-o', $OutExe,
    '-lopengl32', '-lgdi32', '-luser32', '-lshell32', '-ladvapi32'
)

Write-Host "Compiling launcher -> $OutExe"
& $Gcc @flags
if ($LASTEXITCODE -ne 0) {
    Write-Host "ERROR: build failed ($LASTEXITCODE)" -ForegroundColor Red
    exit $LASTEXITCODE
}

$item = Get-Item -LiteralPath $OutExe
Write-Host ("Done: {0} ({1} bytes)" -f $item.FullName, $item.Length) -ForegroundColor Green
exit 0
