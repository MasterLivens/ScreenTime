# ScreenTime —— AWTK 版构建脚本（备选路径；日常请用 build.ps1，那是已验证的构建方式）
#
# 设计要点（参照 D:/Work/AWTK/awtk-master/examples/cmake_find_awtk）：
#   * AWTK 在原地（D:/Work/AWTK/awtk-master）用 CMake 构建，本工作区不拷贝任何
#     AWTK 头文件 / 库 / 资源，只通过 imported target 引用其源码目录与构建产物。
#   * 编译宏必须与 awtk.dll 的编译宏完全一致，否则结构体布局不一致会导致运行时崩溃。
#     下面的列表直接取自 build_mingw/build.ninja 中 awtk 目标的 DEFINES/INCLUDES。
#   * 注意：build.ps1 与 awtk_env.ps1 里的列表必须和这里保持一致。

cmake_minimum_required(VERSION 3.20)
project(ScreenTime C)

set(CMAKE_C_STANDARD 11)
set(CMAKE_C_STANDARD_REQUIRED ON)

set(AWTK_ROOT  "D:/Work/AWTK/awtk-master" CACHE PATH "AWTK source tree")
set(AWTK_BUILD "${AWTK_ROOT}/build_mingw" CACHE PATH "AWTK CMake build tree")

if(NOT EXISTS "${AWTK_BUILD}/libawtk.dll.a")
  message(FATAL_ERROR "AWTK 构建产物不存在：${AWTK_BUILD}/libawtk.dll.a（请先构建 AWTK）")
endif()

# ---------------------------------------------------------------------------
# awtk::awtk —— 指向 AWTK 构建树的 imported target（无文件拷贝）
# ---------------------------------------------------------------------------
add_library(awtk::awtk SHARED IMPORTED)
set_target_properties(awtk::awtk PROPERTIES
  IMPORTED_IMPLIB   "${AWTK_BUILD}/libawtk.dll.a"
  IMPORTED_LOCATION "${AWTK_BUILD}/awtk.dll"
  INTERFACE_INCLUDE_DIRECTORIES
    "${AWTK_ROOT};${AWTK_ROOT}/src;${AWTK_ROOT}/src/ext_widgets;${AWTK_ROOT}/src/custom_widgets;${AWTK_ROOT}/3rd;${AWTK_ROOT}/3rd/SDL/include;${AWTK_ROOT}/3rd/glad;${AWTK_ROOT}/3rd/nanovg_plus;${AWTK_ROOT}/3rd/nanovg_plus/base;${AWTK_ROOT}/3rd/nanovg_plus/gl;${AWTK_ROOT}/3rd/SheenBidi-3.0.0/Headers;${AWTK_ROOT}/3rd/mbedtls/include;${AWTK_ROOT}/3rd/gpinyin/include;${AWTK_ROOT}/3rd/libunibreak;${AWTK_ROOT}/3rd/svgtiny/include;${AWTK_ROOT}/res;${AWTK_BUILD}/third_party/SDL2/include"
  INTERFACE_COMPILE_DEFINITIONS
    "ENABLE_CURSOR=1;HAS_GET_TIME_US64;HAS_STDIO;HAS_STD_MALLOC;HAVE_STDIO_H;NVGP_GL3;SB_CONFIG_EXPERIMENTAL_TEXT_API=1;STBTT_STATIC;STB_IMAGE_STATIC;TK_MAX_MEM_BLOCK_NR=3;WITH_ANTIALIAS;WITH_ASSET_LOADER;WITH_ASSET_LOADER_ZIP;WITH_BIDI_SHEEN=1;WITH_DATA_READER_WRITER=1;WITH_DESKTOP_STYLE;WITH_EVENT_RECORDER_PLAYER=1;WITH_FS_RES;WITH_GPU;WITH_GPU_GL;WITH_GPU_GL3;WITH_IME_PINYIN;WITH_MAIN_LOOP_CONSOLE=1;WITH_MBEDTLS=1;WITH_NANOVG_GPU;WITH_NANOVG_PLUS_GPU;WITH_OPENGL_HW_ANTIALIAS;WITH_RES_TOOLS;WITH_SDL;WITH_STB_FONT;WITH_STB_IMAGE;WITH_TEXT_BIDI=1;WITH_UNICODE_BREAK;WITH_VGCANVAS;WITH_VGCANVAS_LCD"
)

# AWTK 运行时资源根目录（res/assets/default/raw/...），同样不拷贝
set(AWTK_RES_ROOT "${AWTK_ROOT}/res" CACHE PATH "AWTK runtime assets root")

set(WIN_LIBS kernel32 user32 gdi32 winspool shell32 ole32 oleaut32 uuid comdlg32 advapi32)

# ---------------------------------------------------------------------------
# 1) AWTK 冒烟测试程序（控制台，便于打印诊断信息）
# ---------------------------------------------------------------------------
add_executable(awtk_probe tools/awtk_probe.c)
target_link_libraries(awtk_probe PRIVATE awtk::awtk ${WIN_LIBS})
target_compile_definitions(awtk_probe PRIVATE
  AWTK_RES_ROOT="${AWTK_RES_ROOT}" AWTK_ROOT="${AWTK_ROOT}")
target_link_options(awtk_probe PRIVATE -static)

# ---------------------------------------------------------------------------
# 2) ScreenTime 主程序
# ---------------------------------------------------------------------------
if(EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/src/ui_awtk.c")
  add_executable(ScreenTime WIN32
    src/main.c
    src/config.c
    src/passwd.c
    src/capture.c
    src/vision.c
    src/hooks.c
    src/logger.c
    src/util.c
    src/ui_awtk.c
  )
  target_link_libraries(ScreenTime PRIVATE awtk::awtk ${WIN_LIBS} gdiplus winhttp bcrypt)
  target_compile_definitions(ScreenTime PRIVATE
    UNICODE _UNICODE _WIN32_WINNT=0x0601 _WIN32_IE=0x0600
    AWTK_RES_ROOT="${AWTK_RES_ROOT}" AWTK_ROOT="${AWTK_ROOT}")
  target_link_options(ScreenTime PRIVATE -static -mwindows)
endif()
