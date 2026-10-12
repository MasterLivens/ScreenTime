/* =====================================================================
 * ScreenTime —— AWTK 界面层
 * 一个 AWTK 窗口，三页切换：状态页 / 家长验证页 / 全屏锁屏页。
 * Win32 侧负责：托盘图标、单实例桥、隐藏到右下角、全屏覆盖、置顶、ClipCursor。
 * AWTK 自己处理 DPI（控件坐标是逻辑像素）。
 * ===================================================================== */
#include <winsock2.h>
#include <ws2tcpip.h>
#include <windows.h>
#include "ui.h"

#include <shellapi.h>
#include <strsafe.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "awtk.h"
#include "font_loader/font_loader_stb.h"
#include "window_manager/window_manager_default.h"

#include "appstate.h"
#include "config.h"
#include "hooks.h"
#include "logger.h"
#include "passwd.h"
#include "util.h"

#ifndef AWTK_RES_ROOT
#define AWTK_RES_ROOT "D:/Work/AWTK/awtk-master/res"
#endif

#define WM_TRAY        (WM_APP + 1)
#define TRAY_UID       1

#define BTN_REST       201
#define BTN_RELOAD     202
#define BTN_PARENT     205
#define BTN_TRAY_SHOW  210

/* 程序版本（和根目录 VERSION 保持一致） */
#define ST_VERSION     L"1.1.1"

#define STATUS_W       470
#define STATUS_H       240

#define PAGE_STATUS    0
#define PAGE_PARENT    1
#define PAGE_LOCK      2
#define PAGE_SETTINGS  3
#define PAGE_ADVANCED  4
#define PAGE_WARN      5   /* 休息前倒计时提醒（右下角小窗里的一页） */
#define PAGE_NR        6

#define MARGIN         18
#define BTN_W          104
#define BTN_H          34
#define GAP            12

/* 设置页逻辑尺寸（进入设置时把窗口临时放大到这个尺寸） */
#define ST_W           680
#define ST_H           460
#define ST_ROW_H       40
#define ST_ROW_N       16          /* 2 列 x 8 行 */
#define ST_COL_W       312
#define ST_COL0        18
#define ST_COL1        (ST_COL0 + ST_COL_W + 30)
#define ST_LABEL_W     150
#define ST_NUM_W       62
#define ST_STEP_W      30

/* ---------------- AWTK 侧状态 ---------------- */
static widget_t* g_win = NULL;
static widget_t* g_pages[PAGE_NR];
static int g_page = PAGE_STATUS;
static int g_lastW = 0, g_lastH = 0;
static double g_scale = 1.0;       /* AWTK 的 DPI 缩放（逻辑像素 -> 物理像素） */
static Config g_edit;              /* 设置界面的工作副本，点「保存」才写回 g_cfg */

/* 状态页 */
static widget_t *g_lblHead, *g_lblBig, *g_lblL1, *g_lblL2;
static widget_t *g_barBg, *g_barFg;
static HICON      g_appIcon = NULL;
static widget_t *g_btnRest, *g_btnReload, *g_btnParent;

/* 家长验证页 */
static widget_t *g_pTitle, *g_pText, *g_pLabel, *g_pErr, *g_pEdit, *g_pOk, *g_pCancel;

/* 锁屏页 */
static widget_t *g_lTitle, *g_lUsed, *g_lReason, *g_lCount, *g_lHint, *g_lFoot;
static widget_t *g_lEdit, *g_lBtn;

/* 设置页（基本） */
typedef enum { ROW_NUM = 0, ROW_BOOL = 1 } RowKind;
typedef struct {
  RowKind      kind;
  const WCHAR *label;
  int         *field;
  int          min, max, step;
} RowDef;

static const RowDef kRows[ST_ROW_N] = {
  /* 左列 */
  { ROW_NUM,  L"限时使用（分钟）",   &g_edit.session_minutes,        1, 1440, 5 },
  { ROW_NUM,  L"强制休息（分钟）",   &g_edit.break_minutes,          1, 1440, 5 },
  { ROW_NUM,  L"锁屏前提醒（分钟）", &g_edit.warn_minutes,           0,  120, 1 },
  { ROW_BOOL, L"空闲时暂停计时",     &g_edit.idle_pause,             0,    1, 1 },
  { ROW_NUM,  L"空闲判定（秒）",     &g_edit.idle_seconds,          10, 7200, 30 },
  { ROW_BOOL, L"提前解锁扣时长",     &g_edit.penalty,                0,    1, 1 },
  { ROW_NUM,  L"密码错误加时（秒）", &g_edit.wrong_penalty_seconds,  0, 3600, 10 },
  { ROW_BOOL, L"锁屏屏蔽快捷键",     &g_edit.block_hotkeys,          0,    1, 1 },
  /* 右列 */
  { ROW_BOOL, L"定时截图",           &g_edit.screenshot_enabled,     0,    1, 1 },
  { ROW_NUM,  L"截图间隔（秒）",     &g_edit.screenshot_interval,    5, 3600, 10 },
  { ROW_BOOL, L"保存截图文件",       &g_edit.save_screenshots,       0,    1, 1 },
  { ROW_BOOL, L"送模型识别",         &g_edit.vision_enabled,         0,    1, 1 },
  { ROW_NUM,  L"截图保留（天）",     &g_edit.keep_days,              0, 3650, 1 },
  { ROW_BOOL, L"启动时隐藏界面",     &g_edit.start_hidden,           0,    1, 1 },
  { ROW_BOOL, L"显示托盘图标",       &g_edit.show_tray_icon,         0,    1, 1 },
  { ROW_NUM,  L"送图最大宽度（像素）", &g_edit.max_image_width,      320, 3840, 160 },
};

static widget_t *g_sTitle, *g_sHint, *g_sMsg;
static WCHAR      g_sHintText[256] = L"";
static widget_t *g_rowLabel[ST_ROW_N], *g_rowMinus[ST_ROW_N],
                *g_rowVal[ST_ROW_N],   *g_rowPlus[ST_ROW_N], *g_rowBtn[ST_ROW_N];
static widget_t *g_sBtnAdv, *g_sBtnDefault, *g_sBtnCancel, *g_sBtnSave;

/* 设置页（识别与密码） */
static widget_t *g_aTitle, *g_aHint, *g_aMsg;
static widget_t *g_aUrlLabel,  *g_aUrlEdit;
static widget_t *g_aModelLabel, *g_aModelEdit;
static widget_t *g_aKeyLabel,  *g_aKeyEdit;
static widget_t *g_aPw1Label, *g_aPw1Edit, *g_aPw2Label, *g_aPw2Edit;
static widget_t *g_aDataLabel, *g_aPwNote;
static widget_t *g_aBtnBack, *g_aBtnData, *g_aBtnSave;


/* ---------------- Win32 侧状态 ---------------- */
static HWND     g_bridge = NULL;     /* 隐形顶层窗口：托盘 / 单实例 / 消息桥 */
static HWND     g_sdlWnd = NULL;     /* AWTK(SDL) 的真实顶层窗口 */
static WNDPROC  g_sdlOld = NULL;
static HHOOK    g_cbtHook = NULL;    /* 只在创建 AWTK 窗口期间挂着：把 WS_VISIBLE 摘掉 */
static NOTIFYICONDATAW g_nid;
static int      g_trayAdded = 0;
static int      g_hidden = 0;
static int      g_wasHidden = 0;
static int      g_fullscreen = 0;
static RECT     g_savedRect;
static int      g_topCnt = 0;

static WCHAR    g_reason[256] = L"";
static WCHAR    g_hint[256]   = L"";
static int      g_warnDismissed = 0;  /* 用户点过「知道了」后，本次会话不再弹休息提醒 */
static int      g_bad = 0;

static void SwitchPage(int page);
static void LayoutPage(int page);
static void RefreshStatus(void);
static void RefreshLock(void);
static void PlaceStatusWindow(void);
static void FixWindowFrame(void);
static void HideToTray(int balloon);
static void ShowFromTray(void);
static void ShowTrayMenu(void);
static void DoRest(void);
static void DoReload(void);
static void DoParent(void);
static void ParentTry(void);
static void SubmitPassword(void);
static void TrayAdd(void);
static void TrayRemove(void);
static void TrayUpdateTip(void);
static void ClearMsgAll(void);
static void EnterSettings(void);
static void LeaveSettings(void);
static int  SettingsCommit(int fromAdvanced);
static ret_t OnRestClick(void* ctx, event_t* e);
static ret_t OnReloadClick(void* ctx, event_t* e);
static ret_t OnParentClick(void* ctx, event_t* e);
static ret_t OnParentOk(void* ctx, event_t* e);
static ret_t OnParentCancel(void* ctx, event_t* e);
static ret_t OnUnlockClick(void* ctx, event_t* e);
static ret_t OnEditKey(void* ctx, event_t* e);
static ret_t OnWinKey(void* ctx, event_t* e);
static ret_t OnPagePointerDown(void* ctx, event_t* e);
static ret_t OnTimer(const timer_info_t* info);

/* ---------------- 小工具 ---------------- */
static uint32_t col(int r, int g, int b) {
  return color_init((uint8_t)r, (uint8_t)g, (uint8_t)b, 255).color;
}

static native_window_t* TheNativeWindow(void) {
  window_manager_t* wm = (window_manager_t*)window_manager();
  if (wm == NULL) return NULL;
  return ((window_manager_default_t*)wm)->native_window;
}

static void SetTextW(widget_t* w, const WCHAR* text) {
  char buf[1024];
  if (!w || !text) return;
  buf[0] = 0;
  WToUtf8(text, buf, (int)sizeof(buf));
  widget_set_text_utf8(w, buf);
}

static void LogUtf8(const char* u8) {
  WCHAR wbuf[512];
  wbuf[0] = 0;
  Utf8ToW(u8 ? u8 : "", wbuf, (int)(sizeof(wbuf) / sizeof(WCHAR)));
  LogEventW(L"%s", wbuf);
}

/* ---------------- 资源与字体 ---------------- */
static ret_t assets_init(void) {
  assets_manager_t* am = assets_manager();
  assets_manager_preload(am, ASSET_TYPE_STYLE, "default");
  return tk_init_assets();
}

static const wchar_t* g_fontCandidates[] = {
  L"C:\\Windows\\Fonts\\simhei.ttf",
  L"C:\\Windows\\Fonts\\simfang.ttf",
  L"C:\\Windows\\Fonts\\simkai.ttf",
  L"C:\\Windows\\Fonts\\STZHONGS.TTF",
  L"C:\\Windows\\Fonts\\STSONG.TTF",
  NULL
};

static int load_cjk_font(void) {
  int i;
  for (i = 0; g_fontCandidates[i] != NULL; i++) {
    FILE* f = _wfopen(g_fontCandidates[i], L"rb");
    unsigned char* buf;
    long sz;
    font_t* font;
    if (!f) continue;
    fseek(f, 0, SEEK_END);
    sz = ftell(f);
    fseek(f, 0, SEEK_SET);
    if (sz < 4096) { fclose(f); continue; }
    buf = (unsigned char*)malloc((size_t)sz);
    if (!buf || fread(buf, 1, (size_t)sz, f) != (size_t)sz) {
      if (buf) free(buf);
      fclose(f);
      continue;
    }
    fclose(f);
    font = font_stb_create("default", buf, (uint32_t)sz);
    if (font == NULL) { free(buf); continue; }
    font_manager_add_font(font_manager(), font);
    LogEventW(L"中文字体：%s（%lld 字节）", g_fontCandidates[i], (LONGLONG)sz);
    return RET_OK;
  }
  LogEventW(L"警告：未找到中文字体，界面中文可能无法显示");
  return RET_FAIL;
}

