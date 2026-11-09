# =============================================================================
#  ScreenTime installer build script  (Inno Setup 7)
# -----------------------------------------------------------------------------
#  Builds dist\ScreenTimeSetup-<version>.exe (Inno Setup) and, unless
#  -SkipPortable is given, dist\ScreenTime-<version>-portable.zip.
#
#  IMPORTANT: this file is intentionally PURE ASCII.
#  Windows PowerShell 5.1 decodes BOM-less files as ANSI, so non-ASCII text in
#  this script would be garbled and could break parsing. Keep all messages in
#  English; the Chinese UI text lives in ScreenTime.iss (UTF-8 WITH BOM).
#
#  Usage:
#    powershell -NoProfile -ExecutionPolicy Bypass -File installer\make_installer.ps1
#    powershell -NoProfile -ExecutionPolicy Bypass -File installer\make_installer.ps1 -SkipPortable
# =============================================================================

[CmdletBinding()]
param(
    # Do not build the portable zip.
    [switch]$SkipPortable,
    # Optional: package this ScreenTime.exe instead of <project root>\ScreenTime.exe.
    # Useful when the exe was built elsewhere, e.g. build.ps1 -Out out\ScreenTime.exe
    [string]$PayloadExe = '',
    # Which AWTK build to package:
    #   'auto' (default) -> ONE package with both backends (gl\ and soft\) plus a tiny
    #                       launcher that probes OpenGL at startup and picks one.
    #   'gl'             -> single-backend package, GPU/OpenGL only
    #   'soft'           -> single-backend package, software renderer only
    #                       (output name gets a -soft suffix)
    [ValidateSet('auto','gl','soft')]
    [string]$Backend = 'auto'
)

$ErrorActionPreference = 'Stop'

# ---------------------------------------------------------------------------
# Configuration (absolute paths)
# ---------------------------------------------------------------------------
$ScriptDir    = Split-Path -Parent $MyInvocation.MyCommand.Path
$ProjectRoot  = Split-Path -Parent $ScriptDir
$IssFile      = Join-Path $ScriptDir  'ScreenTime.iss'
$VersionFile  = Join-Path $ProjectRoot 'VERSION'
$DistDir      = Join-Path $ProjectRoot 'dist'

if ($PayloadExe) {
    if (-not [System.IO.Path]::IsPathRooted($PayloadExe)) {
        $PayloadExe = Join-Path $ProjectRoot $PayloadExe
    }
} else {
    $PayloadExe = Join-Path $ProjectRoot 'ScreenTime.exe'
}
$PayloadDir   = Split-Path -Parent $PayloadExe

$IsccExe      = 'C:\Program Files\Inno Setup 7\ISCC.exe'
$SevenZipExe  = 'C:\Program Files\7-Zip\7z.exe'
# AWTK is the git submodule third_party\awtk; both backends are built into out\.
$AwtkRoot     = Join-Path $ProjectRoot 'third_party\awtk'

# 'auto' bundles BOTH backends + the launcher:
#   {app}\ScreenTime.exe        <- launcher (probes OpenGL, picks a backend)
#   {app}\gl\ScreenTime.exe     <- AWTK OpenGL build
#   {app}\soft\ScreenTime.exe   <- AWTK software-renderer build
$AutoBundle = ($Backend -eq 'auto')
$GlBuildDir = Join-Path $ProjectRoot 'out\awtk_gl'
$SoftBuildDir = Join-Path $ProjectRoot 'out\awtk_soft'

if ($AutoBundle) {
    # awtk_env.ps1 is not needed for the bundle; we use both build trees explicitly.
    $AwtkBuild = $GlBuildDir
    $AwtkBackend = 'auto(gl+soft)'
} else {
    # Which AWTK build provides awtk.dll / SDL2.dll: ask awtk_env.ps1 (single source of truth).
    $env:AWTK_BACKEND = $Backend
    . (Join-Path $ProjectRoot 'awtk_env.ps1')
}

$DefaultVersion = '1.1.9'
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

function Ensure-AppExe {
    param([string]$BuildBackend, [string]$Path)
    if (Test-Path -LiteralPath $Path -PathType Leaf) { return }
    Write-Host ("    building missing {0} backend exe ..." -f $BuildBackend) -ForegroundColor DarkGray
    & powershell -NoProfile -ExecutionPolicy Bypass -File (Join-Path $ProjectRoot 'build.ps1') -Backend $BuildBackend -Out $Path
    if ($LASTEXITCODE -ne 0) { Stop-Build ("build.ps1 -Backend {0} failed" -f $BuildBackend) }
}

