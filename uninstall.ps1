# ScreenTime uninstall script
# Stops the process and removes the autostart entry.
# With -RemoveFiles the install dir (exe + awtk.dll + SDL2.dll + res\ + screenshots/ + logs/) is deleted too.
param(
    [string]$InstallDir = (Join-Path $env:LOCALAPPDATA 'ScreenTime'),
    [switch]$RemoveFiles
)

$ErrorActionPreference = 'SilentlyContinue'

Get-Process -Name ScreenTime -ErrorAction SilentlyContinue | Stop-Process -Force
Start-Sleep -Seconds 1

$runKey = 'HKCU:\Software\Microsoft\Windows\CurrentVersion\Run'
if (Test-Path $runKey) {
    $v = Get-ItemProperty -Path $runKey -Name 'ScreenTime' -ErrorAction SilentlyContinue
    if ($v) {
        Remove-ItemProperty -Path $runKey -Name 'ScreenTime' -ErrorAction SilentlyContinue
        Write-Host "Autostart entry 'ScreenTime' removed."
    } else {
        Write-Host "No autostart entry found."
    }
}

if ($RemoveFiles) {
    if (Test-Path $InstallDir) {
        Remove-Item -Recurse -Force $InstallDir
        Write-Host "Removed install dir: $InstallDir"
    }
} else {
    Write-Host "Install dir kept (screenshots / logs): $InstallDir"
    Write-Host "To remove everything: uninstall.ps1 -RemoveFiles"
}

Write-Host "Done."
