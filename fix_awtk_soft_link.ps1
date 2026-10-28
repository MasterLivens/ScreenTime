# =============================================================================
#  Relink the software-rendering awtk.dll with AWTK's SDL2 LCD source.
#
#  Why this exists:
#    AWTK's CMake port never adds src/lcd/lcd_sdl2.c to the build. With
#    AWTK_NANOVG_BACKEND=AGGE (software renderer, needed on machines without
#    OpenGL) the final link therefore fails with
#        undefined reference to `lcd_sdl2_init'
#        undefined reference to `lcd_sdl2_texture_reset'
#    AWTK's own SCons build compiles that file, so the CMake port is simply
#    incomplete for this combination.
#
#  Instead of editing the AWTK tree, we compile lcd_sdl2.c ourselves (using the
#  exact DEFINES/INCLUDES that the AGGE build used) and re-run the otherwise
#  identical link command with that extra object added. Everything stays inside
#  this workspace (out\awtk_soft).
#
#  Called automatically by build_awtk_soft.ps1; can also be run on its own.
#  This file is intentionally PURE ASCII.
# =============================================================================

$ErrorActionPreference = 'Stop'

$Here     = Split-Path -Parent $MyInvocation.MyCommand.Path
$AwtkRoot = 'D:\Work\AWTK\awtk-master'
$BuildDir = Join-Path $Here 'out\awtk_soft'
$BuildLog = Join-Path $Here 'out\awtk_soft_build.log'
$Gcc      = 'D:\w64devkit\bin\gcc.exe'
$Ninja    = Join-Path $BuildDir 'build.ninja'
$SrcFile  = Join-Path $AwtkRoot 'src\lcd\lcd_sdl2.c'
$ObjFile  = Join-Path $BuildDir 'lcd_sdl2.obj'

Write-Host '==> reading DEFINES / INCLUDES from build.ninja'
$defLine = $null
$incLine = $null
$lines = Get-Content -LiteralPath $Ninja
for ($i = 0; $i -lt $lines.Count; $i++) {
    if ($lines[$i] -match '^build CMakeFiles/awtk_base\.dir/') {
        for ($j = $i + 1; $j -lt $lines.Count; $j++) {
            if ($lines[$j] -notmatch '^\s') { break }
            if (-not $defLine -and $lines[$j] -match '^\s+DEFINES = (.*)$') { $defLine = $matches[1] }
            if (-not $incLine -and $lines[$j] -match '^\s+INCLUDES = (.*)$') { $incLine = $matches[1] }
        }
        if ($defLine -and $incLine) { break }
    }
}
if (-not $defLine -or -not $incLine) { throw 'could not parse DEFINES/INCLUDES from build.ninja' }
$defs = @($defLine -split '\s+' | Where-Object { $_ })
$incs = @($incLine -split '\s+' | Where-Object { $_ })
Write-Host ("    {0} defines, {1} include dirs" -f $defs.Count, $incs.Count)
if (($defs -join ' ') -notmatch 'WITH_NANOVG_SOFT') {
    throw 'WITH_NANOVG_SOFT not found -- this does not look like the AGGE build'
}

Write-Host '==> compiling lcd_sdl2.c'
$gccArgs = @('-O3', '-c', $SrcFile, '-o', $ObjFile) + $incs + $defs
& $Gcc @gccArgs
if ($LASTEXITCODE -ne 0) { throw "gcc failed ($LASTEXITCODE)" }
Write-Host ("    OK: {0} ({1} bytes)" -f $ObjFile, (Get-Item -LiteralPath $ObjFile).Length)

Write-Host '==> relinking awtk.dll '
$link = Get-Content -LiteralPath $BuildLog |
        Where-Object { $_ -match 'cmd\.exe /C "cd \. && D:\\w64devkit\\bin\\g\+\+\.exe' } |
        Select-Object -First 1
if (-not $link) {
    Write-Host '    no link command in the build log; building awtk on its own' -ForegroundColor DarkGray
    & 'D:\w64devkit\bin\cmake.exe' --build $BuildDir --target awtk 2>&1 |
        Where-Object { $_ -match 'cmd\.exe /C "cd \. && D:\\w64devkit\\bin\\g\+\+\.exe' } |
        Set-Content -LiteralPath $BuildLog -Encoding ASCII
    $link = Get-Content -LiteralPath $BuildLog |
            Where-Object { $_ -match 'cmd\.exe /C "cd \. && D:\\w64devkit\\bin\\g\+\+\.exe' } |
            Select-Object -First 1
}
if (-not $link) { throw 'link command not found (run build_awtk_soft.ps1 first)' }

$inner = $link
$inner = $inner -replace '^.*?/C "', ''
$inner = $inner -replace '"\s*$', ''
$inner = $inner -replace '^cd \. && ', ''
$inner = $inner -replace ' && cd \.$', ''

$marker = 'CMakeFiles/awtk.dir/cmake/empty.c.obj'
if ($inner -notmatch [regex]::Escape($marker)) { throw "expected object not found in link command: $marker" }
$inner = $inner.Replace($marker, "$marker lcd_sdl2.obj")

Push-Location -LiteralPath $BuildDir
try {
    & cmd.exe /c $inner
    $rc = $LASTEXITCODE
} finally {
    Pop-Location
}
if ($rc -ne 0) { throw "relink failed ($rc)" }

$dll = Join-Path $BuildDir 'awtk.dll'
$lib = Join-Path $BuildDir 'libawtk.dll.a'
if (-not (Test-Path -LiteralPath $dll)) { throw 'awtk.dll was not produced' }
Write-Host ("    OK: {0} ({1} bytes)" -f $dll, (Get-Item -LiteralPath $dll).Length) -ForegroundColor Green
Write-Host ("    OK: {0} ({1} bytes)" -f $lib, (Get-Item -LiteralPath $lib).Length) -ForegroundColor Green
exit 0