# {app}\ScreenTime.exe (launcher) + {app}\gl\... + {app}\soft\...
function New-BundleStaging {
    param([string]$LauncherExe, [string]$GlExe, [string]$GlAwtk, [string]$SoftExe, [string]$SoftAwtk, [string]$OutDir)

    if (Test-Path -LiteralPath $OutDir) { Remove-Item -LiteralPath $OutDir -Recurse -Force }
    New-Item -ItemType Directory -Force -Path $OutDir | Out-Null
    Copy-Item -LiteralPath $LauncherExe -Destination $OutDir -Force

    $backends = @(
        @{ name = 'gl';   exe = $GlExe;   awtk = $GlAwtk },
        @{ name = 'soft'; exe = $SoftExe; awtk = $SoftAwtk }
    )
    foreach ($be in $backends) {
        $dir = Join-Path $OutDir $be.name
        $resRaw = Join-Path $dir 'res\assets\default\raw'
        New-Item -ItemType Directory -Force -Path (Join-Path $resRaw 'styles') | Out-Null
        Copy-Item -LiteralPath $be.exe -Destination $dir -Force
        Copy-Item -LiteralPath (Join-Path $be.awtk 'awtk.dll') -Destination $dir -Force
        Copy-Item -LiteralPath (Join-Path $be.awtk 'third_party\SDL2\SDL2.dll') -Destination $dir -Force
        Copy-Item -LiteralPath (Join-Path $AwtkRoot 'res\assets\default\raw\styles\default.bin') `
                  -Destination (Join-Path $resRaw 'styles') -Force
        foreach ($scale in $ScaleDirs) {
            $dst = Join-Path $resRaw ("images\" + $scale)
            New-Item -ItemType Directory -Force -Path $dst | Out-Null
            Copy-Item -Path (Join-Path $AwtkRoot ("res\assets\default\raw\images\" + $scale + '\cursor*.png')) `
                      -Destination $dst -Force
        }
    }
}

# ---------------------------------------------------------------------------
# 1. Tool checks
# ---------------------------------------------------------------------------
Write-Host '============================================================='
Write-Host ' ScreenTime installer build (Inno Setup 7)'
Write-Host '============================================================='

Write-Step 'Checking build tools'

if (-not (Test-Path -LiteralPath $IsccExe -PathType Leaf)) {
    Stop-Build "ISCC.exe not found: $IsccExe (install Inno Setup 7)"
}
Write-Ok "ISCC: $IsccExe"

$HaveSevenZip = Test-Path -LiteralPath $SevenZipExe -PathType Leaf
if ($HaveSevenZip) {
    Write-Ok "7-Zip: $SevenZipExe"
} else {
    Write-Host "    WARN: 7z.exe not found ($SevenZipExe) - portable zip uses Compress-Archive" -ForegroundColor Yellow
}

if (-not (Test-Path -LiteralPath $IssFile -PathType Leaf)) {
    Stop-Build "Inno script not found: $IssFile"
}
Write-Ok "script: $IssFile"

