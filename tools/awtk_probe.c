/**
 * AWTK 冒烟测试：验证 ScreenTime 迁移到 AWTK 的关键前提
 *   1) 能否用 AWTK 构建树就地链接（不拷贝任何 AWTK 文件）
 *   2) 中文能否正确渲染（AWTK 自带字体是子集，需注册系统 CJK TTF）
 *   3) 125% DPI 下 native window 的 w/h/ratio 到底是多少
 *   4) 能否用 Win32 ShowWindow 隐藏/显示 SDL 原生窗口（隐藏启动）
 *   5) 同线程隐藏 Win32 窗口（托盘桥）能否被 SDL 事件循环泵到
 */
#include "awtk.h"
#include "font_loader/font_loader_stb.h"
#include <windows.h>

#ifndef AWTK_RES_ROOT
#define AWTK_RES_ROOT "D:/Work/AWTK/awtk-master/res"
#endif

/* 控制台编码不可靠，诊断信息同时写 UTF-8 日志文件 */
#include <stdarg.h>
static FILE* s_log = NULL;
static void probe_log(const char* fmt, ...) {
  va_list ap;
  va_start(ap, fmt);
  if (s_log != NULL) {
    vfprintf(s_log, fmt, ap);
    fflush(s_log);
  }
  vprintf(fmt, ap);
  va_end(ap);
}
#undef printf
#define printf probe_log

#define BRIDGE_CLASS L"ScreenTimeProbeBridge"
#define WM_PROBE_TRAY (WM_APP + 1)

static const char* const kFontCandidates[] = {"C:/Windows/Fonts/simhei.ttf",
                                              "C:/Windows/Fonts/simfang.ttf",
                                              "C:/Windows/Fonts/simkai.ttf"};

static HWND s_bridge = NULL;
static HWND s_native_hwnd = NULL;
static HWND s_sdl_hwnd = NULL;
static int s_bridge_msgs = 0;

/* native_window()->handle 是 SDL_Window*，不是 HWND：需要自己找出真正的顶层窗口 */
typedef struct {
  HWND wins[16];
  int nr;
} WinList;

static BOOL enum_proc(HWND hwnd, LPARAM lp) {
  DWORD pid = 0;
  WinList* list = (WinList*)lp;

  GetWindowThreadProcessId(hwnd, &pid);
  if (pid != GetCurrentProcessId()) {
    return TRUE;
  }
  if (list->nr < 16) {
    list->wins[list->nr++] = hwnd;
  }
  return TRUE;
}

static const char* w2u(const WCHAR* w, char* buf, size_t n) {
  if (WideCharToMultiByte(CP_UTF8, 0, w, -1, buf, (int)n, NULL, NULL) <= 0) {
    buf[0] = 0;
  }
  return buf;
}

static void list_process_windows(void) {
  WinList list;
  int i = 0;

  list.nr = 0;
  EnumWindows(enum_proc, (LPARAM)&list);
  printf("[enum] %d top-level windows belong to this process:\n", list.nr);
  for (i = 0; i < list.nr; i++) {
    WCHAR cls[128] = L"";
    WCHAR title[128] = L"";
    char clsu[256] = "";
    char titlu[256] = "";
    RECT r;
    GetClassNameW(list.wins[i], cls, 127);
    GetWindowTextW(list.wins[i], title, 127);
    GetWindowRect(list.wins[i], &r);
    printf("[enum]   hwnd=%p class=\"%s\" title=\"%s\" rect=(%d,%d)-(%d,%d) visible=%d\n",
           (void*)list.wins[i], w2u(cls, clsu, sizeof(clsu)), w2u(title, titlu, sizeof(titlu)),
           (int)r.left, (int)r.top, (int)r.right, (int)r.bottom, (int)IsWindowVisible(list.wins[i]));
    if (s_sdl_hwnd == NULL && wcsstr(cls, L"SDL") != NULL) {
      s_sdl_hwnd = list.wins[i];
    }
  }
  printf("[enum] picked SDL top-level hwnd=%p\n", (void*)s_sdl_hwnd);
  fflush(stdout);
}

