Add-Type -Path D:\Work\ScreenTime\tools\windowtest.cs

function Get-PwLine([string]$key) {
    $m = Select-String -Path D:\Work\ScreenTime\config.ini -Pattern $key -Encoding UTF8
    return $m.Line
}

'before:'; Get-PwLine 'password_hash'; Get-PwLine 'password_salt'

$status = [ST.Win]::Find()
[void][ST.Win]::PostMessage($status[0], 0x0111, [IntPtr]202, [IntPtr]0)   # 重载配置
Start-Sleep -Milliseconds 3000
'after reload 1:'; Get-PwLine 'password_hash'; Get-PwLine 'password_salt'

[void][ST.Win]::PostMessage($status[0], 0x0111, [IntPtr]202, [IntPtr]0)
Start-Sleep -Milliseconds 3000
'after reload 2:'; Get-PwLine 'password_hash'; Get-PwLine 'password_salt'

'--- log tail ---'
Get-Content D:\Work\ScreenTime\logs\activity.log -Tail 3 -Encoding UTF8
