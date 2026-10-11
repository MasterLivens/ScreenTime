# AWTK in-place reference config (NO AWTK file is copied into this workspace).
# Include dirs and defines are taken verbatim from the awtk target's INCLUDES/DEFINES
# in D:/Work/AWTK/awtk-master/build_mingw/build.ninja, so the ABI matches awtk.dll.
# Usage: powershell -NoProfile -ExecutionPolicy Bypass -File build.ps1

$AwtkRoot  = 'D:\Work\AWTK\awtk-master'
$AwtkBuild = Join-Path $AwtkRoot 'build_mingw'
$AwtkRes   = Join-Path $AwtkRoot 'res'
$AwtkLib   = Join-Path $AwtkBuild 'libawtk.dll.a'

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

function Get-AwtkCFlags {
  $inc = $AwtkIncDirs | ForEach-Object { "-I$_" }
  $def = $AwtkDefs     | ForEach-Object { "-D$_" }
  @($inc + $def)
}

function Get-AwtkLinkFlags {
  @("-L$AwtkBuild", '-l:libawtk.dll.a',
    '-lkernel32','-luser32','-lgdi32','-lwinspool','-lshell32','-lole32','-loleaut32',
    '-luuid','-lcomdlg32','-ladvapi32')
}