/* ------------------------------------------------------------------ */
/* assets_init：AWTK 要求应用自己提供（AWTK 库里没有实现）             */
/* ------------------------------------------------------------------ */
static ret_t load_cjk_font(void) {
  uint32_t i = 0;
  for (i = 0; i < ARRAY_SIZE(kFontCandidates); i++) {
    FILE* fp = fopen(kFontCandidates[i], "rb");
    long sz = 0;
    uint8_t* buf = NULL;
    font_t* font = NULL;

    if (fp == NULL) {
      continue;
    }
    fseek(fp, 0, SEEK_END);
    sz = ftell(fp);
    fseek(fp, 0, SEEK_SET);
    buf = (uint8_t*)malloc((size_t)sz);
    if (buf == NULL || fread(buf, 1, (size_t)sz, fp) != (size_t)sz) {
      fclose(fp);
      free(buf);
      continue;
    }
    fclose(fp);

    /* 以 "default" 这个名字注册，样式里引用的字体名就无需改动 */
    font = font_stb_create("default", buf, (uint32_t)sz);
    if (font == NULL) {
      free(buf);
      continue;
    }
    font_manager_add_font(font_manager(), font);
    printf("[font] registered %s (%ld bytes) as font name \"%s\"\n", kFontCandidates[i], sz,
           font->name);
    return RET_OK;
  }
  return RET_FAIL;
}

ret_t assets_init(void) {
  assets_manager_t* am = assets_manager();

  /* 只预载样式：AWTK 自带 default.ttf 不含中文，字体由下面注册的系统 TTF 提供 */
  assets_manager_preload(am, ASSET_TYPE_STYLE, "default");
  tk_init_assets();

  {
    const asset_info_t* st = assets_manager_ref(am, ASSET_TYPE_STYLE, "default");
    printf("[assets] res_root=%s theme=%p style_asset=%p name=%s\n",
           assets_manager_get_res_root(am), (void*)theme(), (void*)st,
           st != NULL ? asset_info_get_name(st) : "-");
  }

  if (load_cjk_font() != RET_OK) {
    printf("[font] !!! 没有可用的中文字体\n");
  }
  return RET_OK;
}

/* ------------------------------------------------------------------ */
/* 托盘桥窗口：验证 SDL 事件循环是否会泵同线程的 Win32 消息            */
/* ------------------------------------------------------------------ */
static LRESULT CALLBACK bridge_proc(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp) {
  if (msg == WM_PROBE_TRAY) {
    s_bridge_msgs++;
    printf("[bridge] WndProc got WM_APP+1 (#%d) while tk_run() is executing\n", s_bridge_msgs);
    fflush(stdout);
    return 0;
  }
  return DefWindowProcW(hwnd, msg, wp, lp);
}

static void create_bridge_window(void) {
  WNDCLASSW wc;
  ZeroMemory(&wc, sizeof(wc));
  wc.lpfnWndProc = bridge_proc;
  wc.hInstance = GetModuleHandleW(NULL);
  wc.lpszClassName = BRIDGE_CLASS;
  RegisterClassW(&wc);
  s_bridge = CreateWindowExW(0, BRIDGE_CLASS, L"bridge", WS_OVERLAPPED, 0, 0, 0, 0, HWND_MESSAGE,
                             NULL, wc.hInstance, NULL);
  printf("[bridge] hidden window hwnd=%p\n", (void*)s_bridge);
}

/* ------------------------------------------------------------------ */
static native_window_t* native_win(void) {
  return (native_window_t*)widget_get_prop_pointer(window_manager(), WIDGET_PROP_NATIVE_WINDOW);
}

static void dump_native_window(const char* tag) {
  native_window_info_t info;
  native_window_t* nw = native_win();

  if (nw != NULL && native_window_get_info(nw, &info) == RET_OK) {
    s_native_hwnd = (HWND)info.handle;
    printf("[%s] native window: x=%d y=%d w=%u h=%u ratio=%g hwnd=%p\n", tag, info.x, info.y,
           info.w, info.h, (double)info.ratio, info.handle);
  } else {
    printf("[%s] native window: get_info failed\n", tag);
  }
  printf("[%s] lcd=%dx%d  app_root=%s  default_font=%s\n", tag, (int)system_info()->lcd_w,
         (int)system_info()->lcd_h, system_info()->app_root, system_info()->default_font);
  fflush(stdout);
}

static void dump_text_measure(widget_t* label, const char* utf8) {
  wchar_t buf[128];
  size_t w = 0;

  tk_utf8_to_utf16(utf8, buf, ARRAY_SIZE(buf));
  w = widget_measure_text(label, buf);
  printf("[text] measure(\"%s\") = %d px, style_name=\"%s\"\n", utf8, (int)w,
         label->style ? label->style : "(null)");
  fflush(stdout);
}

