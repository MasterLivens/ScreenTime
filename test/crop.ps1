# Crop a region out of a PNG and upscale it for inspection.
# Usage: crop.ps1 <in.png> <out.png> <x> <y> <w> <h> [scale]
param(
  [string]$In,
  [string]$Out,
  [int]$X, [int]$Y, [int]$W, [int]$H,
  [double]$Scale = 2.0
)
Add-Type -AssemblyName System.Drawing
$src = [System.Drawing.Image]::FromFile($In)
$ow = [int]($W * $Scale); $oh = [int]($H * $Scale)
$dst = New-Object System.Drawing.Bitmap($ow, $oh)
$g = [System.Drawing.Graphics]::FromImage($dst)
$g.InterpolationMode = [System.Drawing.Drawing2D.InterpolationMode]::NearestNeighbor
$g.PixelOffsetMode = [System.Drawing.Drawing2D.PixelOffsetMode]::Half
$g.DrawImage($src, (New-Object System.Drawing.Rectangle(0, 0, $ow, $oh)),
             (New-Object System.Drawing.Rectangle($X, $Y, $W, $H)),
             [System.Drawing.GraphicsUnit]::Pixel)
$g.Dispose()
$dst.Save($Out, [System.Drawing.Imaging.ImageFormat]::Png)
$dst.Dispose(); $src.Dispose()
Write-Output "saved $Out (${ow}x${oh})"
