# ScreenTime build script (MinGW-w64 / w64devkit)
param([string[]]$Extra = @())
$ErrorActionPreference = 'Stop'
$root = $PSScriptRoot

$gcc = 'gcc'
if (Get-Command gcc -ErrorAction SilentlyContinue) { $gcc = (Get-Command gcc).Source }
else { throw "gcc not found. Install MinGW-w64 (e.g. w64devkit)." }

$sources = (Get-ChildItem -Path (Join-Path $root 'src') -Filter '*.c' | ForEach-Object { $_.FullName })
$out = Join-Path $root 'ScreenTime.exe'

$flags = @(
    '-O2', '-std=gnu11', '-Wall', '-Wextra',
    '-DUNICODE', '-D_UNICODE',
    '-D_WIN32_WINNT=0x0601', '-D_WIN32_IE=0x0600',
    '-mwindows', '-static'
) + $Extra + $sources + @(
    '-o', $out,
    '-lgdiplus', '-lwinhttp', '-lgdi32', '-luser32',
    '-ladvapi32', '-lshell32', '-luuid', '-lbcrypt'
)

Write-Host "Compiling -> $out"
& $gcc @flags
if ($LASTEXITCODE -ne 0) { throw "Build failed (exit $LASTEXITCODE)" }
Write-Host "Done: $out"