static ret_t on_step1(const timer_info_t* info) {
  widget_t* win = WIDGET(info->ctx);
  widget_t* label = widget_lookup(win, "probe_label", TRUE);
  widget_t* edit = widget_lookup(win, "probe_edit", TRUE);

  (void)edit;
  dump_native_window("step1");
  list_process_windows();
  if (label != NULL) {
    dump_text_measure(label, "屏幕使用时间已到，请休息 15 分钟");
    dump_text_measure(label, "提前解锁需要输入密码");
    dump_text_measure(label, "ScreenTime 家长设置");
    printf("[text] label rect: %d,%d %dx%d\n", label->x, label->y, label->w, label->h);
  }
  PostMessageW(s_bridge, WM_PROBE_TRAY, 1, 0);
  return RET_REMOVE;
}

static ret_t on_hide(const timer_info_t* info) {
  (void)info;
  printf("[hide] before: IsWindowVisible=%d\n", IsWindowVisible(s_sdl_hwnd));
  ShowWindow(s_sdl_hwnd, SW_HIDE);
  printf("[hide] after ShowWindow(SW_HIDE): IsWindowVisible=%d\n", IsWindowVisible(s_sdl_hwnd));
  fflush(stdout);
  return RET_REMOVE;
}

static ret_t on_show(const timer_info_t* info) {
  (void)info;
  ShowWindow(s_sdl_hwnd, SW_SHOW);
  SetWindowPos(s_sdl_hwnd, HWND_TOPMOST, 0, 0, 0, 0, SWP_NOMOVE | SWP_NOSIZE);
  printf("[show] after ShowWindow(SW_SHOW)+TOPMOST: IsWindowVisible=%d\n",
         IsWindowVisible(s_sdl_hwnd));
  fflush(stdout);
  return RET_REMOVE;
}

static ret_t on_quit(const timer_info_t* info) {
  (void)info;
  printf("[done] bridge messages seen = %d\n", s_bridge_msgs);
  dump_native_window("final");
  tk_quit();
  return RET_REMOVE;
}

int main(int argc, char** argv) {
  widget_t* wm = NULL;
  widget_t* win = NULL;
  widget_t* label = NULL;
  widget_t* edit = NULL;
  widget_t* btn = NULL;

  (void)argc;
  (void)argv;

  s_log = fopen("D:/Work/ScreenTime/logs/probe.log", "wb");
  printf("=== AWTK probe ===\n");

  tk_pre_init();
  if (tk_init(560, 340, APP_DESKTOP, "ScreenTimeProbe", AWTK_RES_ROOT) != RET_OK) {
    printf("[init] tk_init FAILED\n");
    return 1;
  }
  assets_init();
  tk_ext_widgets_init();
  log_set_log_level(LOG_LEVEL_WARN);
  window_manager_set_cursor(window_manager(), "cursor");

  create_bridge_window();

  wm = window_manager();
  win = window_create(NULL, 0, 0, 0, 0);
  widget_set_name(win, "probe_win");
  widget_set_text_utf8(win, "屏幕使用时间 · 测试");
  window_manager_open_window(wm, win);

  label = label_create(win, 20, 20, 500, 40);
  widget_set_name(label, "probe_label");
  widget_set_text_utf8(label, "屏幕使用时间已到，请休息 15 分钟");

  edit = edit_create(win, 20, 80, 300, 36);
  widget_set_name(edit, "probe_edit");
  widget_set_text_utf8(edit, "请输入密码");

  btn = button_create(win, 20, 140, 180, 40);
  widget_set_name(btn, "probe_btn");
  widget_set_text_utf8(btn, "确定");

  printf("[ui] window rect: %d,%d %dx%d, children=%d\n", win->x, win->y, win->w, win->h,
         (int)win->children->size);

  timer_add(on_step1, win, 400);
  timer_add(on_hide, win, 4500);
  timer_add(on_show, win, 5500);
  timer_add(on_quit, win, 7000);

  printf("[run] entering tk_run()\n");
  fflush(stdout);
  tk_run();

  printf("[run] tk_run() returned\n");
  tk_exit();
  if (s_log != NULL) {
    fclose(s_log);
  }
  return 0;
}
