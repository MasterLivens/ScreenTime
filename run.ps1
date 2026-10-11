# Run the workspace build of ScreenTime.exe.
#
# The workspace exe needs awtk.dll and SDL2.dll, which live in the AWTK build tree
# (referenced in place -- nothing is copied into this workspace), so this script just
# puts them on PATH first. The installed copy under %LOCALAPPDATA%\ScreenTime has the
# DLLs next to the exe and does not need this.
#
# Usage:
#   powershell -NoProfile -ExecutionPolicy Bypass -File run.ps1
#   powershell -NoProfile -ExecutionPolicy Bypass -File run.ps1 -Extra '--config','D:\path\my.ini'

param([string[]]$Extra = @())

$ErrorActionPreference = 'Stop'
. (Join-Path $PSScriptRoot 'awtk_env.ps1')

$exe = Join-Path $PSScriptRoot 'ScreenTime.exe'
if (-not (Test-Path $exe)) { throw "not built yet: $exe (run build.ps1 first)" }

$sdl = Join-Path $AwtkBuild 'third_party\SDL2'
$env:PATH = "$AwtkBuild;$sdl;$env:PATH"

Write-Host "running $exe"
Start-Process -FilePath $exe -WorkingDirectory $PSScriptRoot -ArgumentList $Extra
