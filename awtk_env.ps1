# AWTK reference config. AWTK lives in the git submodule third_party\awtk and is
# compiled into THIS repo (out\awtk_gl, out\awtk_soft) -- nothing outside the repo.
#
# Two AWTK builds are supported and picked by $env:AWTK_BACKEND:
#   out\awtk_gl    GPU/OpenGL build            (.\build_awtk.ps1 -Backend gl)
#   out\awtk_soft  software renderer (AGGE)    (.\build_awtk.ps1 -Backend soft)
#                  works WITHOUT OpenGL.
#
# Include dirs and defines are parsed out of the chosen build's build.ninja, so the
# flags always match that awtk.dll (same ABI) -- no hand-maintained macro list.
# If parsing fails, the hardcoded GL list below is used as a fallback.
#
# IMPORTANT: this file is PURE ASCII. PowerShell 5.1 decodes BOM-less files as ANSI,
# so non-ASCII text here would be garbled and can break parsing.
#
# Usage: powershell -NoProfile -ExecutionPolicy Bypass -File build.ps1

$AwtkRoot  = Join-Path $PSScriptRoot 'third_party\awtk'
$AwtkRes   = Join-Path $AwtkRoot 'res'
$SoftBuild = Join-Path $PSScriptRoot 'out\awtk_soft'
$GlBuild   = Join-Path $PSScriptRoot 'out\awtk_gl'

# Backend selection: 'gl' (default, GPU/OpenGL) or 'soft' (software renderer).
# build.ps1 / make_installer.ps1 set $env:AWTK_BACKEND before dot-sourcing this file.
$Backend = 'gl'
if ($env:AWTK_BACKEND) { $Backend = $env:AWTK_BACKEND.ToLower() }

if ($Backend -eq 'soft') {
    if (-not (Test-Path -LiteralPath (Join-Path $SoftBuild 'awtk.dll') -PathType Leaf)) {
        throw ("software AWTK build not found ({0}). Run .\build_awtk.ps1 -Backend soft first." -f $SoftBuild)
    }
    $AwtkBuild = $SoftBuild
} else {
    if (-not (Test-Path -LiteralPath (Join-Path $GlBuild 'awtk.dll') -PathType Leaf)) {
        throw ("OpenGL AWTK build not found ({0}). Run .\build_awtk.ps1 -Backend gl first." -f $GlBuild)
    }
    $AwtkBuild = $GlBuild
}
$AwtkBackend = $Backend
$AwtkLib = Join-Path $AwtkBuild 'libawtk.dll.a'

# ---------------------------------------------------------------------------
# Parse DEFINES / INCLUDES of the awtk targets out of build.ninja
# ---------------------------------------------------------------------------
function Get-AwtkFlagsFromNinja {
    param([string]$NinjaPath)

    $defs = New-Object 'System.Collections.Generic.HashSet[string]'
    $incs = New-Object 'System.Collections.Generic.HashSet[string]'
    if (-not (Test-Path -LiteralPath $NinjaPath -PathType Leaf)) {
        return @{ Defines = @(); Includes = @() }
    }

    $lines = Get-Content -LiteralPath $NinjaPath
    for ($i = 0; $i -lt $lines.Count; $i++) {
        # CMake target dirs: CMakeFiles/awtk.dir/, CMakeFiles/awtk_base.dir/, ...
        if ($lines[$i] -notmatch '^build CMakeFiles/awtk[^/]*\.dir/') { continue }
        for ($j = $i + 1; $j -lt $lines.Count; $j++) {
            $line = $lines[$j]
            if ($line -notmatch '^\s') { break }          # end of this build block
            if ($line -match '^\s+DEFINES = (.*)$') {
                foreach ($tok in ($matches[1] -split '\s+')) {
                    if ($tok -like '-D*') { [void]$defs.Add($tok) }
                }
            }
            if ($line -match '^\s+INCLUDES = (.*)$') {
                foreach ($tok in ($matches[1] -split '\s+')) {
                    if ($tok -like '-I*') { [void]$incs.Add($tok) }
                }
            }
        }
    }
    return @{ Defines = @($defs); Includes = @($incs) }
}