static void pick_res_root(char* out, size_t cap) {
  WCHAR exeDir[MAX_PATH];
  WCHAR probe[MAX_PATH];
  GetModuleFileNameW(NULL, exeDir, MAX_PATH);
  {
    WCHAR* slash = wcsrchr(exeDir, L'\\');
    if (slash) *slash = 0;
  }
  StringCchCopyW(probe, MAX_PATH, exeDir);
  StringCchCatW(probe, MAX_PATH, L"\\res\\assets\\default\\raw\\styles\\default.bin");
  if (GetFileAttributesW(probe) != INVALID_FILE_ATTRIBUTES) {
    WToUtf8(exeDir, out, (int)cap);
  } else {
    StringCchCopyA(out, cap, AWTK_RES_ROOT);
  }
}

/* ---------------- Win32 桥接窗口（托盘 / 单实例） ---------------- */
static LRESULT CALLBACK BridgeProc(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp);

static BOOL CALLBACK EnumSdlCb(HWND h, LPARAM lp) {
  DWORD p = 0;
  WCHAR cls[64];
  GetWindowThreadProcessId(h, &p);
  if (p != GetCurrentProcessId()) return TRUE;
  GetClassNameW(h, cls, 64);
  if (wcsstr(cls, L"SDL") == NULL) return TRUE;
  *(HWND*)lp = h;
  return FALSE;
}

static HWND FindTopSdlWindow(void) {
  HWND found = NULL;
  EnumWindows(EnumSdlCb, (LPARAM)&found);
  return found;
}

static void BridgeCreate(void) {
  WNDCLASSEXW wc;
  memset(&wc, 0, sizeof(wc));
  wc.cbSize        = sizeof(wc);
  wc.lpfnWndProc   = BridgeProc;
  wc.hInstance     = GetModuleHandleW(NULL);
  wc.hCursor       = LoadCursorW(NULL, IDC_ARROW);
  wc.lpszClassName = L"ScreenTimeStatus";
  RegisterClassExW(&wc);

  g_bridge = CreateWindowExW(WS_EX_TOOLWINDOW, L"ScreenTimeStatus", L"屏幕使用时间管理",
                             WS_POPUP, -32000, -32000, 2, 2,
                             NULL, NULL, GetModuleHandleW(NULL), NULL);
}

static LRESULT CALLBACK BridgeProc(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp) {
  switch (msg) {
    case WM_TRAY:
      if (LOWORD(lp) == WM_LBUTTONUP || LOWORD(lp) == WM_LBUTTONDBLCLK) {
        ShowFromTray();
      } else if (LOWORD(lp) == WM_RBUTTONUP || LOWORD(lp) == WM_CONTEXTMENU) {
        ShowTrayMenu();
      }
      return 0;
    case WM_SHOWREQ:
      ShowFromTray();
      return 0;
    case WM_COMMAND:
      switch (LOWORD(wp)) {
        case BTN_TRAY_SHOW: ShowFromTray(); break;
        case BTN_REST:      DoRest(); break;
        case BTN_RELOAD:    DoReload(); break;
        case BTN_PARENT:    DoParent(); break;
      }
      return 0;
    case WM_CLOSE:
      HideToTray(0);
      return 0;
    case WM_DESTROY:
      PostQuitMessage(0);
      return 0;
  }
  return DefWindowProcW(hwnd, msg, wp, lp);
}

/* ---------------- 子类化 AWTK 的 SDL 窗口：吞掉关闭 / 最大化 ---------------- */
static LRESULT CALLBACK SdlProc(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp) {
  if (msg == WM_CLOSE) {
    if (LockIsActive()) return 0;
    HideToTray(1);
    return 0;
  }
  if (msg == WM_SYSCOMMAND) {
    WPARAM sc = wp & 0xFFF0;
    if (sc == SC_CLOSE) {
      if (LockIsActive()) return 0;
      HideToTray(1);
      return 0;
    }
    if (LockIsActive() && (sc == SC_MAXIMIZE || sc == SC_MOVE ||
                           sc == SC_SIZE || sc == SC_MINIMIZE)) {
      return 0;
    }
  }
  return CallWindowProcW(g_sdlOld, hwnd, msg, wp, lp);
}

static void HookSdlWindow(void) {
  g_sdlWnd = FindTopSdlWindow();
  if (!g_sdlWnd) {
    LogEventW(L"未找到 AWTK 窗口句柄，隐藏/全屏功能可能异常");
    return;
  }
  g_sdlOld = (WNDPROC)SetWindowLongPtrW(g_sdlWnd, GWLP_WNDPROC, (LONG_PTR)SdlProc);
}

/* AWTK(SDL) 创建窗口的流程是：CreateWindow(带 WS_VISIBLE) → SDL_ShowWindow()，
   所以"启动隐藏"如果等窗口建好再 SW_HIDE，屏幕上一定会闪一下。
   这里挂一个线程级 CBT 钩子，在窗口创建的瞬间改三件事：
     1) 摘掉 WS_VISIBLE；
     2) 临时加上 WS_EX_TOOLWINDOW —— 否则 SDL 那次 ShowWindow 会让任务栏按钮闪一下；
     3) 把位置挪到屏幕外 (-32000)。
   这样即便 SDL 紧接着把它 ShowWindow 出来，用户也看不到任何东西（也抢不到焦点），
   等我们布局好右下角、真正决定显示时再恢复样式并 ShowWindow。 */
static LRESULT CALLBACK CbtProc(int code, WPARAM wp, LPARAM lp) {
  if (code == HCBT_CREATEWND) {
    CBT_CREATEWNDW* cc = (CBT_CREATEWNDW*)lp;
    if (cc != NULL && cc->lpcs != NULL) {
      cc->lpcs->style &= ~WS_VISIBLE;
      cc->lpcs->dwExStyle |= WS_EX_TOOLWINDOW;
      cc->lpcs->x = -32000;
      cc->lpcs->y = -32000;
    }
  }
  return CallNextHookEx(g_cbtHook, code, wp, lp);
}

static void HideOnCreateBegin(void) {
  if (g_cbtHook == NULL) {
    g_cbtHook = SetWindowsHookExW(WH_CBT, CbtProc, NULL, GetCurrentThreadId());
    if (g_cbtHook == NULL) LogEventW(L"CBT 钩子安装失败（启动瞬间可能闪一下）");
  }
}

static void HideOnCreateEnd(void) {
  if (g_cbtHook != NULL) {
    UnhookWindowsHookEx(g_cbtHook);
    g_cbtHook = NULL;
  }
  /* 去掉临时加上的 WS_EX_TOOLWINDOW，这样以后正常显示时任务栏还是会出按钮 */
  if (g_sdlWnd != NULL) {
    LONG_PTR ex = GetWindowLongPtrW(g_sdlWnd, GWL_EXSTYLE);
    if (ex & WS_EX_TOOLWINDOW) {
      SetWindowLongPtrW(g_sdlWnd, GWL_EXSTYLE, ex & ~WS_EX_TOOLWINDOW);
    }
  }
}

static void FixWindowFrame(void) {
  LONG_PTR st;
  if (!g_sdlWnd) return;
  st = GetWindowLongPtrW(g_sdlWnd, GWL_STYLE);
  st &= ~(WS_MAXIMIZEBOX | WS_THICKFRAME);
  st |= WS_MINIMIZEBOX;
  SetWindowLongPtrW(g_sdlWnd, GWL_STYLE, st);
  SetWindowPos(g_sdlWnd, NULL, 0, 0, 0, 0,
               SWP_NOMOVE | SWP_NOSIZE | SWP_NOZORDER | SWP_FRAMECHANGED);
  /* 标题栏/任务栏图标（exe 里没有图标资源，用系统图标） */
  if (g_appIcon != NULL) {
    SendMessageW(g_sdlWnd, WM_SETICON, ICON_SMALL, (LPARAM)g_appIcon);
    SendMessageW(g_sdlWnd, WM_SETICON, ICON_BIG, (LPARAM)g_appIcon);
  }
}

static void PlaceStatusWindow(void) {
  RECT rc, wa;
  int w, h;
  if (!g_sdlWnd) return;
  if (!GetWindowRect(g_sdlWnd, &rc)) return;
  w = rc.right - rc.left;
  h = rc.bottom - rc.top;
  SystemParametersInfoW(SPI_GETWORKAREA, 0, &wa, 0);
  SetWindowPos(g_sdlWnd, NULL, wa.right - w - 24, wa.bottom - h - 12, 0, 0,
               SWP_NOSIZE | SWP_NOZORDER | SWP_NOACTIVATE);
}

/* 把锁屏窗口强行拉到前台并拿到键盘焦点。
   Windows 有「前台锁定」：后台进程直接 SetForegroundWindow 常常失败，
   所以先 AttachThreadInput 到当前前台线程再抢。 */
static void ForceForeground(void) {
  HWND fg;
  DWORD myTid, fgTid = 0;
  if (!g_sdlWnd) return;
  fg = GetForegroundWindow();
  myTid = GetCurrentThreadId();
  if (fg != g_sdlWnd) {
    if (fg) fgTid = GetWindowThreadProcessId(fg, NULL);
    if (fgTid && fgTid != myTid) AttachThreadInput(myTid, fgTid, TRUE);
    BringWindowToTop(g_sdlWnd);
    SetForegroundWindow(g_sdlWnd);
    SetActiveWindow(g_sdlWnd);
    if (fgTid && fgTid != myTid) AttachThreadInput(myTid, fgTid, FALSE);
  }
  if (GetFocus() != g_sdlWnd) SetFocus(g_sdlWnd);
}

static void EnterFullscreen(void) {
  native_window_t* nw = TheNativeWindow();
  int vx, vy, vw, vh;
  if (!g_sdlWnd) return;
  /* 窗口可能被「显示桌面」/显示模式切换弄成最小化，最小化状态下
     SetWindowPos 不会把它显示出来（锁屏就白锁了），必须先还原。 */
  if (IsIconic(g_sdlWnd)) ShowWindow(g_sdlWnd, SW_RESTORE);
  GetWindowRect(g_sdlWnd, &g_savedRect);
  if (nw) native_window_show_border(nw, FALSE);
  vx = GetSystemMetrics(SM_XVIRTUALSCREEN);
  vy = GetSystemMetrics(SM_YVIRTUALSCREEN);
  vw = GetSystemMetrics(SM_CXVIRTUALSCREEN);
  vh = GetSystemMetrics(SM_CYVIRTUALSCREEN);
  if (vw <= 0 || vh <= 0) {
    vx = 0; vy = 0;
    vw = GetSystemMetrics(SM_CXSCREEN);
    vh = GetSystemMetrics(SM_CYSCREEN);
  }
  SetWindowPos(g_sdlWnd, HWND_TOPMOST, vx, vy, vw, vh,
               SWP_SHOWWINDOW | SWP_FRAMECHANGED);
  ForceForeground();
  {
    RECT clip = { vx, vy, vx + vw, vy + vh };
    ClipCursor(&clip);
  }
  g_fullscreen = 1;
}

static void ExitFullscreen(void) {
  native_window_t* nw = TheNativeWindow();
  if (!g_sdlWnd) return;
  if (IsIconic(g_sdlWnd)) ShowWindow(g_sdlWnd, SW_RESTORE);
  if (nw) native_window_show_border(nw, TRUE);
  SetWindowPos(g_sdlWnd, HWND_NOTOPMOST,
               g_savedRect.left, g_savedRect.top,
               g_savedRect.right - g_savedRect.left, g_savedRect.bottom - g_savedRect.top,
               SWP_FRAMECHANGED | SWP_NOACTIVATE | SWP_SHOWWINDOW);
  FixWindowFrame();
  PlaceStatusWindow();
  g_fullscreen = 0;
}

