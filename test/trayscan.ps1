# 扫描托盘"隐藏图标"浮出区的每个格子：悬停后抓提示气泡，拼成一张竖图便于一次查看。
# 用法: powershell -NoProfile -ExecutionPolicy Bypass -File trayscan.ps1
$root = 'D:\Work\ScreenTime'
$out  = Join-Path $root 'test\verify'
$inject = Join-Path $root 'tools\input.exe'
New-Item -ItemType Directory -Force -Path $out | Out-Null

Add-Type -AssemblyName System.Drawing

# 隐藏图标浮出区的 5 列 x 2 行（物理像素，1920x1080 / 125% 下量出来的）
$cols = 1546, 1589, 1630, 1674, 1718
$rows = 924, 971
$strip = @{ X = 1300; Y = 818; W = 620; H = 82 }

# 先把浮出区打开（点任务栏最右边的 ^ 箭头）
& $inject focus 0 2>$null | Out-Null
& $inject key 27 | Out-Null            # 保险：先 ESC 关掉可能已打开的浮出区
Start-Sleep -Milliseconds 300
& $inject click 1651 1055 | Out-Null
Start-Sleep -Milliseconds 1200

$shots = @()
$n = 0
foreach ($y in $rows) {
    foreach ($x in $cols) {
        $n++
        & $inject move $x $y | Out-Null
        Start-Sleep -Milliseconds 700
        $bmp = [System.Drawing.Bitmap]::new([int]$strip.W, [int]$strip.H)
        $g = [System.Drawing.Graphics]::FromImage($bmp)
        $g.CopyFromScreen([int]$strip.X, [int]$strip.Y, 0, 0, $bmp.Size)
        $g.Dispose()
        $shots += ,@($n, $bmp)
    }
}
& $inject key 27 | Out-Null            # 关闭浮出区
& $inject mouseoff | Out-Null

# 纵向拼成一张图
$cw = [int]$strip.W
$ch = [int]$strip.H + 4
$total = [System.Drawing.Bitmap]::new($cw, ($ch * $shots.Count))
$tg = [System.Drawing.Graphics]::FromImage($total)
$tg.Clear([System.Drawing.Color]::Black)
$font = [System.Drawing.Font]::new('Consolas', 12)
$idx = 0
foreach ($s in $shots) {
    $tg.DrawImage($s[1], 0, ($idx * $ch))
    $tg.DrawString("#$($s[0])", $font, [System.Drawing.Brushes]::Yellow, 4, ($idx * $ch) + 4)
    $s[1].Dispose()
    $idx++
}
$font.Dispose()
$tg.Dispose()
$file = Join-Path $out 'tray_scan.png'
$total.Save($file, [System.Drawing.Imaging.ImageFormat]::Png)
$total.Dispose()
Write-Output "saved $file"
& $inject mouseoff | Out-Null
