param([string]$Pass = '12345678', [string]$LogPath = 'D:\Work\ScreenTime\logs\activity.log')
Add-Type -Path D:\Work\ScreenTime\tools\windowtest.cs
Add-Type -AssemblyName System.Windows.Forms

function TextOf([IntPtr]$h) {
    if (-not $h) { return '' }
    $sb = New-Object System.Text.StringBuilder 512
    [void][ST.Win]::GetWindowText($h, $sb, 500)
    return $sb.ToString()
}

$old = [ST.Win]::FindClass('ScreenTimeParentDlg')
foreach ($o in $old) { [void][ST.Win]::PostMessage($o, 0x0010, [IntPtr]0, [IntPtr]0) }
if ($old) { Start-Sleep -Milliseconds 600 }

$status = [ST.Win]::Find()
if (-not $status) { 'no status window'; exit 1 }
[void][ST.Win]::PostMessage($status[0], 0x0111, [IntPtr]205, [IntPtr]0)
Start-Sleep -Milliseconds 1200

$dlgList = [ST.Win]::FindClass('ScreenTimeParentDlg')
if (-not $dlgList) { 'parent dialog did not open'; exit 1 }
$dlg = $dlgList[0]
$edit = [ST.Win]::GetDlgItem($dlg, 305)
"dialog hwnd=$dlg edit hwnd=$edit"

[void][ST.Win]::SetForegroundWindow($dlg)
Start-Sleep -Milliseconds 400
[void][ST.Win]::SetForegroundWindow($dlg)
Start-Sleep -Milliseconds 400

# real keyboard input into the focused edit
[System.Windows.Forms.SendKeys]::SendWait($Pass)
Start-Sleep -Milliseconds 300
"edit after typing=[$(TextOf $edit)]"

[void][ST.Win]::PostMessage($dlg, 0x0111, [IntPtr]1, [IntPtr]0)   # IDOK
Start-Sleep -Milliseconds 2000

"dialog after IDOK=$(([ST.Win]::FindClass('ScreenTimeParentDlg')) -join ',')"
'--- log tail ---'
Get-Content $LogPath -Tail 6 -Encoding UTF8