/* ---------------- 托盘 ---------------- */
static void TrayAdd(void) {
  if (g_trayAdded || !g_bridge) return;
  if (!g_cfg.show_tray_icon) return;
  memset(&g_nid, 0, sizeof(g_nid));
  g_nid.cbSize = sizeof(g_nid);
  g_nid.hWnd = g_bridge;
  g_nid.uID = TRAY_UID;
  g_nid.uFlags = NIF_ICON | NIF_MESSAGE | NIF_TIP;
  g_nid.uCallbackMessage = WM_TRAY;
  g_nid.hIcon = g_appIcon;
  StringCchCopyW(g_nid.szTip, ARRAYSIZE(g_nid.szTip), L"屏幕使用时间管理");
  if (Shell_NotifyIconW(NIM_ADD, &g_nid)) g_trayAdded = 1;
}

static void TrayRemove(void) {
  if (!g_trayAdded) return;
  Shell_NotifyIconW(NIM_DELETE, &g_nid);
  g_trayAdded = 0;
}

static void TrayUpdateTip(void) {
  LONGLONG remain;
  if (!g_trayAdded) return;
  remain = g_sessionLenMs - g_usageMs;
  if (remain < 0) remain = 0;
  if (g_locked) {
    StringCchCopyW(g_nid.szTip, ARRAYSIZE(g_nid.szTip), L"屏幕使用时间管理（锁屏休息中）");
  } else {
    LONGLONG rs = remain / 1000;
    StringCchPrintfW(g_nid.szTip, ARRAYSIZE(g_nid.szTip),
                     L"屏幕使用时间管理　剩余 %02lld:%02lld", (rs / 60) % 100, rs % 60);
  }
  g_nid.uFlags = NIF_TIP;
  Shell_NotifyIconW(NIM_MODIFY, &g_nid);
}

static void TrayBalloon(const WCHAR* text) {
  if (!g_trayAdded) return;
  g_nid.uFlags = NIF_INFO;
  StringCchCopyW(g_nid.szInfoTitle, ARRAYSIZE(g_nid.szInfoTitle), L"屏幕使用时间管理");
  StringCchCopyW(g_nid.szInfo, ARRAYSIZE(g_nid.szInfo), text);
  g_nid.dwInfoFlags = NIIF_INFO;
  Shell_NotifyIconW(NIM_MODIFY, &g_nid);
  g_nid.uFlags = NIF_ICON | NIF_MESSAGE | NIF_TIP;
}

static void ShowTrayMenu(void) {
  HMENU menu;
  POINT pt;
  if (!g_bridge) return;
  menu = CreatePopupMenu();
  if (!menu) return;
  AppendMenuW(menu, MF_STRING, BTN_TRAY_SHOW, L"显示窗口");
  AppendMenuW(menu, MF_SEPARATOR, 0, NULL);
  AppendMenuW(menu, MF_STRING, BTN_REST,   L"立即休息");
  AppendMenuW(menu, MF_STRING, BTN_RELOAD, L"重载配置");
  AppendMenuW(menu, MF_STRING, BTN_PARENT, L"家长设置（需密码）");
  GetCursorPos(&pt);
  SetForegroundWindow(g_bridge);
  TrackPopupMenuEx(menu, TPM_BOTTOMALIGN | TPM_LEFTALIGN, pt.x, pt.y, g_bridge, NULL);
  DestroyMenu(menu);
}

static void HideToTray(int balloon) {
  if (g_locked || !g_sdlWnd) return;
  TrayAdd();
  ShowWindow(g_sdlWnd, SW_HIDE);
  g_hidden = 1;
  TrayUpdateTip();
  LogEventW(L"窗口已隐藏到托盘（计时与锁屏继续运行）");
  if (balloon)
    TrayBalloon(L"窗口已隐藏，计时与锁屏继续运行。\n双击右下角图标可重新显示窗口。");
}

static void ShowFromTray(void) {
  if (g_locked || !g_sdlWnd) return;
  ShowWindow(g_sdlWnd, SW_RESTORE);
  ShowWindow(g_sdlWnd, SW_SHOW);
  SetForegroundWindow(g_sdlWnd);
  g_hidden = 0;
  TrayRemove();
  /* 在设置界面里被隐藏的话，恢复时要连窗口尺寸一起还原成状态窗口大小 */
  if (g_page == PAGE_SETTINGS || g_page == PAGE_ADVANCED) {
    LeaveSettings();
  } else {
    if (g_page != PAGE_STATUS) SwitchPage(PAGE_STATUS);
    PlaceStatusWindow();
    RefreshStatus();
  }
  /* 窗口是"从未显示过"的状态，显示后强制整窗重绘一次，避免第一帧是空白 */
  if (g_win) widget_invalidate_force(g_win, NULL);
  LogEventW(L"窗口已显示");
}

/* ---------------- AWTK 控件构建 ---------------- */
static widget_t* MakeLabel(widget_t* parent, int x, int y, int w, int h, int size, uint32_t fg) {
  widget_t* lab = label_create(parent, x, y, w, h);
  widget_set_style_int(lab, "font_size", size);
  widget_set_style_color(lab, "text_color", fg);
  widget_set_style_int(lab, "text_align_h", ALIGN_H_LEFT);
  widget_set_style_int(lab, "text_align_v", ALIGN_V_MIDDLE);
  return lab;
}

static widget_t* MakeCenterLabel(widget_t* parent, int x, int y, int w, int h, int size, uint32_t fg) {
  widget_t* lab = MakeLabel(parent, x, y, w, h, size, fg);
  widget_set_style_int(lab, "text_align_h", ALIGN_H_CENTER);
  return lab;
}

static widget_t* MakeButton(widget_t* parent, int x, int y, int w, int h, const WCHAR* text) {
  widget_t* btn = button_create(parent, x, y, w, h);
  SetTextW(btn, text);
  widget_set_style_int(btn, "font_size", 16);
  return btn;
}

/* 设置页用的小按钮（行内 [−] / [+] / 开关） */
static widget_t* MakeMiniButton(widget_t* parent, int x, int y, int w, int h,
                                const WCHAR* text, int font) {
  widget_t* btn = button_create(parent, x, y, w, h);
  SetTextW(btn, text);
  widget_set_style_int(btn, "font_size", font);
  return btn;
}

/* 普通文本输入框（不用密码掩码） */
static widget_t* MakeTextEdit(widget_t* parent, int x, int y, int w, int h, int size,
                              int maxLen) {
  widget_t* ed = edit_create(parent, x, y, w, h);
  edit_set_text_limit(ed, 0, (uint32_t)maxLen);
  edit_set_open_im_when_focused(ed, TRUE);
  edit_set_select_none_when_focused(ed, TRUE);
  widget_set_style_int(ed, "font_size", size);
  widget_set_style_color(ed, "text_color", col(20, 26, 36));
  widget_set_style_color(ed, "bg_color", col(238, 242, 248));
  return ed;
}

static widget_t* MakeEdit(widget_t* parent, int x, int y, int w, int h, int size) {
  widget_t* ed = edit_create(parent, x, y, w, h);
  /* 注意顺序：edit_set_text_limit 会把 input_type 重置成 INPUT_TEXT；
     edit_on_paint_self 每帧又会把密码掩码清掉，除非 input_type 是 INPUT_PASSWORD。
     所以密码相关设置必须放在最后。 */
  edit_set_text_limit(ed, 0, 64);
  /* 必须为 TRUE：AWTK 只在获得焦点时（open_im_when_focused）或用户点过编辑框时
     才把编辑框登记为输入法目标（input_method_request）；没登记的话
     SDL 的文字输入（WM_CHAR -> SDL_TEXTINPUT）会被 input_method_dispatch_to_widget
     直接丢掉，只有回车之类的按键事件能到达。
     桌面版 SDL 输入法不会弹软键盘，只是调 SDL_StartTextInput。 */
  edit_set_open_im_when_focused(ed, TRUE);
  edit_set_select_none_when_focused(ed, TRUE);
  widget_set_style_int(ed, "font_size", size);
  edit_set_input_type(ed, INPUT_PASSWORD);
  edit_set_password_visible(ed, FALSE);
  return ed;
}

/* 让编辑框拿到键盘焦点，并确保它是 SDL 文字输入的目标 */
static void FocusEdit(widget_t* ed) {
  if (ed == NULL) return;
  widget_set_focused(ed, TRUE);
  input_method_request(input_method(), ed);
}

static widget_t* MakePage(int w, int h, uint32_t bg, int baseFontSize) {
  widget_t* p = view_create(WIDGET(g_win), 0, 0, w, h);
  widget_set_style_color(p, "bg_color", bg);
  widget_set_style_int(p, "font_size", baseFontSize);
  widget_set_visible(p, FALSE);
  widget_set_sensitive(p, FALSE);
  return p;
}

static void BuildStatusPage(void) {
  widget_t* p = MakePage(STATUS_W, STATUS_H, col(18, 24, 34), 16);
  g_pages[PAGE_STATUS] = p;

  g_lblHead = MakeLabel(p, MARGIN, 14, STATUS_W - 2 * MARGIN, 32, 22, col(235, 240, 250));
  g_lblBig  = MakeLabel(p, MARGIN, 50, STATUS_W - 2 * MARGIN, 58, 44, col(120, 200, 255));

  /* 会话进度条：两个 view 叠出来（不依赖样式表里的 progress_bar） */
  g_barBg = view_create(p, MARGIN, 112, STATUS_W - 2 * MARGIN, 8);
  widget_set_style_color(g_barBg, "bg_color", col(40, 52, 72));
  g_barFg = view_create(WIDGET(g_barBg), 0, 0, STATUS_W - 2 * MARGIN, 8);
  widget_set_style_color(g_barFg, "bg_color", col(120, 200, 255));

  g_lblL1   = MakeLabel(p, MARGIN, 128, STATUS_W - 2 * MARGIN, 26, 16, col(210, 220, 235));
  g_lblL2   = MakeLabel(p, MARGIN, 154, STATUS_W - 2 * MARGIN, 26, 16, col(210, 220, 235));

  g_btnRest   = MakeButton(p, 14,  STATUS_H - 14 - BTN_H, BTN_W, BTN_H, L"立即休息");
  g_btnReload = MakeButton(p, 128, STATUS_H - 14 - BTN_H, BTN_W, BTN_H, L"重载配置");
  g_btnParent = MakeButton(p, 242, STATUS_H - 14 - BTN_H, BTN_W, BTN_H, L"家长设置");

  widget_on(g_btnRest,   EVT_CLICK, OnRestClick,   NULL);
  widget_on(g_btnReload, EVT_CLICK, OnReloadClick, NULL);
  widget_on(g_btnParent, EVT_CLICK, OnParentClick, NULL);
}

