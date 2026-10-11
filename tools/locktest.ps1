param([string]$Pass = '642101', [string]$LogPath = 'D:\Work\ScreenTime\logs\activity.log')
Add-Type -Path D:\Work\ScreenTime\tools\windowtest.cs
Add-Type -AssemblyName System.Windows.Forms

function TextOf([IntPtr]$h) {
    if (-not $h) { return '' }
    $sb = New-Object System.Text.StringBuilder 512
    [void][ST.Win]::GetWindowText($h, $sb, 500)
    return $sb.ToString()
}

# close leftover parent dialogs
$old = [ST.Win]::FindClass('ScreenTimeParentDlg')
foreach ($o in $old) { [void][ST.Win]::PostMessage($o, 0x0010, [IntPtr]0, [IntPtr]0) }
if ($old) { Start-Sleep -Milliseconds 500 }

$status = [ST.Win]::Find()
if (-not $status) { 'no status window'; exit 1 }

[void][ST.Win]::PostMessage($status[0], 0x0111, [IntPtr]201, [IntPtr]0)   # 立即休息
Start-Sleep -Milliseconds 1500

$lockList = [ST.Win]::FindClass('ScreenTimeLock')
if (-not $lockList) { 'lock window not found'; exit 1 }
$lock = $lockList[0]
"lock hwnd=$lock  $([ST.Win]::Dump($lock))"

[void][ST.Win]::SetForegroundWindow($lock)
Start-Sleep -Milliseconds 500
[void][ST.Win]::SetForegroundWindow($lock)
Start-Sleep -Milliseconds 500

[System.Windows.Forms.SendKeys]::SendWait($Pass)
Start-Sleep -Milliseconds 400
[System.Windows.Forms.SendKeys]::SendWait('{ENTER}')
Start-Sleep -Milliseconds 2500

$still = [ST.Win]::FindClass('ScreenTimeLock')
"lock after submit=$($still -join ',')"
$s2 = [ST.Win]::Find()
"status window after unlock: $([ST.Win]::Dump($s2[0]))"
'--- log tail ---'
Get-Content $LogPath -Tail 6 -Encoding UTF8
