param([string]$NewPassword = '998877', [string]$ConfigPath = 'D:\Work\ScreenTime\config.ini')
Add-Type -Path D:\Work\ScreenTime\tools\windowtest.cs

function Get-PwLines([string]$path) {
    $out = @()
    foreach ($line in (Get-Content $path -Encoding UTF8)) {
        if ($line -match '^\s*(password|password_hash|password_salt|pw_iterations)\s*=') { $out += $line }
    }
    return $out
}

function Add-PlainPassword([string]$path, [string]$pw) {
    $attr = (Get-Item $path -Force).Attributes
    [System.IO.File]::SetAttributes($path, [System.IO.FileAttributes]::Normal)
    $lines = @(Get-Content $path -Encoding UTF8)
    $kept = @($lines | Where-Object { $_ -notmatch '^\s*password\s*=' })
    $new = @("password = $pw") + $kept
    [System.IO.File]::WriteAllLines($path, $new, (New-Object System.Text.UTF8Encoding($false)))
    [System.IO.File]::SetAttributes($path, $attr)
}

function Reload() {
    $s = [ST.Win]::Find()
    [void][ST.Win]::PostMessage($s[0], 0x0111, [IntPtr]202, [IntPtr]0)
    Start-Sleep -Milliseconds 3000
}

'--- password lines BEFORE ---'
Get-PwLines $ConfigPath

Add-PlainPassword $ConfigPath $NewPassword
'--- after inserting plaintext line ---'
Get-PwLines $ConfigPath

Reload
'--- after reload 1 (should re-derive, no plaintext left) ---'
Get-PwLines $ConfigPath

Reload
'--- after reload 2 (hash MUST stay identical: no silent reset) ---'
Get-PwLines $ConfigPath

'attrs now: ' + (Get-Item $ConfigPath -Force).Attributes