static void BuildParentPage(void) {
  widget_t* p = MakePage(STATUS_W, STATUS_H, col(18, 24, 34), 16);
  g_pages[PAGE_PARENT] = p;

  g_pTitle = MakeLabel(p, MARGIN, 12, STATUS_W - 2 * MARGIN, 34, 22, col(235, 240, 250));
  SetTextW(g_pTitle, L"家长设置");

  g_pText = MakeLabel(p, MARGIN, 50, STATUS_W - 2 * MARGIN, 26, 15, col(190, 200, 215));
  SetTextW(g_pText, L"打开配置文件与日志需要家长密码。");

  g_pLabel = MakeLabel(p, MARGIN, 84, 100, 30, 16, col(210, 220, 235));
  SetTextW(g_pLabel, L"家长密码");

  g_pEdit = MakeEdit(p, MARGIN + 106, 84, STATUS_W - 2 * MARGIN - 106, 30, 20);
  widget_on(g_pEdit, EVT_KEY_DOWN, OnEditKey, NULL);
  /* 点页面空白处也要把焦点拉回密码框（AWTK 会因点到非焦点控件而清掉焦点） */
  widget_on(p, EVT_POINTER_DOWN, OnPagePointerDown, g_pEdit);

  g_pErr = MakeLabel(p, MARGIN, 124, STATUS_W - 2 * MARGIN, 26, 15, col(255, 130, 130));

  {
    int by  = STATUS_H - 16 - BTN_H;
    int ccx = STATUS_W - MARGIN - BTN_W;
    int okx = ccx - GAP - BTN_W;
    g_pOk     = MakeButton(p, okx, by, BTN_W, BTN_H, L"确定");
    g_pCancel = MakeButton(p, ccx, by, BTN_W, BTN_H, L"取消");
  }
  widget_on(g_pOk,     EVT_CLICK, OnParentOk,     NULL);
  widget_on(g_pCancel, EVT_CLICK, OnParentCancel, NULL);
}

static void BuildLockPage(void) {
  widget_t* p = MakePage(STATUS_W, STATUS_H, col(10, 14, 22), 16);
  g_pages[PAGE_LOCK] = p;

  g_lTitle  = MakeCenterLabel(p, 0, 0, 10, 10, 48, col(255, 92, 92));
  SetTextW(g_lTitle, L"强制休息中");
  g_lUsed   = MakeCenterLabel(p, 0, 0, 10, 10, 26, col(225, 230, 240));
  g_lReason = MakeCenterLabel(p, 0, 0, 10, 10, 20, col(170, 180, 195));
  g_lCount  = MakeCenterLabel(p, 0, 0, 10, 10, 84, col(120, 200, 255));
  g_lHint   = MakeCenterLabel(p, 0, 0, 10, 10, 24, col(200, 210, 225));
  g_lFoot   = MakeCenterLabel(p, 0, 0, 10, 10, 16, col(150, 160, 175));
  SetTextW(g_lFoot, L"提前解锁必须输入密码");

  g_lEdit = MakeEdit(p, 0, 0, 10, 10, 22);
  widget_set_style_color(g_lEdit, "text_color", col(255, 255, 255));
  widget_set_style_color(g_lEdit, "bg_color", col(30, 38, 54));

  g_lBtn = MakeButton(p, 0, 0, 10, 10, L"解锁");

  widget_on(g_lEdit, EVT_KEY_DOWN, OnEditKey, NULL);
  widget_on(g_lBtn,  EVT_CLICK,    OnUnlockClick, NULL);
  /* 锁屏时点任何地方都保持密码框焦点，否则用户点一下屏幕就再也输不进密码 */
  widget_on(p, EVT_POINTER_DOWN, OnPagePointerDown, g_lEdit);
}

/* ---------------- 设置页（AWTK 表单） ---------------- */
static void RefreshSettings(void);
static void RefreshAdvanced(void);
static void ApplyLayoutSettings(int W, int H);
static void ApplyLayoutAdvanced(int W, int H);
static ret_t OnNumMinus(void* ctx, event_t* e);
static ret_t OnNumPlus(void* ctx, event_t* e);
static ret_t OnBoolToggle(void* ctx, event_t* e);
static ret_t OnSettingsSave(void* ctx, event_t* e);
static ret_t OnSettingsCancel(void* ctx, event_t* e);
static ret_t OnSettingsDefault(void* ctx, event_t* e);
static ret_t OnSettingsAdvanced(void* ctx, event_t* e);
static ret_t OnAdvancedBack(void* ctx, event_t* e);
static ret_t OnAdvancedSave(void* ctx, event_t* e);
static ret_t OnOpenDataDir(void* ctx, event_t* e);

static int RowIndex(void* ctx) { return (int)(size_t)ctx; }

static void SetMsg(widget_t* lab, const WCHAR* text) {
  if (lab != NULL) SetTextW(lab, text ? text : L"");
}

/* 建一个「标签 + [−] 数值 [+]」或「标签 + 开关」行 */
static void BuildSettingsPage(void) {
  widget_t* p = MakePage(STATUS_W, STATUS_H, col(20, 26, 36), 15);
  int i;
  g_pages[PAGE_SETTINGS] = p;

  g_sTitle = MakeLabel(p, MARGIN, 12, ST_W - 2 * MARGIN, 30, 22, col(235, 240, 250));
  SetTextW(g_sTitle, L"家长设置");
  g_sHint = MakeLabel(p, MARGIN, 44, ST_W - 2 * MARGIN, 20, 13, col(150, 160, 175));
  StringCchPrintfW(g_sHintText, 256,
                   L"配置以加密形式保存在 config.dat（界面版本 %s）；"
                   L"点「保存」立即生效，限时/休息改动会开始新的计时会话", ST_VERSION);
  SetTextW(g_sHint, g_sHintText);
  g_sMsg = MakeLabel(p, MARGIN, 388, ST_W - 2 * MARGIN, 20, 13, col(140, 230, 170));

  for (i = 0; i < ST_ROW_N; i++) {
    g_rowLabel[i] = MakeLabel(p, 0, 0, 10, 10, 14, col(205, 215, 230));
    SetTextW(g_rowLabel[i], kRows[i].label);
    if (kRows[i].kind == ROW_NUM) {
      g_rowMinus[i] = MakeMiniButton(p, 0, 0, 10, 10, L"-", 18);
      g_rowVal[i]   = MakeCenterLabel(p, 0, 0, 10, 10, 16, col(120, 200, 255));
      g_rowPlus[i]  = MakeMiniButton(p, 0, 0, 10, 10, L"+", 18);
      widget_on(g_rowMinus[i], EVT_CLICK, OnNumMinus, (void*)(size_t)i);
      widget_on(g_rowPlus[i],  EVT_CLICK, OnNumPlus,  (void*)(size_t)i);
    } else {
      g_rowBtn[i] = MakeMiniButton(p, 0, 0, 10, 10, L"开", 15);
      widget_on(g_rowBtn[i], EVT_CLICK, OnBoolToggle, (void*)(size_t)i);
    }
  }

  g_sBtnAdv     = MakeMiniButton(p, 0, 0, 10, 10, L"识别与密码", 15);
  g_sBtnDefault = MakeMiniButton(p, 0, 0, 10, 10, L"恢复默认", 15);
  g_sBtnCancel  = MakeMiniButton(p, 0, 0, 10, 10, L"取消", 15);
  g_sBtnSave    = MakeMiniButton(p, 0, 0, 10, 10, L"保存", 15);
  widget_on(g_sBtnAdv,     EVT_CLICK, OnSettingsAdvanced, NULL);
  widget_on(g_sBtnDefault, EVT_CLICK, OnSettingsDefault,  NULL);
  widget_on(g_sBtnCancel,  EVT_CLICK, OnSettingsCancel,   NULL);
  widget_on(g_sBtnSave,    EVT_CLICK, OnSettingsSave,     NULL);
}

static void BuildAdvancedPage(void) {
  widget_t* p = MakePage(STATUS_W, STATUS_H, col(20, 26, 36), 15);
  g_pages[PAGE_ADVANCED] = p;

  g_aTitle = MakeLabel(p, MARGIN, 12, ST_W - 2 * MARGIN, 30, 22, col(235, 240, 250));
  SetTextW(g_aTitle, L"识别与密码");
  g_aHint = MakeLabel(p, MARGIN, 44, ST_W - 2 * MARGIN, 20, 13, col(150, 160, 175));
  SetTextW(g_aHint, L"截图会送给下面这个模型识别；解锁密码只保存 PBKDF2 哈希，不保存明文");

  g_aUrlLabel = MakeLabel(p, MARGIN, 80, ST_LABEL_W, 30, 14, col(205, 215, 230));
  SetTextW(g_aUrlLabel, L"接口地址");
  g_aUrlEdit = MakeTextEdit(p, 176, 80, ST_W - MARGIN - 176, 30, 14, 250);

  g_aModelLabel = MakeLabel(p, MARGIN, 124, ST_LABEL_W, 30, 14, col(205, 215, 230));
  SetTextW(g_aModelLabel, L"模型名称");
  g_aModelEdit = MakeTextEdit(p, 176, 124, ST_W - MARGIN - 176, 30, 14, 120);

  g_aKeyLabel = MakeLabel(p, MARGIN, 168, ST_LABEL_W, 30, 14, col(205, 215, 230));
  SetTextW(g_aKeyLabel, L"API 密钥");
  g_aKeyEdit = MakeTextEdit(p, 176, 168, ST_W - MARGIN - 176, 30, 14, 120);

  g_aPwNote = MakeLabel(p, MARGIN, 214, ST_W - 2 * MARGIN, 20, 14, col(150, 160, 175));
  SetTextW(g_aPwNote, L"修改解锁密码（留空表示不修改）");

  g_aPw1Label = MakeLabel(p, MARGIN, 244, ST_LABEL_W, 30, 14, col(205, 215, 230));
  SetTextW(g_aPw1Label, L"新密码");
  g_aPw1Edit = MakeEdit(p, 176, 244, 240, 30, 16);

  g_aPw2Label = MakeLabel(p, MARGIN, 288, ST_LABEL_W, 30, 14, col(205, 215, 230));
  SetTextW(g_aPw2Label, L"确认新密码");
  g_aPw2Edit = MakeEdit(p, 176, 288, 240, 30, 16);

  g_aDataLabel = MakeCenterLabel(p, MARGIN, 330, ST_W - 2 * MARGIN, 28, 13,
                                 col(120, 200, 255));
  SetTextW(g_aDataLabel, g_logDir);

  g_aMsg = MakeLabel(p, MARGIN, 388, ST_W - 2 * MARGIN, 20, 13, col(255, 150, 150));

  g_aBtnBack = MakeMiniButton(p, 0, 0, 10, 10, L"返回", 15);
  g_aBtnData = MakeMiniButton(p, 0, 0, 10, 10, L"打开日志目录", 15);
  g_aBtnSave = MakeMiniButton(p, 0, 0, 10, 10, L"保存", 15);
  widget_on(g_aBtnBack, EVT_CLICK, OnAdvancedBack, NULL);
  widget_on(g_aBtnData, EVT_CLICK, OnOpenDataDir,  NULL);
  widget_on(g_aBtnSave, EVT_CLICK, OnAdvancedSave, NULL);
}

static void ApplyLayoutSettings(int W, int H) {
  int i;
  int y0 = 74;
  int bw = (W - 2 * MARGIN - 3 * GAP) / 4;
  int total = 4 * bw + 3 * GAP;
  int x0 = (W - total) / 2;
  int by = H - 14 - BTN_H;

  widget_move_resize(g_sTitle, MARGIN, 12, W - 2 * MARGIN, 30);
  widget_move_resize(g_sHint,  MARGIN, 44, W - 2 * MARGIN, 20);
  widget_move_resize(g_sMsg,   MARGIN, by - 24, W - 2 * MARGIN, 20);

  for (i = 0; i < ST_ROW_N; i++) {
    int col = (i < 8) ? 0 : 1;
    int r   = (i < 8) ? i : (i - 8);
    int x   = (col == 0) ? ST_COL0 : (W - MARGIN - ST_COL_W);
    int y   = y0 + r * ST_ROW_H;
    widget_move_resize(g_rowLabel[i], x, y, ST_LABEL_W, 30);
    if (kRows[i].kind == ROW_NUM) {
      int mx = x + ST_LABEL_W;
      widget_move_resize(g_rowMinus[i], mx, y + 2, ST_STEP_W, 26);
      widget_move_resize(g_rowVal[i],   mx + ST_STEP_W + 4, y, ST_NUM_W, 30);
      widget_move_resize(g_rowPlus[i],  mx + ST_STEP_W + ST_NUM_W + 8, y + 2, ST_STEP_W, 26);
    } else {
      widget_move_resize(g_rowBtn[i], x + ST_LABEL_W, y + 2, 84, 26);
    }
  }

  widget_move_resize(g_sBtnAdv,     x0, by, bw, BTN_H);
  widget_move_resize(g_sBtnDefault, x0 + bw + GAP, by, bw, BTN_H);
  widget_move_resize(g_sBtnCancel,  x0 + 2 * (bw + GAP), by, bw, BTN_H);
  widget_move_resize(g_sBtnSave,    x0 + 3 * (bw + GAP), by, bw, BTN_H);
}

