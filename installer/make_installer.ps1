# =============================================================================
#  ScreenTime installer build script
# -----------------------------------------------------------------------------
#  Builds dist\ScreenTimeSetup-<version>.exe (NSIS) and, unless -SkipPortable is
#  given, dist\ScreenTime-<version>-portable.zip.
#
#  IMPORTANT: this file is intentionally PURE ASCII.
#  Windows PowerShell 5.1 decodes BOM-less files as ANSI, so non-ASCII text in
#  this script would be garbled and could break parsing. Keep all messages in
#  English; the Chinese UI text lives in ScreenTime.nsi (UTF-8, no BOM).
#
#  Usage:
#    powershell -NoProfile -ExecutionPolicy Bypass -File installer\make_installer.ps1
#    powershell -NoProfile -ExecutionPolicy Bypass -File installer\make_installer.ps1 -SkipPortable
# =============================================================================

[CmdletBinding()]
param(
    # Do not build the portable zip.
    [switch]$SkipPortable
)

$ErrorActionPreference = 'Stop'

# ---------------------------------------------------------------------------
# Configuration (absolute paths; no spaces in any of them)
# ---------------------------------------------------------------------------
$ScriptDir    = Split-Path -Parent $MyInvocation.MyCommand.Path
$ProjectRoot  = Split-Path -Parent $ScriptDir
$NsiFile      = Join-Path $ScriptDir  'ScreenTime.nsi'
$VersionFile  = Join-Path $ProjectRoot 'VERSION'
$DistDir      = Join-Path $ProjectRoot 'dist'

$MakensisExe  = 'D:\w64devkit\bin\makensis.exe'
$NsisShareDir = 'D:\w64devkit\share\nsis'
$SevenZipExe  = 'C:\Program Files\7-Zip\7z.exe'
$AwtkRoot     = 'D:\Work\AWTK\awtk-master'

$DefaultVersion = '1.1.0'
$ScaleDirs      = @('x1', 'x2', 'x3', 'x4')

# ---------------------------------------------------------------------------
# Helpers
# ---------------------------------------------------------------------------
function Write-Step {
    param([string]$Message)
    Write-Host ''
    Write-Host "==> $Message" -ForegroundColor Cyan
}

function Write-Ok {
    param([string]$Message)
    Write-Host "    OK: $Message" -ForegroundColor Green
}

function Stop-Build {
    param([string]$Message)
    Write-Host ''
    Write-Host "ERROR: $Message" -ForegroundColor Red
    exit 1
}

function Format-Arg {
    param([string]$Value)
    if ($Value -match '\s') { return '"' + $Value + '"' }
    return $Value
}

Write-Host '============================================================='
Write-Host ' ScreenTime installer build'
Write-Host '============================================================='

# ---------------------------------------------------------------------------
# 1. Tool checks
# ---------------------------------------------------------------------------
Write-Step 'Checking build tools'

if (-not (Test-Path -LiteralPath $MakensisExe -PathType Leaf)) {
    Stop-Build "makensis.exe not found: $MakensisExe"
}
Write-Ok "makensis: $MakensisExe"

if (Test-Path -LiteralPath $NsisShareDir -PathType Container) {
    # Make sure Include\ / Stubs\ / Plugins\ are found even if the environment
    # is odd; if the build still complains, add /NOCONFIG on the command line.
    $env:NSISDIR = $NsisShareDir
    Write-Ok "NSISDIR: $NsisShareDir"
} else {
    Write-Host "    WARN: NSIS share dir not found, relying on makensis defaults: $NsisShareDir" -ForegroundColor Yellow
}

$HaveSevenZip = Test-Path -LiteralPath $SevenZipExe -PathType Leaf
if ($HaveSevenZip) {
    Write-Ok "7-Zip: $SevenZipExe"
} else {
    Write-Host "    WARN: 7z.exe not found ($SevenZipExe) - content listing will be skipped" -ForegroundColor Yellow
}

if (-not (Test-Path -LiteralPath $NsiFile -PathType Leaf)) {
    Stop-Build "NSIS script not found: $NsiFile"
}

