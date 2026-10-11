param([string]$Pass = '642101', [string]$LogPath = 'D:\Work\ScreenTime\logs\activity.log')
Add-Type -Path D:\Work\ScreenTime\tools\windowtest.cs

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
"status hwnd=$($status[0])"
[void][ST.Win]::PostMessage($status[0], 0x0111, [IntPtr]205, [IntPtr]0)   # WM_COMMAND / BTN_PARENT
Start-Sleep -Milliseconds 1200

$dlgList = [ST.Win]::FindClass('ScreenTimeParentDlg')
if (-not $dlgList) { 'parent dialog did not open'; exit 1 }
$dlg = $dlgList[0]
"dialog hwnd=$dlg  title=[$(TextOf $dlg)]"

$edit = [ST.Win]::GetDlgItem($dlg, 305)
"edit hwnd=$edit"
[void][ST.Win]::SetWindowText($edit, $Pass)
Start-Sleep -Milliseconds 200
"edit read-back=[$(TextOf $edit)]"

[void][ST.Win]::PostMessage($dlg, 0x0111, [IntPtr]1, [IntPtr]0)           # IDOK
Start-Sleep -Milliseconds 2000

"edit text AFTER IDOK=[$(TextOf ([ST.Win]::GetDlgItem($dlg, 305)))]"
$after = [ST.Win]::FindClass('ScreenTimeParentDlg')
"dialog after IDOK=$($after -join ',')"
if ($after) { "err text=[$(TextOf ([ST.Win]::GetDlgItem($dlg, 301)))]" }

'--- log tail ---'
Get-Content $LogPath -Tail 4 -Encoding UTF8