static void ApplyLayoutAdvanced(int W, int H) {
  int ew = W - MARGIN - 176;
  int bw = (W - 2 * MARGIN - 2 * GAP) / 3;
  int total = 3 * bw + 2 * GAP;
  int x0 = (W - total) / 2;
  int by = H - 14 - BTN_H;

  widget_move_resize(g_aTitle, MARGIN, 12, W - 2 * MARGIN, 30);
  widget_move_resize(g_aHint,  MARGIN, 44, W - 2 * MARGIN, 20);
  widget_move_resize(g_aUrlLabel, MARGIN, 80, ST_LABEL_W, 30);
  widget_move_resize(g_aUrlEdit,  176, 80, ew, 30);
  widget_move_resize(g_aModelLabel, MARGIN, 124, ST_LABEL_W, 30);
  widget_move_resize(g_aModelEdit,  176, 124, ew, 30);
  widget_move_resize(g_aKeyLabel, MARGIN, 168, ST_LABEL_W, 30);
  widget_move_resize(g_aKeyEdit,  176, 168, ew, 30);
  widget_move_resize(g_aPwNote, MARGIN, 214, W - 2 * MARGIN, 20);
  widget_move_resize(g_aPw1Label, MARGIN, 244, ST_LABEL_W, 30);
  widget_move_resize(g_aPw1Edit,  176, 244, 240, 30);
  widget_move_resize(g_aPw2Label, MARGIN, 288, ST_LABEL_W, 30);
  widget_move_resize(g_aPw2Edit,  176, 288, 240, 30);
  widget_move_resize(g_aDataLabel, MARGIN, 326, W - 2 * MARGIN, 28);
  widget_move_resize(g_aMsg, MARGIN, by - 24, W - 2 * MARGIN, 20);

  widget_move_resize(g_aBtnData, x0, by, bw, BTN_H);
  widget_move_resize(g_aBtnBack, x0 + bw + GAP, by, bw, BTN_H);
  widget_move_resize(g_aBtnSave, x0 + 2 * (bw + GAP), by, bw, BTN_H);
}

static void RefreshSettings(void) {
  int i;
  WCHAR b[40];
  for (i = 0; i < ST_ROW_N; i++) {
    if (kRows[i].kind == ROW_NUM) {
      int v = *kRows[i].field;
      StringCchPrintfW(b, 40, L"%d", v);
      if (g_rowVal[i]) SetTextW(g_rowVal[i], b);
      if (g_rowMinus[i]) widget_set_sensitive(g_rowMinus[i], v > kRows[i].min);
      if (g_rowPlus[i])  widget_set_sensitive(g_rowPlus[i],  v < kRows[i].max);
    } else if (g_rowBtn[i]) {
      SetTextW(g_rowBtn[i], *kRows[i].field ? L"开" : L"关");
    }
  }
}

static void RefreshAdvanced(void) {
  widget_set_text_utf8(g_aUrlEdit, "");
  {
    char u8[512];
    WToUtf8(g_cfg.api_url, u8, (int)sizeof(u8));
    widget_set_text_utf8(g_aUrlEdit, u8);
    WToUtf8(g_cfg.model, u8, (int)sizeof(u8));
    widget_set_text_utf8(g_aModelEdit, u8);
    WToUtf8(g_cfg.api_key, u8, (int)sizeof(u8));
    widget_set_text_utf8(g_aKeyEdit, u8);
  }
  widget_set_text_utf8(g_aPw1Edit, "");
  widget_set_text_utf8(g_aPw2Edit, "");
}

/* 取编辑框里的文本（UTF-8 -> 宽字符） */
static void EditTextW(widget_t* ed, WCHAR* out, int cap) {
  char u8[1024];
  u8[0] = 0;
  out[0] = 0;
  if (ed == NULL) return;
  widget_get_text_utf8(ed, u8, (int)sizeof(u8));
  Utf8ToW(u8, out, cap);
  out[cap - 1] = 0;
}

static ret_t OnNumMinus(void* ctx, event_t* e) {
  int i = RowIndex(ctx);
  int v;
  (void)e;
  v = *kRows[i].field - kRows[i].step;
  if (v < kRows[i].min) v = kRows[i].min;
  *kRows[i].field = v;
  SetMsg(g_sMsg, L"");
  RefreshSettings();
  return RET_OK;
}

static ret_t OnNumPlus(void* ctx, event_t* e) {
  int i = RowIndex(ctx);
  int v;
  (void)e;
  v = *kRows[i].field + kRows[i].step;
  if (v > kRows[i].max) v = kRows[i].max;
  *kRows[i].field = v;
  SetMsg(g_sMsg, L"");
  RefreshSettings();
  return RET_OK;
}

static ret_t OnBoolToggle(void* ctx, event_t* e) {
  int i = RowIndex(ctx);
  (void)e;
  *kRows[i].field = *kRows[i].field ? 0 : 1;
  SetMsg(g_sMsg, L"");
  RefreshSettings();
  return RET_OK;
}

/* 保存：把工作副本写回 g_cfg 并加密落盘 */
static int SettingsCommit(int fromAdvanced) {
  WCHAR wa[512], wb[512], pw1[128], pw2[128];
  int oldSession = g_cfg.session_minutes;
  int oldBreak   = g_cfg.break_minutes;
  int sessionChanged;

  /* 文本框 */
  EditTextW(g_aUrlEdit, wa, 256);
  if (wa[0]) StringCchCopyW(g_edit.api_url, 256, wa);
  EditTextW(g_aModelEdit, wb, 128);
  if (wb[0]) StringCchCopyW(g_edit.model, 128, wb);
  EditTextW(g_aKeyEdit, wa, 128);
  StringCchCopyW(g_edit.api_key, 128, wa);

  /* 新密码 */
  EditTextW(g_aPw1Edit, pw1, 128);
  EditTextW(g_aPw2Edit, pw2, 128);
  if (pw1[0]) {
    if (wcscmp(pw1, pw2) != 0) {
      SetMsg(g_aMsg, L"两次输入的新密码不一致，没有保存");
      if (fromAdvanced) SwitchPage(PAGE_ADVANCED);
      return 0;
    }
    if (wcslen(pw1) < 4) {
      SetMsg(g_aMsg, L"新密码太短（至少 4 位），没有保存");
      if (fromAdvanced) SwitchPage(PAGE_ADVANCED);
      return 0;
    }
    StringCchCopyW(g_edit.password, 64, pw1);
  }

  g_cfg = g_edit;
  g_cfg.password[0] = pw1[0] ? g_edit.password[0] : 0;

  HooksSetBlockHotkeys(g_cfg.block_hotkeys ? 1 : 0);

  /* 顺序很重要：ConfigSave() 写盘成功后会把 c->password 清零（明文不留在内存里），
     所以必须先把新的明文密码派生成哈希，再做整体保存。
     反过来的话明文在到达哈希函数前就被清掉，表现是"保存成功但密码没变"（曾经的真 bug）。 */
  if (g_cfg.password[0]) {
    if (!ConfigFinalizePassword(&g_cfg, g_configPath)) {
      SetMsg(fromAdvanced ? g_aMsg : g_sMsg, L"写入 config.dat 失败");
      LogEventW(L"家长设置保存失败：无法写入 %s", g_configPath);
      return 0;
    }
    LogEventW(L"解锁密码已通过家长设置更新");
  }

  if (!ConfigSave(&g_cfg, g_configPath)) {
    SetMsg(fromAdvanced ? g_aMsg : g_sMsg, L"写入 config.dat 失败");
    LogEventW(L"家长设置保存失败：无法写入 %s", g_configPath);
    return 0;
  }

  sessionChanged = (oldSession != g_cfg.session_minutes) || (oldBreak != g_cfg.break_minutes);
  LogEventW(L"家长设置已保存：限时 %d 分钟，休息 %d 分钟，截图 %s，间隔 %d 秒，"
            L"存图 %s，识别 %s",
            g_cfg.session_minutes, g_cfg.break_minutes,
            g_cfg.screenshot_enabled ? L"开" : L"关", g_cfg.screenshot_interval,
            g_cfg.save_screenshots ? L"开" : L"关", g_cfg.vision_enabled ? L"开" : L"关");
  if (sessionChanged) {
    SessionReset((LONGLONG)g_cfg.session_minutes * 60000, L"家长修改了限时配置");
  }
  return 1;
}

static ret_t OnSettingsSave(void* ctx, event_t* e) {
  (void)ctx; (void)e;
  if (SettingsCommit(0)) LeaveSettings();
  return RET_OK;
}

static ret_t OnAdvancedSave(void* ctx, event_t* e) {
  (void)ctx; (void)e;
  if (SettingsCommit(1)) LeaveSettings();
  return RET_OK;
}

static ret_t OnSettingsCancel(void* ctx, event_t* e) {
  (void)ctx; (void)e;
  LeaveSettings();
  return RET_OK;
}

static ret_t OnSettingsDefault(void* ctx, event_t* e) {
  Config pw;
  (void)ctx; (void)e;
  ConfigDefaults(&pw);
  g_edit = pw;
  /* 保留当前密码与识别配置：恢复默认只重置时间/截图/界面类开关 */
  StringCchCopyW(g_edit.pw_hash, ST_PW_HASH_HEX, g_cfg.pw_hash);
  StringCchCopyW(g_edit.pw_salt, ST_PW_SALT_HEX, g_cfg.pw_salt);
  g_edit.pw_iterations = g_cfg.pw_iterations;
  StringCchCopyW(g_edit.api_url, 256, g_cfg.api_url);
  StringCchCopyW(g_edit.model, 128, g_cfg.model);
  StringCchCopyW(g_edit.api_key, 128, g_cfg.api_key);
  SetMsg(g_sMsg, L"已恢复默认值（密码与识别配置保留），点「保存」生效");
  RefreshSettings();
  return RET_OK;
}

static ret_t OnSettingsAdvanced(void* ctx, event_t* e) {
  (void)ctx; (void)e;
  ClearMsgAll();
  RefreshAdvanced();
  SwitchPage(PAGE_ADVANCED);
  FocusEdit(g_aUrlEdit);
  return RET_OK;
}

static ret_t OnAdvancedBack(void* ctx, event_t* e) {
  (void)ctx; (void)e;
  RefreshSettings();
  SwitchPage(PAGE_SETTINGS);
  return RET_OK;
}

static ret_t OnOpenDataDir(void* ctx, event_t* e) {
  (void)ctx; (void)e;
  ShellExecuteW(NULL, L"open", g_shotDir, NULL, g_baseDir, SW_SHOWNORMAL);
  ShellExecuteW(NULL, L"open", g_logDir, NULL, g_baseDir, SW_SHOWNORMAL);
  SetMsg(g_aMsg, L"已打开截图与日志目录");
  return RET_OK;
}