# ---------------------------------------------------------------------------
# 2. Version
# ---------------------------------------------------------------------------
Write-Step 'Reading version'

$Version = $DefaultVersion
if (Test-Path -LiteralPath $VersionFile -PathType Leaf) {
    $raw = Get-Content -LiteralPath $VersionFile -TotalCount 1
    if ($raw -and $raw.Trim()) {
        $Version = $raw.Trim()
    } else {
        Write-Host "    WARN: $VersionFile is empty, using $DefaultVersion" -ForegroundColor Yellow
    }
} else {
    Write-Host "    WARN: $VersionFile not found, using $DefaultVersion" -ForegroundColor Yellow
}

if ($Version -notmatch '^\d+(\.\d+){1,3}$') {
    Stop-Build "VERSION content is not a dotted version number: '$Version'"
}

# NSIS VIProductVersion needs exactly four numeric fields.
$verParts = @($Version.Split('.'))
while ($verParts.Count -lt 4) { $verParts += '0' }
$ProductVersion = ($verParts[0..3] -join '.')
Write-Ok "version: $Version (file version $ProductVersion)"

# ---------------------------------------------------------------------------
# 3. Payload source check
# ---------------------------------------------------------------------------
Write-Step 'Checking payload sources'

$missing = New-Object System.Collections.Generic.List[string]

$flatSources = @(
    @{ Path = (Join-Path $ProjectRoot 'ScreenTime.exe');                              Dest = 'ScreenTime.exe' },
    @{ Path = (Join-Path $AwtkRoot 'build_mingw\awtk.dll');                           Dest = 'awtk.dll' },
    @{ Path = (Join-Path $AwtkRoot 'build_mingw\third_party\SDL2\SDL2.dll');          Dest = 'SDL2.dll' },
    @{ Path = (Join-Path $AwtkRoot 'res\assets\default\raw\styles\default.bin');      Dest = 'res\assets\default\raw\styles\default.bin' }
)

foreach ($item in $flatSources) {
    if (Test-Path -LiteralPath $item.Path -PathType Leaf) {
        $sizeKb = [math]::Round((Get-Item -LiteralPath $item.Path).Length / 1KB, 1)
        Write-Ok ("{0} ({1} KB)  ->  {2}" -f $item.Path, $sizeKb, $item.Dest)
    } else {
        $missing.Add($item.Path)
    }
}