$parsed = Get-AwtkFlagsFromNinja (Join-Path $AwtkBuild 'build.ninja')
$parsedOk = ($parsed.Defines.Count -gt 5) -and (($parsed.Defines -join ' ') -match 'WITH_SDL')

if ($parsedOk) {
    $AwtkIncDirs   = @($parsed.Includes | ForEach-Object { $_.Substring(2) })
    $AwtkDefs      = @($parsed.Defines  | ForEach-Object { $_.Substring(2) })
    $AwtkFlagsSrc  = "build.ninja ($AwtkBackend)"
} elseif ($AwtkBackend -eq 'gl') {
    # ---- fallback: hand-maintained list for the original GL build ----
    $AwtkIncDirs = @(
        $AwtkRoot,
        "$AwtkRoot\src",
        "$AwtkRoot\src\ext_widgets",
        "$AwtkRoot\src\custom_widgets",
        "$AwtkRoot\3rd",
        "$AwtkRoot\3rd\SDL\include",
        "$AwtkRoot\3rd\glad",
        "$AwtkRoot\3rd\nanovg_plus",
        "$AwtkRoot\3rd\nanovg_plus\base",
        "$AwtkRoot\3rd\nanovg_plus\gl",
        "$AwtkRoot\3rd\SheenBidi-3.0.0\Headers",
        "$AwtkRoot\3rd\mbedtls\include",
        "$AwtkRoot\3rd\gpinyin\include",
        "$AwtkRoot\3rd\libunibreak",
        "$AwtkRoot\3rd\svgtiny\include",
        $AwtkRes,
        "$AwtkBuild\third_party\SDL2\include"
    )
    $AwtkDefs = @(
        'ENABLE_CURSOR=1','HAS_GET_TIME_US64','HAS_STDIO','HAS_STD_MALLOC','HAVE_STDIO_H','NVGP_GL3',
        'SB_CONFIG_EXPERIMENTAL_TEXT_API=1','STBTT_STATIC','STB_IMAGE_STATIC','TK_MAX_MEM_BLOCK_NR=3',
        'WITH_ANTIALIAS','WITH_ASSET_LOADER','WITH_ASSET_LOADER_ZIP','WITH_BIDI_SHEEN=1',
        'WITH_DATA_READER_WRITER=1','WITH_DESKTOP_STYLE','WITH_EVENT_RECORDER_PLAYER=1','WITH_FS_RES',
        'WITH_GPU','WITH_GPU_GL','WITH_GPU_GL3','WITH_IME_PINYIN','WITH_MAIN_LOOP_CONSOLE=1',
        'WITH_MBEDTLS=1','WITH_NANOVG_GPU','WITH_NANOVG_PLUS_GPU','WITH_OPENGL_HW_ANTIALIAS',
        'WITH_RES_TOOLS','WITH_SDL','WITH_STB_FONT','WITH_STB_IMAGE','WITH_TEXT_BIDI=1',
        'WITH_UNICODE_BREAK','WITH_VGCANVAS','WITH_VGCANVAS_LCD'
    )
    $AwtkFlagsSrc = 'hardcoded GL list'
} else {
    throw ("cannot parse AWTK defines from {0}\build.ninja. Run .\build_awtk.ps1 -Backend soft first, or use -Backend gl." -f $AwtkBuild)
}

function Get-AwtkCFlags {
    $inc = $AwtkIncDirs | ForEach-Object { '-I' + $_ }
    $def = $AwtkDefs     | ForEach-Object { '-D' + $_ }
    @($inc + $def)
}

function Get-AwtkLinkFlags {
    @("-L$AwtkBuild", '-l:libawtk.dll.a',
      '-lkernel32','-luser32','-lgdi32','-lwinspool','-lshell32','-lole32','-loleaut32',
      '-luuid','-lcomdlg32','-ladvapi32')
}

function Show-AwtkEnv {
    "AWTK build : $AwtkBuild"
    "backend    : $AwtkBackend ($AwtkFlagsSrc)"
    "flags      : $($AwtkDefs.Count) defines, $($AwtkIncDirs.Count) include dirs"
}