/* 进入/退出设置：窗口临时放大 / 还原 */
static void ResizeWindowTo(int lw, int lh) {
  RECT wr, cr, wa;
  int fw, fh, pw, ph, x, y;
  if (!g_sdlWnd) return;
  GetWindowRect(g_sdlWnd, &wr);
  GetClientRect(g_sdlWnd, &cr);
  fw = (wr.right - wr.left) - cr.right;
  fh = (wr.bottom - wr.top) - cr.bottom;
  pw = (int)(lw * g_scale + 0.5) + fw;
  ph = (int)(lh * g_scale + 0.5) + fh;
  SystemParametersInfoW(SPI_GETWORKAREA, 0, &wa, 0);
  x = wa.right - pw - 24;
  y = wa.bottom - ph - 12;
  if (x < wa.left) x = wa.left;
  if (y < wa.top) y = wa.top;
  SetWindowPos(g_sdlWnd, NULL, x, y, pw, ph, SWP_NOZORDER | SWP_NOACTIVATE);
}

static void ClearMsgAll(void) {
  SetMsg(g_sMsg, L"");
  SetMsg(g_aMsg, L"");
}

static void EnterSettings(void) {
  g_edit = g_cfg;                 /* 工作副本：取消就丢弃 */
  g_edit.password[0] = 0;
  ResizeWindowTo(ST_W, ST_H);
  ClearMsgAll();
  RefreshSettings();
  RefreshAdvanced();
  SwitchPage(PAGE_SETTINGS);
}

static void LeaveSettings(void) {
  ResizeWindowTo(STATUS_W, STATUS_H);
  SwitchPage(PAGE_STATUS);
  PlaceStatusWindow();
  RefreshStatus();
  SetMsg(g_sMsg, L"");
}

/* ---------------- 页面切换与布局 ---------------- */
/* ---------------- 休息前倒计时提醒页（AWTK 页面，跨平台） ----------------
   距离强制休息还剩 warn_minutes 分钟（默认 5）时切到这个页面：
   右下角那个窗口里显示倒计时，点「知道了」关掉（关掉后本次会话不再弹）。
   全部用 AWTK 控件实现，没有平台相关代码，将来移植到 Linux 时照旧可用。 */
static widget_t* g_warnTitle = NULL;
static widget_t* g_warnBig = NULL;
static widget_t* g_warnHint = NULL;
static widget_t* g_warnBtn = NULL;
static widget_t* g_warnBar = NULL;
static int g_warnWasHidden = 0;   /* 提醒时把窗口从托盘弹出来了，关掉后要再收回去 */
static int g_warnOnTop = 0;       /* 提醒期间临时置顶，保证不被别的窗口盖住 */

/* 提醒期间置顶（不抢焦点），结束后恢复普通层级 */
static void WarnSetTop(int on) {
  if (g_sdlWnd == NULL || g_warnOnTop == on) return;
  g_warnOnTop = on;
  SetWindowPos(g_sdlWnd, on ? HWND_TOPMOST : HWND_NOTOPMOST, 0, 0, 0, 0,
               SWP_NOMOVE | SWP_NOSIZE | SWP_NOACTIVATE);
}

static void WarnUpdateCountdown(LONGLONG secs) {
  WCHAR buf[32];
  if (g_warnBig == NULL) return;
  StringCchPrintfW(buf, 32, L"%02lld:%02lld", (secs / 60) % 100, secs % 60);
  SetTextW(g_warnBig, buf);
}

static ret_t OnWarnClose(void* ctx, event_t* e) {
  (void)ctx;
  (void)e;
  g_warnDismissed = 1;
  WarnSetTop(0);
  SwitchPage(PAGE_STATUS);
  if (g_warnWasHidden) {
    g_warnWasHidden = 0;
    HideToTray(0);
  }
  LogEventW(L"用户关闭了休息提醒（本次会话不再弹出）");
  return RET_OK;
}

static void BuildWarnPage(void) {
  widget_t* p = MakePage(STATUS_W, STATUS_H, col(28, 24, 18), 16);
  g_pages[PAGE_WARN] = p;

  /* 顶部一条琥珀色横条，一眼能看出是"提醒"而不是出错 */
  g_warnBar = view_create(p, 0, 0, STATUS_W, 4);
  widget_set_style_color(g_warnBar, "bg_color", col(255, 176, 60));

  g_warnTitle = MakeLabel(p, MARGIN, 18, STATUS_W - 2 * MARGIN, 30, 21, col(255, 196, 110));
  SetTextW(g_warnTitle, L"马上要休息了");

  g_warnBig = MakeLabel(p, MARGIN, 50, STATUS_W - 2 * MARGIN, 62, 44, col(255, 255, 255));
  SetTextW(g_warnBig, L"05:00");

  g_warnHint = MakeLabel(p, MARGIN, 116, STATUS_W - 2 * MARGIN, 26, 15, col(196, 186, 168));
  SetTextW(g_warnHint, L"到点会自动锁屏，先把手头的事收个尾");

  g_warnBtn = MakeButton(p, STATUS_W - MARGIN - 132, STATUS_H - 14 - BTN_H, 132, BTN_H, L"知道了");
  widget_on(g_warnBtn, EVT_CLICK, OnWarnClose, NULL);
}

static void ApplyLayoutWarn(int W, int H) {
  if (g_pages[PAGE_WARN] == NULL) return;
  widget_move_resize(g_warnBar,   0, 0, W, 4);
  widget_move_resize(g_warnTitle, MARGIN, 18, W - 2 * MARGIN, 30);
  widget_move_resize(g_warnBig,   MARGIN, 50, W - 2 * MARGIN, 62);
  widget_move_resize(g_warnHint,  MARGIN, 116, W - 2 * MARGIN, 26);
  widget_move_resize(g_warnBtn,   W - MARGIN - 132, H - 14 - BTN_H, 132, BTN_H);
}

static void WarnTick(void) {
  static LONGLONG lastSec = -1;
  LONGLONG remain = g_sessionLenMs - g_usageMs;
  LONGLONG secs = remain > 0 ? remain / 1000 : 0;
  int want;

  if (g_warned == 0) {              /* 新会话：允许再次弹出 */
    g_warnDismissed = 0;
    lastSec = -1;
  }

  /* 只从状态页进入；家长验证/设置页面开着的时候不打断用户 */
  want = (!g_locked && !g_warnDismissed && g_cfg.warn_minutes > 0 && remain > 0 &&
          remain <= (LONGLONG)g_cfg.warn_minutes * 60000 &&
          (g_page == PAGE_STATUS || g_page == PAGE_WARN));

  if (!want) {
    if (g_page == PAGE_WARN) {
      WarnSetTop(0);
      SwitchPage(PAGE_STATUS);
      if (g_warnWasHidden) {
        g_warnWasHidden = 0;
        HideToTray(0);
      }
    }
    return;
  }

  if (g_page != PAGE_WARN) {
    if (g_hidden) {                 /* 平时藏在托盘里也要弹出来给用户看见 */
      ShowFromTray();
      g_warnWasHidden = 1;
    }
    WarnSetTop(1);                  /* 临时置顶，免得被浏览器之类的窗口盖住 */
    SwitchPage(PAGE_WARN);
    lastSec = -1;
    LogEventW(L"弹出休息提醒：距离强制休息还剩 %lld 分 %lld 秒", (secs / 60) % 100, secs % 60);
  }
  if (secs != lastSec) {            /* 每秒刷新一次倒计时 */
    lastSec = secs;
    WarnUpdateCountdown(secs);
  }
}
static void SwitchPage(int page) {
  int i;
  if (page < 0 || page >= PAGE_NR) return;
  for (i = 0; i < PAGE_NR; i++) {
    if (g_pages[i]) {
      bool_t on = (i == page);
      /* 三个页面整窗重叠，AWTK 命中测试不检查 visible，
         必须把非当前页设为 insensitive，否则会吞掉鼠标事件。 */
      widget_set_visible(g_pages[i], on);
      widget_set_sensitive(g_pages[i], on);
    }
  }
  g_page = page;
  LayoutPage(page);
  if (g_win) widget_invalidate_force(g_win, NULL);
}

static void LayoutPage(int page) {
  int W = g_win ? g_win->w : STATUS_W;
  int H = g_win ? g_win->h : STATUS_H;
  int i;
  (void)page;
  if (W <= 0 || H <= 0) return;

  for (i = 0; i < PAGE_NR; i++) {
    if (g_pages[i]) widget_move_resize(g_pages[i], 0, 0, W, H);
  }

  /* 设置页按固定的逻辑尺寸排版（进入设置时窗口会被放大到同样尺寸），
     免得等 AWTK 回传新尺寸时先闪一帧被裁切的界面。 */
  widget_move_resize(g_pages[PAGE_SETTINGS], 0, 0, ST_W, ST_H);
  widget_move_resize(g_pages[PAGE_ADVANCED], 0, 0, ST_W, ST_H);
  ApplyLayoutSettings(ST_W, ST_H);
  ApplyLayoutAdvanced(ST_W, ST_H);
  ApplyLayoutWarn(W, H);

  /* 状态页：三个按钮按窗口宽度均分（左右留 MARGIN，按钮之间留 GAP），
     否则按钮挤在左边、右边空一大块，看着不协调。 */
  {
    int by = H - 14 - BTN_H;
    int bw = (W - 2 * MARGIN - 2 * GAP) / 3;
    int total, x0;
    if (bw > 150) bw = 150;
    if (bw < 80) bw = 80;
    total = 3 * bw + 2 * GAP;
    x0 = (W - total) / 2;
    if (x0 < 6) x0 = 6;
    widget_move_resize(g_lblHead, MARGIN, 14, W - 2 * MARGIN, 32);
    widget_move_resize(g_lblBig,  MARGIN, 50, W - 2 * MARGIN, 58);
    widget_move_resize(g_barBg,   MARGIN, 112, W - 2 * MARGIN, 8);
    widget_move_resize(g_lblL1,   MARGIN, 128, W - 2 * MARGIN, 26);
    widget_move_resize(g_lblL2,   MARGIN, 154, W - 2 * MARGIN, 26);
    widget_move_resize(g_btnRest,   x0, by, bw, BTN_H);
    widget_move_resize(g_btnReload, x0 + bw + GAP, by, bw, BTN_H);
    widget_move_resize(g_btnParent, x0 + 2 * (bw + GAP), by, bw, BTN_H);
  }

  {
    int by  = H - 16 - BTN_H;
    int ccx = W - MARGIN - BTN_W;
    int okx = ccx - GAP - BTN_W;
    widget_move_resize(g_pTitle, MARGIN, 12, W - 2 * MARGIN, 34);
    widget_move_resize(g_pText,  MARGIN, 50, W - 2 * MARGIN, 26);
    widget_move_resize(g_pLabel, MARGIN, 84, 100, 30);
    widget_move_resize(g_pEdit,  MARGIN + 106, 84, W - 2 * MARGIN - 106, 30);
    widget_move_resize(g_pErr,   MARGIN, 124, W - 2 * MARGIN, 26);
    widget_move_resize(g_pOk,     okx, by, BTN_W, BTN_H);
    widget_move_resize(g_pCancel, ccx, by, BTN_W, BTN_H);
  }

  /* 锁屏页：矮屏（< 660 逻辑像素高，约 1024x768 及以下）换成紧凑排版，
     否则倒计时和下面的提示行会撞在一起。字号用 widget_set_style_int 动态改。 */
  {
    int compact = (H < 660);
    int fsTitle  = compact ? 34 : 48;
    int fsUsed   = compact ? 20 : 26;
    int fsReason = compact ? 16 : 20;
    int fsCount  = compact ? 56 : 84;
    int fsHint   = compact ? 18 : 24;
    int fsFoot   = compact ? 13 : 16;
    int hTitle   = compact ? 44 : 62;
    int hUsed    = compact ? 26 : 36;
    int hReason  = compact ? 22 : 30;
    int hCount   = compact ? 70 : 104;
    int hHint    = compact ? 26 : 34;
    int hFoot    = compact ? 22 : 28;
    int ew = compact ? 300 : 340, eh = compact ? 36 : 44;
    int bw = compact ? 160 : 180, bh = compact ? 34 : 42;
    int yTitle  = (int)(H * (compact ? 0.08 : 0.12));
    int yUsed   = (int)(H * (compact ? 0.22 : 0.28));
    int yReason = (int)(H * (compact ? 0.27 : 0.34));
    int yCount  = (int)(H * (compact ? 0.32 : 0.40));
    int yHint   = (int)(H * (compact ? 0.47 : 0.55));
    int ey      = (int)(H * (compact ? 0.56 : 0.62));
    int yFoot   = (int)(H * (compact ? 0.86 : 0.80));

    if (ew > W - 40) ew = W - 40;
    if (bw > W - 40) bw = W - 40;
    if (ey + eh + bh + 12 > yFoot) ey = yFoot - eh - bh - 12;
    if (ey < yHint + hHint) ey = yHint + hHint;
    if (ey + eh + bh > H) ey = H - eh - bh;

    widget_set_style_int(g_lTitle,  "font_size", fsTitle);
    widget_set_style_int(g_lUsed,   "font_size", fsUsed);
    widget_set_style_int(g_lReason, "font_size", fsReason);
    widget_set_style_int(g_lCount,  "font_size", fsCount);
    widget_set_style_int(g_lHint,   "font_size", fsHint);
    widget_set_style_int(g_lFoot,   "font_size", fsFoot);

    widget_move_resize(g_lTitle,  0, yTitle,  W, hTitle);
    widget_move_resize(g_lUsed,   0, yUsed,   W, hUsed);
    widget_move_resize(g_lReason, 0, yReason, W, hReason);
    widget_move_resize(g_lCount,  0, yCount,  W, hCount);
    widget_move_resize(g_lHint,   0, yHint,   W, hHint);
    widget_move_resize(g_lEdit,   (W - ew) / 2, ey, ew, eh);
    widget_move_resize(g_lBtn,    (W - bw) / 2, ey + eh + 14, bw, bh);
    widget_move_resize(g_lFoot,   0, yFoot,   W, hFoot);
  }

  g_lastW = W;
  g_lastH = H;
}

