# Capture the full virtual screen to a PNG (physical pixels, DPI aware).
# Usage: shot.ps1 [outfile]
param([string]$Out = 'D:\Work\ScreenTime\logs\shot.png')

Add-Type -AssemblyName System.Drawing
Add-Type -AssemblyName System.Windows.Forms
Add-Type -Namespace Native -Name Dpi -MemberDefinition @'
[DllImport("user32.dll")] public static extern bool SetProcessDPIAware();
[DllImport("user32.dll")] public static extern bool SetProcessDpiAwarenessContext(IntPtr ctx);
'@
try { [Native.Dpi]::SetProcessDpiAwarenessContext([IntPtr](-4)) | Out-Null } catch {}
try { [Native.Dpi]::SetProcessDPIAware() | Out-Null } catch {}

$vs = [System.Windows.Forms.SystemInformation]::VirtualScreen
$w = [Math]::Abs($vs.Width); $h = [Math]::Abs($vs.Height)
$x = [Math]::Min($vs.X, $vs.X + $vs.Width)
$y = [Math]::Min($vs.Y, $vs.Y + $vs.Height)

$bmp = New-Object System.Drawing.Bitmap($w, $h)
$g = [System.Drawing.Graphics]::FromImage($bmp)
$g.CopyFromScreen($x, $y, 0, 0, $bmp.Size)
$g.Dispose()
$bmp.Save($Out, [System.Drawing.Imaging.ImageFormat]::Png)
$bmp.Dispose()
Write-Output "saved $Out (${w}x${h})"
