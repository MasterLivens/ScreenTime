# 构建 AWTK 冒烟测试程序（直接调用 gcc，引用 AWTK 源码树，不拷贝任何 AWTK 文件）
$ErrorActionPreference = 'Stop'
. "$PSScriptRoot\awtk_env.ps1"

if (-not (Test-Path $AwtkLib)) { throw "AWTK 库不存在: $AwtkLib" }

New-Item -ItemType Directory -Force -Path "$PSScriptRoot\build" | Out-Null

$cf = Get-AwtkCFlags
$lf = Get-AwtkLinkFlags

& D:\w64devkit\bin\gcc.exe -O2 -std=gnu11 -Wall -Wextra `
  -DUNICODE -D_UNICODE -D_WIN32_WINNT=0x0601 -D_WIN32_IE=0x0600 `
  $cf `
  -o "$PSScriptRoot\build\awtk_probe.exe" `
  "$PSScriptRoot\tools\awtk_probe.c" `
  $lf -static

Write-Host "gcc exit=$LASTEXITCODE"