/* ---------------- 内容刷新 ---------------- */
static void RefreshStatus(void) {
  LONGLONG len    = g_sessionLenMs;
  LONGLONG used   = g_usageMs;
  LONGLONG remain = len - used;
  int warning;
  WCHAR head[160], big[64], l1[200], l2[200];
  SYSTEMTIME lockAt;

  if (remain < 0) remain = 0;
  warning = (g_cfg.warn_minutes > 0 && remain <= (LONGLONG)g_cfg.warn_minutes * 60000);

  StringCchPrintfW(head, 160, L"屏幕使用时间管理　%s",
                   g_locked ? L"（锁屏中）" : (warning ? L"（即将锁屏）" : L"（正常）"));
  SetTextW(g_lblHead, head);
  widget_set_style_color(g_lblHead, "text_color",
                         warning ? col(255, 120, 120) : col(235, 240, 250));

  {
    LONGLONG rs = remain / 1000;
    StringCchPrintfW(big, 64, L"剩余 %02lld:%02lld", (rs / 60) % 100, rs % 60);
    SetTextW(g_lblBig, big);
  }
  widget_set_style_color(g_lblBig, "text_color",
                         warning ? col(255, 150, 90) : col(120, 200, 255));

  {
    LONGLONG us = used / 1000;
    StringCchPrintfW(l1, 200, L"本次会话 %lld 分钟　已用 %02lld:%02lld",
                     len / 60000, (us / 60) % 100, us % 60);
    SetTextW(g_lblL1, l1);
  }

  {
    FILETIME ft;
    ULONGLONG now100, target;
    GetSystemTimeAsFileTime(&ft);
    now100 = ((ULONGLONG)ft.dwHighDateTime << 32) | ft.dwLowDateTime;
    target = now100 + (ULONGLONG)(remain / 1000) * 10000ULL;
    ft.dwLowDateTime  = (DWORD)(target & 0xFFFFFFFF);
    ft.dwHighDateTime = (DWORD)(target >> 32);
    FileTimeToSystemTime(&ft, &lockAt);
    SystemTimeToTzSpecificLocalTime(NULL, &lockAt, &lockAt);
  }
  StringCchPrintfW(l2, 200, L"将在 %02d:%02d:%02d 锁屏休息 %d 分钟",
                   lockAt.wHour, lockAt.wMinute, lockAt.wSecond, g_cfg.break_minutes);
  SetTextW(g_lblL2, l2);

  /* 进度条：剩余比例（和「剩余 mm:ss」一致，越用越短，快到时变红） */
  if (g_barFg != NULL) {
    int W = g_win ? g_win->w : STATUS_W;
    int barW = W - 2 * MARGIN;
    int fill = 0;
    if (barW < 0) barW = 0;
    if (len > 0) fill = (int)((double)remain * barW / (double)len);
    if (fill < 0) fill = 0;
    if (fill > barW) fill = barW;
    widget_move_resize(g_barFg, 0, 0, fill, 8);
    widget_set_style_color(g_barFg, "bg_color",
                           warning ? col(255, 120, 120) : col(120, 200, 255));
  }

  {
    int enabled = g_locked ? 0 : 1;
    widget_set_sensitive(g_btnRest, enabled);
    widget_set_sensitive(g_btnParent, enabled);
  }
}

static void RefreshLock(void) {
  LONGLONG now    = (LONGLONG)GetTickCount64();
  LONGLONG remain = g_lockEndTick - now;
  LONGLONG total  = (g_lockEndTick - g_lockStartTick) / 1000;
  LONGLONG secs;
  WCHAR buf[256];

  if (remain < 0) remain = 0;
  secs = remain / 1000;

  StringCchPrintfW(buf, 256, L"本次已连续使用 %lld 分钟，强制休息 %lld 分钟",
                   g_usageMs / 60000, total / 60);
  SetTextW(g_lUsed, buf);

  SetTextW(g_lReason, g_reason);

  StringCchPrintfW(buf, 256, L"%02lld:%02lld", (secs / 60) % 100, secs % 60);
  SetTextW(g_lCount, buf);

  SetTextW(g_lHint, g_hint);
  widget_set_style_color(g_lHint, "text_color",
                         g_bad ? col(255, 120, 120) : col(200, 210, 225));
}

/* ---------------- 动作 ---------------- */
static void DoRest(void) {
  if (g_locked) return;
  LogEventW(L"用户手动触发休息");
  LockBegin((LONGLONG)g_cfg.break_minutes * 60000, L"手动休息");
}

static void DoReload(void) {
  ConfigLoad(&g_cfg, g_configPath);
  ConfigFinalizePassword(&g_cfg, g_configPath);
  HooksSetBlockHotkeys(g_cfg.block_hotkeys ? 1 : 0);
  LogEventW(L"已重新加载配置文件");
  if (g_page == PAGE_STATUS) RefreshStatus();
}

static void DoParent(void) {
  if (g_locked) return;
  /* 从托盘菜单进来时窗口可能还是隐藏的，先把窗口显示出来 */
  if (g_hidden) ShowFromTray();
  SwitchPage(PAGE_PARENT);
  SetTextW(g_pErr, L"");
  widget_set_text_utf8(g_pEdit, "");
  FocusEdit(g_pEdit);
}

static void ParentTry(void) {
  WCHAR pass[128];
  char u8[256];
  pass[0] = 0;
  u8[0] = 0;
  widget_get_text_utf8(g_pEdit, u8, (int)sizeof(u8));
  Utf8ToW(u8, pass, (int)(sizeof(pass) / sizeof(WCHAR)));
  pass[127] = 0;

  if (pass[0] == 0) {
    SetTextW(g_pErr, L"请输入密码。");
    FocusEdit(g_pEdit);
    return;
  }

  if (PwVerify(pass, g_cfg.pw_hash, g_cfg.pw_salt,
               g_cfg.pw_iterations, g_cfg.password)) {
    LogEventW(L"家长设置验证通过，进入设置界面");
    widget_set_text_utf8(g_pEdit, "");
    EnterSettings();
  } else {
    SetTextW(g_pErr, L"密码不正确，请重试。");
    widget_set_text_utf8(g_pEdit, "");
    FocusEdit(g_pEdit);
    LogEventW(L"家长设置验证未通过");
  }
}

static void SubmitPassword(void) {
  WCHAR pass[128];
  char u8[256];
  pass[0] = 0;
  u8[0] = 0;
  widget_get_text_utf8(g_lEdit, u8, (int)sizeof(u8));
  Utf8ToW(u8, pass, (int)(sizeof(pass) / sizeof(WCHAR)));
  pass[127] = 0;

  if (pass[0] == 0) {
    /* 空密码不算「密码错误」：不记错误次数、不加罚时，只提示 */
    StringCchCopyW(g_hint, 256, L"请输入密码");
    g_bad = 1;
    FocusEdit(g_lEdit);
    RefreshLock();
    return;
  }

  if (PwVerify(pass, g_cfg.pw_hash, g_cfg.pw_salt,
               g_cfg.pw_iterations, g_cfg.password)) {
    LONGLONG now = (LONGLONG)GetTickCount64();
    LONGLONG remain = g_lockEndTick - now;
    if (remain < 0) remain = 0;
    LogEventW(L"密码正确，提前解锁（剩余休息 %lld 分 %lld 秒）",
              remain / 60000, (remain / 1000) % 60);
    if (g_cfg.penalty)
      InterlockedExchange64(&g_pendingPenaltyMs, remain);
    LockEnd(0);
    return;
  }

  {
    LONG n = InterlockedIncrement(&g_wrongCount);
    if (g_cfg.wrong_penalty_seconds > 0)
      InterlockedAdd64(&g_lockEndTick, (LONGLONG)g_cfg.wrong_penalty_seconds * 1000);
    StringCchPrintfW(g_hint, 256, L"密码错误（第 %ld 次），休息时间增加 %d 秒",
                     n, g_cfg.wrong_penalty_seconds);
    LogEventW(L"解锁密码错误，累计错误 %ld 次", n);
    g_bad = 1;
    MessageBeep(MB_ICONWARNING);
  }
  widget_set_text_utf8(g_lEdit, "");
  FocusEdit(g_lEdit);
  RefreshLock();
}

static ret_t OnRestClick(void* ctx, event_t* e) {
  (void)ctx; (void)e;
  DoRest();
  return RET_OK;
}

static ret_t OnReloadClick(void* ctx, event_t* e) {
  (void)ctx; (void)e;
  DoReload();
  return RET_OK;
}

static ret_t OnParentClick(void* ctx, event_t* e) {
  (void)ctx; (void)e;
  DoParent();
  return RET_OK;
}

static ret_t OnParentOk(void* ctx, event_t* e) {
  (void)ctx; (void)e;
  ParentTry();
  return RET_OK;
}

static ret_t OnParentCancel(void* ctx, event_t* e) {
  (void)ctx; (void)e;
  widget_set_text_utf8(g_pEdit, "");
  SwitchPage(PAGE_STATUS);
  return RET_OK;
}

static ret_t OnUnlockClick(void* ctx, event_t* e) {
  (void)ctx; (void)e;
  SubmitPassword();
  return RET_OK;
}

