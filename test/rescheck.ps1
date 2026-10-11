# 分辨率/布局验证：逐个分辨率检查状态窗口位置 + 锁屏页布局，最后恢复原分辨率。
# 打的是**已安装**的实例（持久运行、写日志不受沙箱限制）。
# 用法: powershell -NoProfile -ExecutionPolicy Bypass -File rescheck.ps1 [-Modes '1280x720','1024x768'] [-KeepMode]
param(
    [string[]]$Modes = @('1600x900', '1280x720', '1024x768', '800x600', '640x480'),
    [switch]$KeepMode
)

$root  = 'D:\Work\ScreenTime'
$out   = Join-Path $root 'test\verify'
$dir   = Join-Path $env:LOCALAPPDATA 'ScreenTime'
$exe   = Join-Path $dir 'ScreenTime.exe'
$cfg   = Join-Path $dir 'config.ini'
$log   = Join-Path $dir 'logs\activity.log'
$setmode = Join-Path $root 'tools\setmode.exe'
$inject  = Join-Path $root 'tools\input.exe'
$info    = Join-Path $root 'tools\awtkinfo.exe'
New-Item -ItemType Directory -Force -Path $out | Out-Null

Add-Type -TypeDefinition @'
using System;
using System.Runtime.InteropServices;
public static class W {
  [DllImport("user32.dll", CharSet = CharSet.Unicode)] public static extern IntPtr FindWindowW(string c, IntPtr t);
  [DllImport("user32.dll")] public static extern bool PostMessageW(IntPtr h, uint m, IntPtr w, IntPtr l);
}
'@

$proc = Get-Process ScreenTime -ErrorAction SilentlyContinue | Select-Object -First 1
if (-not $proc) { Write-Host 'ScreenTime is not running'; exit 1 }
$targetPid = $proc.Id
Write-Host "target pid=$targetPid"

foreach ($m in $Modes) {
    $wh = $m -split 'x'
    $sw = [int]$wh[0]; $sh = [int]$wh[1]
    $lw = $sw / 1.25
    $lh = $sh / 1.25
    Write-Host "===== $m  (AWTK logical ${lw}x${lh}) ====="
    & $setmode $sw $sh | Write-Host
    Start-Sleep -Milliseconds 900

    # 让已运行实例把窗口显示出来（单实例分支发 WM_SHOWREQ）
    Start-Process -FilePath $exe -WorkingDirectory $dir -ArgumentList '--config', $cfg
    Start-Sleep -Seconds 2
    Write-Host '--- status window (physical) ---'
    & $info ScreenTime | Select-String 'SDL_app' -Context 0,5 | ForEach-Object { $_.Line; $_.Context.PostContext }
    & powershell -NoProfile -ExecutionPolicy Bypass -File (Join-Path $root 'test\shot.ps1') `
        (Join-Path $out "res_${m}_status.png") | Out-Null

    # 立刻锁屏（等同点界面上的「立即休息」）
    $bridge = [W]::FindWindowW('ScreenTimeStatus', [IntPtr]0)
    [void][W]::PostMessageW($bridge, 0x0111, [IntPtr]201, [IntPtr]0)
    Start-Sleep -Seconds 3
    Write-Host '--- lock window (physical) ---'
    & $info ScreenTime | Select-String 'SDL_app' -Context 0,5 | ForEach-Object { $_.Line; $_.Context.PostContext }
    & powershell -NoProfile -ExecutionPolicy Bypass -File (Join-Path $root 'test\shot.ps1') `
        (Join-Path $out "res_${m}_lock.png") | Out-Null

    # 输入密码解锁：编辑框水平居中，y = 0.62*H（不够高时退到 0.52*H）
    $ey = [math]::Round($lh * 0.62)
    if ($ey + 44 + 42 + 40 -gt $lh) { $ey = [math]::Round($lh * 0.52) }
    $clickX = [int]($lw / 2 * 1.25)
    $clickY = [int](($ey + 22) * 1.25)
    Write-Host "unlock click at $clickX,$clickY"
    & $inject focus $targetPid | Out-Null
    & $inject click $clickX $clickY | Out-Null
    & $inject type '642101' | Out-Null
    & $inject key 13 | Out-Null
    Start-Sleep -Seconds 2
    Write-Host '--- after unlock ---'
    & $info ScreenTime | Select-String 'SDL_app' -Context 0,5 | ForEach-Object { $_.Line; $_.Context.PostContext }
    & powershell -NoProfile -ExecutionPolicy Bypass -File (Join-Path $root 'test\shot.ps1') `
        (Join-Path $out "res_${m}_after.png") | Out-Null
    Get-Content $log -Tail 2 -Encoding UTF8 | Write-Host
}

if (-not $KeepMode) {
    Write-Host '===== restore ====='
    & $setmode restore | Write-Host
}
Write-Host 'done'