# The .iss carries Chinese text, so it must be UTF-8 WITH BOM or Inno Setup
# would decode it as ANSI and the wizard would show mojibake. If the BOM is
# missing (e.g. an editor rewrote the file), compile a BOM-normalized copy from
# the (always writable) build\ directory instead of failing.
$BuildDir = Join-Path $ProjectRoot 'build'
$issBytes = [System.IO.File]::ReadAllBytes($IssFile)
$hasBom = ($issBytes.Length -ge 3 -and $issBytes[0] -eq 0xEF -and $issBytes[1] -eq 0xBB -and $issBytes[2] -eq 0xBF)
if (-not $hasBom) {
    Write-Host '    WARN: .iss has no UTF-8 BOM; compiling a normalized copy' -ForegroundColor Yellow
    if (-not (Test-Path -LiteralPath $BuildDir -PathType Container)) {
        New-Item -ItemType Directory -Force -Path $BuildDir | Out-Null
    }
    $issNormalized = Join-Path $BuildDir 'ScreenTime.generated.iss'
    $issText = [System.IO.File]::ReadAllText($IssFile)
    [System.IO.File]::WriteAllText($issNormalized, $issText, (New-Object System.Text.UTF8Encoding($true)))
    $IssFile = $issNormalized
    Write-Ok "normalized copy: $IssFile"
} else {
    Write-Ok 'script encoding: UTF-8 with BOM'
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

# VersionInfoVersion needs exactly four numeric fields.
$verParts = @($Version.Split('.'))
while ($verParts.Count -lt 4) { $verParts += '0' }
$ProductVersion = ($verParts[0..3] -join '.')
Write-Ok "version: $Version (file version $ProductVersion)"

# The two builds install to the same place (same AppId) but get different file names
# so it is obvious which one you are looking at.
$OutBaseName = "ScreenTimeSetup-$Version"
$ZipBaseName = "ScreenTime-$Version-portable"
if ($Backend -eq 'soft') {
    $OutBaseName += '-soft'
    $ZipBaseName += '-soft'
}
Write-Ok "package name: $OutBaseName.exe (AWTK backend: $Backend)"

# ---------------------------------------------------------------------------
# 2b. Bundle mode: build missing exes and assemble the two-backend staging tree
# ---------------------------------------------------------------------------
$StagingDir = $null
if ($AutoBundle) {
    Write-Step 'Preparing bundle (launcher + gl + soft backends)'

    if (-not (Test-Path -LiteralPath (Join-Path $SoftBuildDir 'awtk.dll') -PathType Leaf)) {
        Stop-Build ("software AWTK build missing: {0}\awtk.dll (run .\build_awtk.ps1 -Backend soft first)" -f $SoftBuildDir)
    }
    if (-not (Test-Path -LiteralPath (Join-Path $GlBuildDir 'awtk.dll') -PathType Leaf)) {
        Stop-Build ("OpenGL AWTK build missing: {0}\awtk.dll" -f $GlBuildDir)
    }

    $GlExe    = Join-Path $ProjectRoot 'out\gl\ScreenTime.exe'
    $SoftExe  = Join-Path $ProjectRoot 'out\soft\ScreenTime.exe'
    $Launcher = Join-Path $ProjectRoot 'out\launcher\ScreenTime.exe'
    Ensure-AppExe 'gl'   $GlExe
    Ensure-AppExe 'soft' $SoftExe
    if (-not (Test-Path -LiteralPath $Launcher -PathType Leaf)) {
        Write-Host '    building missing launcher ...' -ForegroundColor DarkGray
        & powershell -NoProfile -ExecutionPolicy Bypass -File (Join-Path $ProjectRoot 'build_launcher.ps1')
        if ($LASTEXITCODE -ne 0) { Stop-Build 'build_launcher.ps1 failed' }
    }

    $StagingDir = Join-Path $DistDir '_bundle'
    New-BundleStaging -LauncherExe $Launcher -GlExe $GlExe -GlAwtk $GlBuildDir `
                      -SoftExe $SoftExe -SoftAwtk $SoftBuildDir -OutDir $StagingDir

    # mediaprobe.exe: on-site diagnostic tool for the video detection signals
    $ProbeExe = Join-Path $ProjectRoot 'out\mediaprobe.exe'
    $GccExe   = 'D:\w64devkit\bin\gcc.exe'
    if (-not (Test-Path -LiteralPath $ProbeExe -PathType Leaf)) {
        if (Test-Path -LiteralPath $GccExe -PathType Leaf) {
            Write-Host '    building mediaprobe.exe ...' -ForegroundColor DarkGray
            & $GccExe -O2 -std=gnu11 -DUNICODE -D_UNICODE -D_WIN32_WINNT=0x0601 `
                      "-I$(Join-Path $ProjectRoot 'src')" `
                      (Join-Path $ProjectRoot 'tools\mediaprobe.c') `
                      (Join-Path $ProjectRoot 'src\media.c') -o $ProbeExe `
                      -lole32 -luuid -lpdh -lshell32 -luser32 -lgdi32
        }
    }
    if (Test-Path -LiteralPath $ProbeExe -PathType Leaf) {
        Copy-Item -LiteralPath $ProbeExe -Destination $StagingDir -Force
    } else {
        Write-Host '    WARN: mediaprobe.exe not available, skipping' -ForegroundColor Yellow
    }

    Write-Ok ("staged bundle: {0} ({1} files)" -f $StagingDir, (Get-ChildItem $StagingDir -Recurse -File | Measure-Object).Count)

    $PayloadDir = $StagingDir
    $PayloadExe = Join-Path $StagingDir 'ScreenTime.exe'
}

# ---------------------------------------------------------------------------
# 3. Payload source check
# ---------------------------------------------------------------------------
Write-Step 'Checking payload sources'

$missing = New-Object System.Collections.Generic.List[string]

if ($AutoBundle) {
    $flatSources = @(
        @{ Path = (Join-Path $StagingDir 'ScreenTime.exe');                Dest = 'ScreenTime.exe (launcher)' },
        @{ Path = (Join-Path $StagingDir 'gl\ScreenTime.exe');             Dest = 'gl\ScreenTime.exe' },
        @{ Path = (Join-Path $StagingDir 'gl\awtk.dll');                   Dest = 'gl\awtk.dll' },
        @{ Path = (Join-Path $StagingDir 'gl\SDL2.dll');                   Dest = 'gl\SDL2.dll' },
        @{ Path = (Join-Path $StagingDir 'soft\ScreenTime.exe');           Dest = 'soft\ScreenTime.exe' },
        @{ Path = (Join-Path $StagingDir 'soft\awtk.dll');                 Dest = 'soft\awtk.dll' },
        @{ Path = (Join-Path $StagingDir 'soft\SDL2.dll');                 Dest = 'soft\SDL2.dll' },
        @{ Path = (Join-Path $StagingDir 'gl\res\assets\default\raw\styles\default.bin');   Dest = 'gl\res\...\default.bin' },
        @{ Path = (Join-Path $StagingDir 'soft\res\assets\default\raw\styles\default.bin'); Dest = 'soft\res\...\default.bin' }
    )
} else {
    $flatSources = @(
        @{ Path = $PayloadExe;                                                           Dest = 'ScreenTime.exe' },
        @{ Path = (Join-Path $AwtkBuild 'awtk.dll');                                      Dest = 'awtk.dll' },
        @{ Path = (Join-Path $AwtkBuild 'third_party\SDL2\SDL2.dll');                     Dest = 'SDL2.dll' },
        @{ Path = (Join-Path $AwtkRoot 'res\assets\default\raw\styles\default.bin');      Dest = 'res\assets\default\raw\styles\default.bin' }
    )
}

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

# Never ship config.ini / config.dat / screenshots / logs.
foreach ($forbidden in @('config.ini', 'config.dat')) {
    if (Test-Path -LiteralPath (Join-Path $ProjectRoot $forbidden) -PathType Leaf) {
        Write-Host "    NOTE: $forbidden exists in the project root but is NOT packaged" -ForegroundColor DarkGray
    }
}

# Optional installer icon: drop ScreenTime.ico next to the .iss and it is picked
# up automatically (the .iss checks for it with ISPP). Not required.
$IconFile = Join-Path $ScriptDir 'ScreenTime.ico'
if (Test-Path -LiteralPath $IconFile -PathType Leaf) {
    Write-Ok "optional icon: $IconFile"
} else {
    Write-Host '    INFO: no ScreenTime.ico in installer\, the setup keeps the default Inno icon' -ForegroundColor DarkGray
}

# ---------------------------------------------------------------------------
# 4. Run ISCC
# ---------------------------------------------------------------------------
Write-Step 'Compiling installer with ISCC'

if (-not (Test-Path -LiteralPath $DistDir -PathType Container)) {
    New-Item -ItemType Directory -Force -Path $DistDir | Out-Null
}

$OutExe = Join-Path $DistDir "$OutBaseName.exe"
if (Test-Path -LiteralPath $OutExe) {
    Remove-Item -LiteralPath $OutExe -Force
}

$isccArgs = @(
    "/DAppVersion=$Version",
    "/DAppVersion4=$ProductVersion",
    "/DPayloadDir=$PayloadDir",
    "/DAwtkDir=$AwtkRoot",
    "/DAwtkBuild=$AwtkBuild",
    "/O$DistDir",
    "/F$OutBaseName",
    $IssFile
)
if ($AutoBundle) {
    $isccArgs = @(
        "/DAppVersion=$Version",
        "/DAppVersion4=$ProductVersion",
        "/DPayloadDir=$PayloadDir",
        "/DAwtkDir=$AwtkRoot",
        "/DAwtkBuild=$AwtkBuild",
        "/DBundle=1",
        "/DBundleDir=$StagingDir",
        "/O$DistDir",
        "/F$OutBaseName",
        $IssFile
    )
}

Write-Host ("    `"$IsccExe`" {0}" -f ($isccArgs -join ' ')) -ForegroundColor DarkGray
Write-Host ''

$isccOutput = & $IsccExe @isccArgs 2>&1
$isccExit = $LASTEXITCODE
foreach ($line in $isccOutput) { Write-Host "    $line" }

if ($isccExit -ne 0) {
    Stop-Build "ISCC failed with exit code $isccExit"
}
if (-not (Test-Path -LiteralPath $OutExe -PathType Leaf)) {
    Stop-Build "ISCC reported success but $OutExe does not exist"
}
Write-Ok "ISCC exit code 0"

# ---------------------------------------------------------------------------
# 5. Verify what actually went into the package
# ---------------------------------------------------------------------------
# ISCC prints one "Compressing: <path>" line per packed file, so the compile
# output itself is a reliable inventory of the payload.
Write-Step 'Verifying packed payload (ISCC compile log)'

$packed = New-Object System.Collections.Generic.List[string]
foreach ($line in $isccOutput) {
    $m = [regex]::Match([string]$line, 'Compressing:\s+(.+?)\s*$')
    if ($m.Success) {
        $name = $m.Groups[1].Value
        $name = $name -replace '^\\\\\?\\', ''
        $packed.Add($name)
    }
}

$problems = New-Object System.Collections.Generic.List[string]
$packedText = ($packed -join "`n")

$requiredNames = @('ScreenTime.exe', 'awtk.dll', 'SDL2.dll', 'default.bin')
if ($AutoBundle) {
    $requiredNames += @('gl\awtk.dll', 'soft\awtk.dll', 'gl\SDL2.dll', 'soft\SDL2.dll')
}
foreach ($req in $requiredNames) {
    if ($packedText -notmatch [regex]::Escape($req)) { $problems.Add("package is missing: $req") }
}
foreach ($scale in $ScaleDirs) {
    if ($packedText -notmatch ("images\\" + $scale + "\\cursor")) {
        $problems.Add("package is missing cursor*.png under images\$scale\")
    }
}
foreach ($bad in @('config.ini', 'config.dat', 'activity.log')) {
    if ($packedText -match [regex]::Escape($bad)) { $problems.Add("package must not contain $bad") }
}

if ($problems.Count -gt 0) {
    Write-Host ''
    foreach ($p in $problems) { Write-Host "  - $p" -ForegroundColor Red }
    Stop-Build 'package content verification failed'
}
Write-Ok ("{0} file(s) packed; all required payload present" -f $packed.Count)

# ---------------------------------------------------------------------------
# 6. Portable zip (optional but on by default)
# ---------------------------------------------------------------------------
$ZipPath = $null
if (-not $SkipPortable) {
    Write-Step 'Building portable zip'

    $ZipPath = Join-Path $DistDir "$ZipBaseName.zip"
    $stage   = Join-Path $DistDir '_portable_stage'

    if (Test-Path -LiteralPath $stage) { Remove-Item -LiteralPath $stage -Recurse -Force }
    if (Test-Path -LiteralPath $ZipPath) { Remove-Item -LiteralPath $ZipPath -Force }

    if ($AutoBundle) {
        # The bundle staging tree IS the portable layout (launcher + gl\ + soft\).
        Compress-Archive -Path (Join-Path $StagingDir '*') -DestinationPath $ZipPath -CompressionLevel Optimal -Force
    } else {
        $stageResRaw = Join-Path $stage 'res\assets\default\raw'
        New-Item -ItemType Directory -Force -Path $stage | Out-Null
        New-Item -ItemType Directory -Force -Path (Join-Path $stageResRaw 'styles') | Out-Null

        Copy-Item -LiteralPath $PayloadExe -Destination $stage -Force
        Copy-Item -LiteralPath (Join-Path $AwtkBuild 'awtk.dll') -Destination $stage -Force
        Copy-Item -LiteralPath (Join-Path $AwtkBuild 'third_party\SDL2\SDL2.dll') -Destination $stage -Force
        Copy-Item -LiteralPath (Join-Path $AwtkRoot 'res\assets\default\raw\styles\default.bin') -Destination (Join-Path $stageResRaw 'styles') -Force

        foreach ($scale in $ScaleDirs) {
            $destDir = Join-Path $stageResRaw ("images\" + $scale)
            New-Item -ItemType Directory -Force -Path $destDir | Out-Null
            Copy-Item -Path (Join-Path $AwtkRoot ("res\assets\default\raw\images\" + $scale + '\cursor*.png')) -Destination $destDir -Force
        }

        Compress-Archive -Path (Join-Path $stage '*') -DestinationPath $ZipPath -CompressionLevel Optimal -Force
        Remove-Item -LiteralPath $stage -Recurse -Force
    }

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
Write-Host ("    `"$IsccExe`" {0}" -f ($isccArgs -join ' '))
Write-Host ''
Write-Host 'DONE' -ForegroundColor Green
exit 0