/* 页面空白处被点击：把输入焦点还回该页面的密码框 */
static ret_t OnPagePointerDown(void* ctx, event_t* e) {
  widget_t* edit = (widget_t*)ctx;
  (void)e;
  if (edit != NULL && !edit->focused) FocusEdit(edit);
  return RET_OK;
}

static ret_t OnEditKey(void* ctx, event_t* e) {
  key_event_t* k = key_event_cast(e);
  (void)ctx;
  if (k == NULL) return RET_OK;

  if (k->key == TK_KEY_RETURN || k->key == TK_KEY_KP_ENTER) {
    if (g_page == PAGE_PARENT)    ParentTry();
    else if (g_page == PAGE_LOCK) SubmitPassword();
    return RET_STOP;
  }
  if (k->key == TK_KEY_ESCAPE) {
    if (g_page == PAGE_LOCK) return RET_STOP;      /* 锁屏时屏蔽 ESC */
    if (g_page == PAGE_PARENT) {
      widget_set_text_utf8(g_pEdit, "");
      SwitchPage(PAGE_STATUS);
      return RET_STOP;
    }
  }
  return RET_OK;
}

/* 窗口级兜底：锁屏时吞掉 ESC，避免任何退出路径 */
static ret_t OnWinKey(void* ctx, event_t* e) {
  key_event_t* k = key_event_cast(e);
  (void)ctx;
  if (k != NULL && g_locked && k->key == TK_KEY_ESCAPE) return RET_STOP;
  return RET_OK;
}

/* ---------------- 定时器：驱动会话计时与锁屏 ---------------- */
static ret_t OnTimer(const timer_info_t* info) {
  static int tick = 0;
  int W, H;
  (void)info;
  tick++;

  if (g_locked) {
    if ((LONGLONG)GetTickCount64() >= g_lockEndTick) {
      LogEventW(L"休息结束，自动解锁");
      LockEnd(1);
    } else {
      g_topCnt++;
      if ((g_topCnt & 5) == 1 && g_sdlWnd) {
        int vx = GetSystemMetrics(SM_XVIRTUALSCREEN);
        int vy = GetSystemMetrics(SM_YVIRTUALSCREEN);
        int vw = GetSystemMetrics(SM_CXVIRTUALSCREEN);
        int vh = GetSystemMetrics(SM_CYVIRTUALSCREEN);
        /* 锁屏期间被最小化（显示桌面等）就立刻还原，否则锁屏形同虚设 */
        if (IsIconic(g_sdlWnd)) ShowWindow(g_sdlWnd, SW_RESTORE);
        SetWindowPos(g_sdlWnd, HWND_TOPMOST, vx, vy, vw, vh, SWP_NOACTIVATE);
        {
          RECT clip = { vx, vy, vx + vw, vy + vh };
          ClipCursor(&clip);
        }
      }
      if ((g_topCnt & 3) == 1 && g_sdlWnd) ForceForeground();
      /* 点到非焦点控件后 AWTK 会把焦点清掉，锁屏期间必须一直把焦点按在密码框上，
         否则用户点一下屏幕就再也输不进密码。 */
      if (g_lEdit != NULL && !g_lEdit->focused) FocusEdit(g_lEdit);
      RefreshLock();
    }
  } else {
    if ((tick & 3) == 1) SessionTick();
    if (((tick + 2) & 3) == 1) RefreshStatus();
    WarnTick();                       /* 距离休息不足 warn_minutes 时弹倒计时小窗 */
    /* 分辨率/DPI 变了（或用户把窗口拖出屏幕）时把状态窗口重新贴回右下角 */
    if ((tick & 7) == 3 && g_sdlWnd && !g_hidden) {
      RECT rc, wa;
      if (GetWindowRect(g_sdlWnd, &rc) &&
          SystemParametersInfoW(SPI_GETWORKAREA, 0, &wa, 0)) {
        if (rc.left < wa.left || rc.top < wa.top ||
            rc.right > wa.right || rc.bottom > wa.bottom) {
          PlaceStatusWindow();
        }
      }
    }
  }

  W = g_win ? g_win->w : 0;
  H = g_win ? g_win->h : 0;
  if (W > 0 && H > 0 && (W != g_lastW || H != g_lastH)) LayoutPage(g_page);

  if (g_hidden && (tick % 5) == 1) TrayUpdateTip();

  return RET_REPEAT;
}

/* ---------------- 锁屏（由界面层实现） ---------------- */
int LockIsActive(void) {
  return g_locked != 0;
}

void LockBegin(LONGLONG breakMs, const WCHAR* reason) {
  if (g_locked) return;

  {
    LONGLONG now = (LONGLONG)GetTickCount64();
    InterlockedExchange64(&g_lockStartTick, now);
    InterlockedExchange64(&g_lockEndTick, now + breakMs);
  }
  InterlockedExchange(&g_wrongCount, 0);
  InterlockedExchange(&g_locked, 1);
  g_bad = 0;
  g_topCnt = 0;
  StringCchCopyW(g_reason, 256, reason ? reason : L"");
  StringCchCopyW(g_hint, 256, L"时间未到，如需提前解锁请输入密码");

  HooksSetLocked(1);
  LogEventW(L"触发锁屏：已使用 %lld 分钟，强制休息 %lld 分钟",
            g_usageMs / 60000, breakMs / 60000);

  g_wasHidden = g_hidden;
  if (g_hidden) TrayRemove();
  g_warnOnTop = 0;   /* 锁屏自己会置顶，退出全屏时恢复普通层级 */

  /* 如果用户正在设置界面（窗口被放大过），先按状态窗口尺寸收回来再锁屏，
     这样 ExitFullscreen 恢复的才是状态窗口大小，而不是那张大表单的尺寸 */
  if (g_page == PAGE_SETTINGS || g_page == PAGE_ADVANCED) {
    ResizeWindowTo(STATUS_W, STATUS_H);
    LogEventW(L"锁屏时自动退出家长设置界面（未保存的改动丢弃）");
  }

  SwitchPage(PAGE_LOCK);
  EnterFullscreen();
  widget_set_text_utf8(g_lEdit, "");
  FocusEdit(g_lEdit);
  RefreshLock();
}

void LockEnd(int autoUnlock) {
  LONGLONG len, pen;
  if (!g_locked) return;

  InterlockedExchange(&g_locked, 0);
  HooksSetLocked(0);
  ClipCursor(NULL);

  SwitchPage(PAGE_STATUS);
  ExitFullscreen();

  len = (LONGLONG)g_cfg.session_minutes * 60000;
  pen = g_pendingPenaltyMs;
  if (pen > 0) {
    LONGLONG min5 = 5 * 60000;
    len -= pen;                 /* 提前解锁：把没休息完的时间从下一次可用时长里扣掉 */
    if (len < min5) len = min5;
  }

  if (g_wasHidden) {
    if (g_sdlWnd) ShowWindow(g_sdlWnd, SW_HIDE);
    g_hidden = 1;
    TrayAdd();
  } else {
    g_hidden = 0;
    if (g_sdlWnd) {
      ShowWindow(g_sdlWnd, SW_SHOW);
      SetForegroundWindow(g_sdlWnd);
    }
  }

  SessionReset(len, autoUnlock ? L"休息结束自动解锁" : L"密码提前解锁");
  RefreshStatus();
}

/* ---------------- 对外接口 ---------------- */
int UiInit(void) {
  char root[MAX_PATH];

  pick_res_root(root, sizeof(root));
  LogUtf8(root);

  /* tk_init 里 SDL 会把窗口直接建成可见的；挂上 CBT 钩子抢在创建那一刻摘掉 WS_VISIBLE，
     等我们布局好（右下角）并决定显示时再显示，避免启动瞬间闪一下。 */
  HideOnCreateBegin();

  tk_pre_init();
  if (tk_init(STATUS_W, STATUS_H, APP_DESKTOP, "ScreenTime", root) != RET_OK) {
    HideOnCreateEnd();
    LogEventW(L"AWTK 初始化失败");
    return 0;
  }
  HideOnCreateEnd();

  if (assets_init() != RET_OK) {
    LogEventW(L"AWTK 资源初始化失败");
    return 0;
  }
  load_cjk_font();
  log_set_log_level(LOG_LEVEL_ERROR);
  window_manager_set_cursor(window_manager(), "cursor");

  /* 图标：优先 exe 同级的 ScreenTime.ico，其次系统盾牌图标。
     注意 LoadIcon 取系统图标时 hInstance 必须传 NULL，
     传自己的模块句柄会拿到 NULL，托盘上就只剩一个白方块。 */
  {
    WCHAR ico[MAX_PATH];
    StringCchCopyW(ico, MAX_PATH, g_baseDir);
    StringCchCatW(ico, MAX_PATH, L"\\ScreenTime.ico");
    if (GetFileAttributesW(ico) != INVALID_FILE_ATTRIBUTES) {
      g_appIcon = (HICON)LoadImageW(NULL, ico, IMAGE_ICON, 0, 0,
                                    LR_LOADFROMFILE | LR_DEFAULTSIZE);
    }
  }
  if (g_appIcon == NULL) g_appIcon = LoadIconW(NULL, IDI_SHIELD);
  if (g_appIcon == NULL) g_appIcon = LoadIconW(NULL, IDI_APPLICATION);
  LogEventW(L"托盘/窗口图标：%s", g_appIcon ? L"已加载" : L"加载失败");

  BridgeCreate();

  g_win = window_create(NULL, 0, 0, 0, 0);
  if (g_win == NULL) {
    LogEventW(L"AWTK 窗口创建失败");
    return 0;
  }
  window_manager_open_window(window_manager(), g_win);

  HookSdlWindow();
  /* 先藏起来再做后面的布局：CBT 钩子只保证"创建时在屏幕外"，
     而 PlaceStatusWindow 会把窗口挪到右下角，那一下仍然会被看到。 */
  if (g_sdlWnd) ShowWindow(g_sdlWnd, SW_HIDE);
  FixWindowFrame();

  BuildStatusPage();
  BuildParentPage();
  BuildLockPage();
  BuildSettingsPage();
  BuildAdvancedPage();
  BuildWarnPage();
  SwitchPage(PAGE_STATUS);
  PlaceStatusWindow();
  RefreshStatus();

  widget_on(WIDGET(g_win), EVT_KEY_DOWN, OnWinKey, NULL);

  /* 窗口已经建好并摆到右下角了，这里才决定要不要显示出来 */
  if (g_cfg.start_hidden) {
    g_hidden = 1;              /* 窗口从建好起就是隐藏的，这里只是记账 + 上托盘 */
    TrayAdd();
    LogEventW(L"启动隐藏：窗口全程未出现在屏幕上（无闪烁）");
  } else if (g_sdlWnd) {
    if (g_win) widget_invalidate_force(g_win, NULL);
    ShowWindow(g_sdlWnd, SW_SHOW);
    SetForegroundWindow(g_sdlWnd);
    LogEventW(L"启动显示：窗口已贴到右下角");
  }

  {
    native_window_info_t ni;
    if (native_window_get_info(TheNativeWindow(), &ni) == RET_OK) {
      LogEventW(L"AWTK 窗口 %d x %d（缩放 %.2f）", ni.w, ni.h, ni.ratio);
      if (ni.ratio > 0.1 && ni.ratio < 8.0) g_scale = ni.ratio;
    }
  }
  LogEventW(L"界面版本 %s（设置页 %d x %d 逻辑像素，缩放 %.2f）",
            ST_VERSION, ST_W, ST_H, g_scale);

  timer_add(OnTimer, NULL, 250);
  return 1;
}

void UiRun(void) {
  tk_run();
}

void UiShutdown(void) {
  TrayRemove();
  ClipCursor(NULL);
  tk_exit();
}