$cursorTotal = 0
foreach ($scale in $ScaleDirs) {
    $scaleDir = Join-Path $AwtkRoot ("res\assets\default\raw\images\" + $scale)
    $pngs = @()
    if (Test-Path -LiteralPath $scaleDir -PathType Container) {
        $pngs = @(Get-ChildItem -LiteralPath $scaleDir -Filter 'cursor*.png' -File -ErrorAction SilentlyContinue)
    }
    if ($pngs.Count -eq 0) {
        $missing.Add((Join-Path $scaleDir 'cursor*.png'))
    } else {
        $cursorTotal += $pngs.Count
        Write-Ok ("{0} cursor*.png  ->  res\assets\default\raw\images\{1}\" -f $pngs.Count, $scale)
    }
}

if ($missing.Count -gt 0) {
    Write-Host ''
    Write-Host 'Missing payload files:' -ForegroundColor Red
    foreach ($m in $missing) { Write-Host "  - $m" -ForegroundColor Red }
    Stop-Build ("$($missing.Count) required payload file(s) missing; build aborted")
}

# Never ship config.ini / screenshots / logs.
foreach ($forbidden in @('config.ini', 'config.dat')) {
    if (Test-Path -LiteralPath (Join-Path $ProjectRoot $forbidden) -PathType Leaf) {
        Write-Host "    NOTE: $forbidden exists in the project root but is NOT packaged" -ForegroundColor DarkGray
    }
}

# Optional installer icon: drop ScreenTime.ico next to the .nsi (or in the
# project root) and it is picked up automatically. Not required.
$IconFile = $null
foreach ($candidate in @((Join-Path $ScriptDir 'ScreenTime.ico'), (Join-Path $ProjectRoot 'ScreenTime.ico'))) {
    if (Test-Path -LiteralPath $candidate -PathType Leaf) { $IconFile = $candidate; break }
}
if ($IconFile) {
    Write-Ok "optional icon: $IconFile"
} else {
    Write-Host '    INFO: no ScreenTime.ico found, the installer keeps the default NSIS icon' -ForegroundColor DarkGray
}

# ---------------------------------------------------------------------------
# 4. Run makensis
# ---------------------------------------------------------------------------
Write-Step 'Compiling installer with makensis'

if (-not (Test-Path -LiteralPath $DistDir -PathType Container)) {
    New-Item -ItemType Directory -Force -Path $DistDir | Out-Null
}

$OutExe = Join-Path $DistDir "ScreenTimeSetup-$Version.exe"
if (Test-Path -LiteralPath $OutExe) {
    Remove-Item -LiteralPath $OutExe -Force
}

$makensisArgs = @(
    '/INPUTCHARSET', 'UTF8',
    "/DVERSION=$Version",
    "/DPRODUCT_VERSION=$ProductVersion",
    "/DPAYLOAD_SRC=$ProjectRoot",
    "/DAWTK_SRC=$AwtkRoot",
    "/DOUT_FILE=$OutExe"
)
if ($IconFile) { $makensisArgs += "/DICON_FILE=$IconFile" }
$makensisArgs += $NsiFile

$quotedArgs = ($makensisArgs | ForEach-Object { Format-Arg $_ }) -join ' '
Write-Host "    $MakensisExe $quotedArgs" -ForegroundColor DarkGray
Write-Host ''

& $MakensisExe @makensisArgs
$makensisExit = $LASTEXITCODE

if ($makensisExit -ne 0) {
    Stop-Build "makensis failed with exit code $makensisExit"
}
if (-not (Test-Path -LiteralPath $OutExe -PathType Leaf)) {
    Stop-Build "makensis reported success but $OutExe does not exist"
}
Write-Ok "makensis exit code 0"

# ---------------------------------------------------------------------------
# 5. Verify the produced package
# ---------------------------------------------------------------------------
Write-Step 'Verifying package contents (7z listing)'

if ($HaveSevenZip) {
    $sevenZipOutput = & $SevenZipExe l $OutExe 2>&1
    if ($LASTEXITCODE -ne 0) {
        Stop-Build "7z failed to list $OutExe (exit code $LASTEXITCODE)"
    }
    foreach ($line in $sevenZipOutput) { Write-Host "    $line" }

    # Parse the name column of the 7z file table (rows carrying an attribute
    # column of five dots are real entries; the summary line is not).
    $entryNames = New-Object System.Collections.Generic.List[string]
    foreach ($line in $sevenZipOutput) {
        $m = [regex]::Match([string]$line, '\.{5}\s+\d+\s+\d*\s*(\S.*?)\s*$')
        if ($m.Success) { $entryNames.Add($m.Groups[1].Value) }
    }

    $problems = New-Object System.Collections.Generic.List[string]

    $requiredEntries = @(
        'ScreenTime.exe',
        'awtk.dll',
        'SDL2.dll',
        'res\assets\default\raw\styles\default.bin'
    )
    foreach ($req in $requiredEntries) {
        if (-not ($entryNames -contains $req)) {
            $problems.Add("package is missing: $req")
        }
    }
    foreach ($scale in $ScaleDirs) {
        $hit = @($entryNames | Where-Object { $_ -like ("res\assets\default\raw\images\" + $scale + "\cursor*.png") })
        if ($hit.Count -eq 0) {
            $problems.Add("package is missing cursor*.png under res\assets\default\raw\images\$scale\")
        }
    }
    foreach ($bad in @('config.ini', 'config.dat')) {
        if (@($entryNames | Where-Object { $_ -like ("*" + $bad) }).Count -gt 0) {
            $problems.Add("package must not contain $bad")
        }
    }

    if ($problems.Count -gt 0) {
        Write-Host ''
        foreach ($p in $problems) { Write-Host "  - $p" -ForegroundColor Red }
        Stop-Build 'package content verification failed'
    }
    Write-Ok ("package lists {0} payload/plugin entries; all required files present" -f $entryNames.Count)
} else {
    Write-Host '    WARN: 7z unavailable, skipped package content verification' -ForegroundColor Yellow
}

# ---------------------------------------------------------------------------
# 6. Portable zip (optional but on by default)
# ---------------------------------------------------------------------------
$ZipPath = $null
if (-not $SkipPortable) {
    Write-Step 'Building portable zip'

    $ZipPath = Join-Path $DistDir "ScreenTime-$Version-portable.zip"
    $stage   = Join-Path $DistDir '_portable_stage'

    if (Test-Path -LiteralPath $stage) { Remove-Item -LiteralPath $stage -Recurse -Force }
    if (Test-Path -LiteralPath $ZipPath) { Remove-Item -LiteralPath $ZipPath -Force }

    $stageResRaw = Join-Path $stage 'res\assets\default\raw'
    New-Item -ItemType Directory -Force -Path $stage | Out-Null
    New-Item -ItemType Directory -Force -Path (Join-Path $stageResRaw 'styles') | Out-Null

    Copy-Item -LiteralPath (Join-Path $ProjectRoot 'ScreenTime.exe') -Destination $stage -Force
    Copy-Item -LiteralPath (Join-Path $AwtkRoot 'build_mingw\awtk.dll') -Destination $stage -Force
    Copy-Item -LiteralPath (Join-Path $AwtkRoot 'build_mingw\third_party\SDL2\SDL2.dll') -Destination $stage -Force
    Copy-Item -LiteralPath (Join-Path $AwtkRoot 'res\assets\default\raw\styles\default.bin') -Destination (Join-Path $stageResRaw 'styles') -Force

    foreach ($scale in $ScaleDirs) {
        $destDir = Join-Path $stageResRaw ("images\" + $scale)
        New-Item -ItemType Directory -Force -Path $destDir | Out-Null
        Copy-Item -Path (Join-Path $AwtkRoot ("res\assets\default\raw\images\" + $scale + '\cursor*.png')) -Destination $destDir -Force
    }

    Compress-Archive -Path (Join-Path $stage '*') -DestinationPath $ZipPath -CompressionLevel Optimal -Force
    Remove-Item -LiteralPath $stage -Recurse -Force

    if (-not (Test-Path -LiteralPath $ZipPath -PathType Leaf)) {
        Stop-Build "portable zip was not created: $ZipPath"
    }

    $zipEntries = @()
    Add-Type -AssemblyName System.IO.Compression.FileSystem
    $zipArchive = [System.IO.Compression.ZipFile]::OpenRead($ZipPath)
    try {
        $zipEntries = @($zipArchive.Entries | ForEach-Object { $_.FullName })
    } finally {
        $zipArchive.Dispose()
    }
    Write-Host ("    zip contains {0} entries:" -f $zipEntries.Count)
    foreach ($e in $zipEntries) { Write-Host "      $e" }
    Write-Ok "portable zip: $ZipPath"
}

# ---------------------------------------------------------------------------
# 7. Summary
# ---------------------------------------------------------------------------
Write-Step 'Build summary'

$exeItem = Get-Item -LiteralPath $OutExe
Write-Host ("    Installer : {0}" -f $exeItem.FullName)
Write-Host ("    Size      : {0} MB ({1} bytes)" -f [math]::Round($exeItem.Length / 1MB, 2), $exeItem.Length)
Write-Host ("    Version   : {0}" -f $Version)
Write-Host ("    Payload   : 4 root files + default.bin + {0} cursor png" -f $cursorTotal)
if ($ZipPath) {
    $zipItem = Get-Item -LiteralPath $ZipPath
    Write-Host ("    Portable  : {0}" -f $zipItem.FullName)
    Write-Host ("    Size      : {0} MB ({1} bytes)" -f [math]::Round($zipItem.Length / 1MB, 2), $zipItem.Length)
}

Write-Host ''
Write-Host 'Rerun command:' -ForegroundColor Cyan
Write-Host ("    $MakensisExe $quotedArgs")
Write-Host ''
Write-Host 'DONE' -ForegroundColor Green
exit 0
