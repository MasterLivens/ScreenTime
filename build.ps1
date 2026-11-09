# ScreenTime build script -- AWTK UI build (MinGW-w64 / w64devkit)
# AWTK is the git submodule third_party\awtk; both backends are built into out\ by build_awtk.ps1.
# Usage:
#   powershell -NoProfile -ExecutionPolicy Bypass -File build.ps1
#   powershell -NoProfile -ExecutionPolicy Bypass -File build.ps1 -Out <path\to\ScreenTime.exe>
#   powershell -NoProfile -ExecutionPolicy Bypass -File build.ps1 -Backend soft -Out out\soft\ScreenTime.exe
#       (soft = AWTK software renderer, runs on machines WITHOUT OpenGL; needs build_awtk.ps1 -Backend soft)
param(
    [string[]]$Extra = @(),
    [string]$Out = '',
    # 'gl' (default, GPU/OpenGL) or 'soft' (software renderer, no OpenGL needed)
    [ValidateSet('gl','soft')]
    [string]$Backend = 'gl'
)
$ErrorActionPreference = 'Stop'
$root = $PSScriptRoot

$env:AWTK_BACKEND = $Backend

. (Join-Path $root 'awtk_env.ps1')
if (-not (Test-Path $AwtkLib)) { throw "AWTK import library not found: $AwtkLib (build AWTK first, see README)" }
Write-Host ("AWTK backend : {0} ({1})" -f $AwtkBackend, $AwtkBuild)

$gcc = 'D:\w64devkit\bin\gcc.exe'
if (-not (Test-Path $gcc)) {
    if (Get-Command gcc -ErrorAction SilentlyContinue) {
        $gcc = (Get-Command gcc).Source
    } else {
        throw "gcc not found. Install MinGW-w64 (e.g. w64devkit)."
    }
}

$sources = (Get-ChildItem -Path (Join-Path $root 'src') -Filter '*.c' | ForEach-Object { $_.FullName })
if (-not $Out) { $Out = Join-Path $root 'ScreenTime.exe' }
$outDir = Split-Path -Parent $Out
if ($outDir -and -not (Test-Path $outDir)) { New-Item -ItemType Directory -Force -Path $outDir | Out-Null }

# Dev-only fallback for the AWTK resource root: this checkout's own submodule,
# so the built exe falls back to third_party\awtk\res when res\ is not next to it.
# NOTE: the \" escapes are required -- PowerShell would otherwise strip the quotes
# and gcc would see -DAWTK_RES_ROOT=D:/... (a bare token, not a C string).
$resRoot = (Join-Path $root 'third_party\awtk\res') -replace '\\', '/'
$resDefine = '-DAWTK_RES_ROOT=\"' + $resRoot + '\"'

$flags = @(
    '-O2', '-std=gnu11', '-Wall', '-Wextra',
    '-finput-charset=UTF-8', '-fexec-charset=UTF-8',
    '-DUNICODE', '-D_UNICODE',
    '-D_WIN32_WINNT=0x0601', '-D_WIN32_IE=0x0600',
    $resDefine,
    '-mwindows', '-static'
) + (Get-AwtkCFlags) + $Extra + $sources + @('-o', $out) + (Get-AwtkLinkFlags) + @(
    '-lgdiplus', '-lwinhttp', '-lgdi32', '-luser32',
    '-ladvapi32', '-lshell32', '-luuid', '-lbcrypt',
    '-lpdh', '-lole32'
)

Write-Host "Compiling -> $out"
& $gcc @flags
if ($LASTEXITCODE -ne 0) { throw "Build failed (exit $LASTEXITCODE)" }
Write-Host "Done: $out"
Write-Host "Runtime needs awtk.dll and SDL2.dll next to the exe (see install.ps1)."
