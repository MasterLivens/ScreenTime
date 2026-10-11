# 打开家长验证对话框，用 uidump.exe 打印几何信息，然后取消对话框
param([string]$Exe = "D:\Work\ScreenTime\tools\uidump.exe")

Add-Type -TypeDefinition @'
using System;
using System.Runtime.InteropServices;
public static class U {
    // title 用 IntPtr 传 0，避免 PowerShell 把 $null 变成空字符串导致匹配失败
    [DllImport("user32.dll", CharSet = CharSet.Unicode)] public static extern IntPtr FindWindowW(string cls, IntPtr title);
    [DllImport("user32.dll")] public static extern bool PostMessageW(IntPtr h, uint msg, IntPtr wp, IntPtr lp);
    [DllImport("user32.dll")] public static extern bool IsWindowVisible(IntPtr h);
}
'@

$status = [U]::FindWindowW("ScreenTimeStatus", [IntPtr]0)
if (-not $status) { Write-Host "no ScreenTimeStatus window"; exit 1 }
Write-Host ("status hwnd={0}" -f $status)

# WM_COMMAND = 0x0111，BTN_PARENT = 205
[void][U]::PostMessageW($status, 0x0111, [IntPtr]205, [IntPtr]0)
Start-Sleep -Milliseconds 900

$dlg = [U]::FindWindowW("ScreenTimeParentDlg", [IntPtr]0)
Write-Host ("parent dialog hwnd={0} visible={1}" -f $dlg, [U]::IsWindowVisible($dlg))

& $Exe ScreenTimeStatus ScreenTimeParentDlg

# 取消对话框（IDCANCEL = 2）
if ($dlg) { [void][U]::PostMessageW($dlg, 0x0111, [IntPtr]2, [IntPtr]0) }
