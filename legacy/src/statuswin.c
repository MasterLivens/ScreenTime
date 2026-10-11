#include "statuswin.h"
#include "appstate.h"
#include "lock.h"
#include "logger.h"
#include "passwd.h"
#include "util.h"
#include <shellapi.h>
#include <strsafe.h>
#include <stdio.h>
#include <string.h>

#define BTN_REST    201
#define BTN_RELOAD  202
#define BTN_PARENT  205

#define BTN_TRAY_SHOW 210
#define TRAY_UID      1
#define WM_TRAY       (WM_APP + 1)
#define WM_SHOWREQ    (WM_APP + 2)

#define PARENT_CLASS  L"ScreenTimeParentDlg"
/* 家长验证对话框：这里写的是“客户区”设计尺寸（96 DPI 像素），
   窗口外框尺寸由 AdjustWindowRectEx 算出来，保证按钮不会被标题栏/边框挤掉。 */
#define PARENT_CW     440
#define PARENT_CH     200
/* 状态窗口客户区设计尺寸 */
#define STATUS_CW     470
#define STATUS_CH     240

static HFONT g_fHead = NULL, g_fBig = NULL, g_fText = NULL, g_fSmall = NULL;
static HBRUSH g_bg = NULL;
static int  g_classDone = 0;
static NOTIFYICONDATAW g_nid;
static int  g_trayAdded = 0;
static int  g_hidden = 0;
static int  g_ticks = 0;

/* 家长密码对话框 */
static int  g_parentDone = 0;      /* 对话框已关闭 */
static int  g_parentOk = 0;        /* 密码正确 */
static HWND g_parentDlg = NULL;
static HWND g_parentEdit = NULL;
static HWND g_parentErr = NULL;

static void MakeFonts(void)
{
    if (g_fHead) return;
    g_fHead  = CreateFontW(-UiPx(26), 0, 0, 0, FW_BOLD,   FALSE, FALSE, FALSE, DEFAULT_CHARSET,
                           OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS, CLEARTYPE_QUALITY,
                           DEFAULT_PITCH | FF_DONTCARE, L"Microsoft YaHei UI");
    g_fBig   = CreateFontW(-UiPx(46), 0, 0, 0, FW_BOLD,   FALSE, FALSE, FALSE, DEFAULT_CHARSET,
                           OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS, CLEARTYPE_QUALITY,
                           DEFAULT_PITCH | FF_DONTCARE, L"Microsoft YaHei UI");
    g_fText  = CreateFontW(-UiPx(22), 0, 0, 0, FW_NORMAL, FALSE, FALSE, FALSE, DEFAULT_CHARSET,
                           OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS, CLEARTYPE_QUALITY,
                           DEFAULT_PITCH | FF_DONTCARE, L"Microsoft YaHei UI");
    g_fSmall = CreateFontW(-UiPx(18), 0, 0, 0, FW_NORMAL, FALSE, FALSE, FALSE, DEFAULT_CHARSET,
                           OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS, CLEARTYPE_QUALITY,
                           DEFAULT_PITCH | FF_DONTCARE, L"Microsoft YaHei UI");
    if (!g_fHead)  g_fHead  = (HFONT)GetStockObject(DEFAULT_GUI_FONT);
    if (!g_fBig)   g_fBig   = g_fHead;
    if (!g_fText)  g_fText  = g_fHead;
    if (!g_fSmall) g_fSmall = g_fHead;
}

static void OnPaint(HWND hwnd)
{
    PAINTSTRUCT ps;
    HDC hdc = BeginPaint(hwnd, &ps);
    RECT rc;
    GetClientRect(hwnd, &rc);
    int W = rc.right, H = rc.bottom;
    if (W <= 0 || H <= 0) { EndPaint(hwnd, &ps); return; }

    HDC mem = CreateCompatibleDC(hdc);
    HBITMAP bmp = CreateCompatibleBitmap(hdc, W, H);
    HBITMAP old = (HBITMAP)SelectObject(mem, bmp);

    FillRect(mem, &rc, g_bg);
    SetBkMode(mem, TRANSPARENT);

    LONGLONG len    = g_sessionLenMs;
    LONGLONG used   = g_usageMs;
    LONGLONG remain = len - used;
    if (remain < 0) remain = 0;
    int warning = (g_cfg.warn_minutes > 0 && remain <= (LONGLONG)g_cfg.warn_minutes * 60000);

    WCHAR head[128];
    StringCchPrintfW(head, 128, L"屏幕使用时间管理　%s",
                     g_locked ? L"（锁屏中）" : (warning ? L"（即将锁屏）" : L"（正常）"));

    WCHAR big[64];
    LONGLONG rs = remain / 1000;
    StringCchPrintfW(big, 64, L"剩余 %02lld:%02lld", (rs / 60) % 100, rs % 60);

    WCHAR l1[160];
    LONGLONG us = used / 1000;
    StringCchPrintfW(l1, 160, L"本次会话 %lld 分钟　已用 %02lld:%02lld",
                     len / 60000, (us / 60) % 100, us % 60);

    SYSTEMTIME lockAt;
    {
        FILETIME ft;
        GetSystemTimeAsFileTime(&ft);
        ULONGLONG now100 = ((ULONGLONG)ft.dwHighDateTime << 32) | ft.dwLowDateTime;
        ULONGLONG target = now100 + (ULONGLONG)(remain / 1000) * 10000ULL;
        ft.dwLowDateTime = (DWORD)(target & 0xFFFFFFFF);
        ft.dwHighDateTime = (DWORD)(target >> 32);
        FileTimeToSystemTime(&ft, &lockAt);
        SYSTEMTIME lt;
        SystemTimeToTzSpecificLocalTime(NULL, &lockAt, &lt);
        lockAt = lt;
    }
    WCHAR l2[160];
    StringCchPrintfW(l2, 160, L"将在 %02d:%02d:%02d 锁屏休息 %d 分钟",
                     lockAt.wHour, lockAt.wMinute, lockAt.wSecond, g_cfg.break_minutes);

    RECT r;
    SelectObject(mem, g_fHead);
    SetTextColor(mem, warning ? RGB(255, 120, 120) : RGB(235, 240, 250));
    r.left = UiPx(18); r.top = UiPx(14); r.right = W - UiPx(12); r.bottom = UiPx(48);
    DrawTextW(mem, head, -1, &r, DT_LEFT | DT_TOP | DT_SINGLELINE);

    SelectObject(mem, g_fBig);
    SetTextColor(mem, warning ? RGB(255, 150, 90) : RGB(120, 200, 255));
    r.top = UiPx(54); r.bottom = UiPx(112);
    DrawTextW(mem, big, -1, &r, DT_LEFT | DT_TOP | DT_SINGLELINE);

    SelectObject(mem, g_fText);
    SetTextColor(mem, RGB(210, 220, 235));
    r.top = UiPx(118); r.bottom = UiPx(146);
    DrawTextW(mem, l1, -1, &r, DT_LEFT | DT_TOP | DT_SINGLELINE);

    r.top = UiPx(148); r.bottom = UiPx(176);
    DrawTextW(mem, l2, -1, &r, DT_LEFT | DT_TOP | DT_SINGLELINE);

    BitBlt(hdc, 0, 0, W, H, mem, 0, 0, SRCCOPY);
    SelectObject(mem, old);
    DeleteObject(bmp);
    DeleteDC(mem);
    EndPaint(hwnd, &ps);
}

/* ---------------- 系统托盘（隐藏到右下角，计时继续运行） ---------------- */

static void TrayUpdateTip(void);

static void TrayAdd(HWND hwnd)
{
    if (g_trayAdded) return;
    if (!g_cfg.show_tray_icon) return;
    memset(&g_nid, 0, sizeof(g_nid));
    g_nid.cbSize           = sizeof(g_nid);
    g_nid.hWnd             = hwnd;
    g_nid.uID              = TRAY_UID;
    g_nid.uFlags           = NIF_ICON | NIF_MESSAGE | NIF_TIP;
    g_nid.uCallbackMessage = WM_TRAY;
    g_nid.hIcon            = LoadIconW(NULL, IDI_SHIELD);
    if (!g_nid.hIcon) g_nid.hIcon = LoadIconW(NULL, IDI_APPLICATION);
    StringCchCopyW(g_nid.szTip, ARRAYSIZE(g_nid.szTip), L"屏幕使用时间管理");
    if (Shell_NotifyIconW(NIM_ADD, &g_nid)) {
        g_trayAdded = 1;
        TrayUpdateTip();
    } else {
        LogEventW(L"托盘图标添加失败");
    }
}

static void TrayRemove(void)
{
    if (!g_trayAdded) return;
    Shell_NotifyIconW(NIM_DELETE, &g_nid);
    g_trayAdded = 0;
}

static void TrayUpdateTip(void)
{
    if (!g_trayAdded) return;
    LONGLONG remain = g_sessionLenMs - g_usageMs;
    if (remain < 0) remain = 0;
    LONGLONG rs = remain / 1000;
    if (g_locked)
        StringCchPrintfW(g_nid.szTip, ARRAYSIZE(g_nid.szTip), L"屏幕使用时间管理（锁屏休息中）");
    else
        StringCchPrintfW(g_nid.szTip, ARRAYSIZE(g_nid.szTip), L"屏幕使用时间管理　剩余 %02lld:%02lld",
                         (rs / 60) % 100, rs % 60);
    g_nid.uFlags = NIF_TIP;
    Shell_NotifyIconW(NIM_MODIFY, &g_nid);
}

static void TrayBalloon(HWND hwnd, const WCHAR *text)
{
    if (!g_trayAdded) return;
    g_nid.uFlags = NIF_INFO;
    StringCchCopyW(g_nid.szInfoTitle, ARRAYSIZE(g_nid.szInfoTitle), L"屏幕使用时间管理");
    StringCchCopyW(g_nid.szInfo, ARRAYSIZE(g_nid.szInfo), text);
    g_nid.dwInfoFlags = NIIF_INFO;
    Shell_NotifyIconW(NIM_MODIFY, &g_nid);
    g_nid.uFlags = NIF_ICON | NIF_MESSAGE | NIF_TIP;
    (void)hwnd;
}

static void HideWindow(HWND hwnd, int balloon)
{
    TrayAdd(hwnd);
    ShowWindow(hwnd, SW_HIDE);
    g_hidden = 1;
    TrayUpdateTip();
    if (balloon)
        TrayBalloon(hwnd, L"窗口已隐藏，计时与锁屏继续运行。\n双击右下角图标可重新显示窗口。");
}

static void ShowWindowFromHidden(HWND hwnd)
{
    ShowWindow(hwnd, SW_SHOW);
    ShowWindow(hwnd, SW_RESTORE);
    SetForegroundWindow(hwnd);
    g_hidden = 0;
    TrayRemove();
    InvalidateRect(hwnd, NULL, TRUE);
}

static void ShowTrayMenu(HWND hwnd)
{
    HMENU m = CreatePopupMenu();
    if (!m) return;
    AppendMenuW(m, MF_STRING, BTN_TRAY_SHOW, L"显示窗口");
    AppendMenuW(m, MF_SEPARATOR, 0, NULL);
    AppendMenuW(m, MF_STRING, BTN_REST,    L"立即休息");
    AppendMenuW(m, MF_STRING, BTN_RELOAD,  L"重载配置");
    AppendMenuW(m, MF_STRING, BTN_PARENT,  L"家长设置（需密码）");
    POINT pt;
    GetCursorPos(&pt);
    SetForegroundWindow(hwnd);
    TrackPopupMenuEx(m, TPM_BOTTOMALIGN | TPM_LEFTALIGN, pt.x, pt.y, hwnd, NULL);
    DestroyMenu(m);
}

/* ---------------- 家长设置（密码验证后才打开配置与日志） ---------------- */

static void ParentTry(HWND dlg)
{
    WCHAR pass[128];
    GetWindowTextW(g_parentEdit, pass, (int)(sizeof(pass) / sizeof(pass[0])) - 1);
#ifdef ST_PWDEBUG
    {
        WCHAR pass2[128];
        memset(pass2, 0, sizeof(pass2));
        SendMessageW(g_parentEdit, WM_GETTEXT, (WPARAM)127, (LPARAM)pass2);
        WCHAR pass3[128];
        memset(pass3, 0, sizeof(pass3));
        GetWindowTextW(g_parentEdit, pass3, 127);
        LogEventW(L"[pwdbg] ParentTry edit=%p dlgEdit=%p passlen=%d textlen=%d wmgettext=[%ls] again=[%ls] hashlen=%d saltlen=%d iter=%d plainlen=%d",
                  (void *)g_parentEdit, (void *)GetDlgItem(dlg, 305), (int)wcslen(pass),
                  (int)GetWindowTextLengthW(g_parentEdit), pass2, pass3,
                  (int)wcslen(g_cfg.pw_hash), (int)wcslen(g_cfg.pw_salt),
                  g_cfg.pw_iterations, (int)wcslen(g_cfg.password));
    }
#endif
    if (pass[0] && PwVerify(pass, g_cfg.pw_hash, g_cfg.pw_salt,
                            g_cfg.pw_iterations, g_cfg.password)) {
        g_parentOk = 1;
        DestroyWindow(dlg);
    } else {
        SetWindowTextW(g_parentErr, L"密码不正确，请重试。");
        InvalidateRect(dlg, NULL, TRUE);
        SetFocus(g_parentEdit);
        SendMessageW(g_parentEdit, EM_SETSEL, -1, -1);
    }
    SecureZeroMemory(pass, sizeof(pass));
}

static void ParentOpen(void)
{
    LogEventW(L"家长设置已通过密码验证，打开配置文件与日志目录");
    ShellExecuteW(NULL, L"open", L"notepad.exe", g_configPath, NULL, SW_SHOWNORMAL);
    ShellExecuteW(NULL, L"open", g_logDir, NULL, g_baseDir, SW_SHOWNORMAL);
}

static LRESULT CALLBACK ParentProc(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp)
{
    switch (msg) {
    case WM_CREATE:
        {
            /* 全部按“真实客户区”布局，按钮贴底并留出边距，任何 DPI 下都不会被裁切 */
            RECT cr;
            GetClientRect(hwnd, &cr);
            const int CW = cr.right, CH = cr.bottom;
            const int m    = UiPx(18);          /* 左右边距 */
            const int bw   = UiPx(100), bh = UiPx(34);
            const int gap  = UiPx(12);
            const int by   = CH - UiPx(16) - bh;      /* 按钮顶边 */
            const int ccx  = CW - m - bw;             /* 取消 */
            const int okx  = ccx - gap - bw;          /* 确定 */
            const int ey   = by - UiPx(8) - UiPx(24); /* 错误提示 */
            const int rowy = UiPx(84), rowh = UiPx(30);

            g_parentErr = CreateWindowExW(0, L"STATIC", L"",
                                          WS_CHILD | WS_VISIBLE | SS_LEFT,
                                          m, ey, CW - 2 * m, UiPx(24), hwnd,
                                          (HMENU)301, g_hInst, NULL);
            HWND t1 = CreateWindowExW(0, L"STATIC", L"家长设置",
                                      WS_CHILD | WS_VISIBLE | SS_LEFT,
                                      m, UiPx(12), CW - 2 * m, UiPx(34), hwnd,
                                      (HMENU)302, g_hInst, NULL);
            HWND t2 = CreateWindowExW(0, L"STATIC",
                                      L"打开配置文件与日志需要家长密码。",
                                      WS_CHILD | WS_VISIBLE | SS_LEFT,
                                      m, UiPx(50), CW - 2 * m, UiPx(26), hwnd,
                                      (HMENU)303, g_hInst, NULL);
            HWND lbl = CreateWindowExW(0, L"STATIC", L"家长密码",
                                       WS_CHILD | WS_VISIBLE | SS_LEFT,
                                       m, rowy + (rowh - UiPx(24)) / 2, UiPx(100), UiPx(24), hwnd,
                                       (HMENU)304, g_hInst, NULL);
            g_parentEdit = CreateWindowExW(WS_EX_CLIENTEDGE, L"EDIT", L"",
                                           WS_CHILD | WS_VISIBLE | WS_TABSTOP |
                                           ES_PASSWORD | ES_AUTOHSCROLL,
                                           m + UiPx(106), rowy, CW - 2 * m - UiPx(106), rowh, hwnd,
                                           (HMENU)305, g_hInst, NULL);
            HWND ok = CreateWindowExW(0, L"BUTTON", L"确定",
                                      WS_CHILD | WS_VISIBLE | BS_DEFPUSHBUTTON,
                                      okx, by, bw, bh, hwnd,
                                      (HMENU)IDOK, g_hInst, NULL);
            HWND cc = CreateWindowExW(0, L"BUTTON", L"取消",
                                      WS_CHILD | WS_VISIBLE | BS_PUSHBUTTON,
                                      ccx, by, bw, bh, hwnd,
                                      (HMENU)IDCANCEL, g_hInst, NULL);
            HFONT f = CreateFontW(-UiPx(22), 0, 0, 0, FW_NORMAL, FALSE, FALSE, FALSE, DEFAULT_CHARSET,
                                  OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS, CLEARTYPE_QUALITY,
                                  DEFAULT_PITCH | FF_DONTCARE, L"Microsoft YaHei UI");
            if (!f) f = (HFONT)GetStockObject(DEFAULT_GUI_FONT);
            HWND all[] = { t1, t2, lbl, g_parentEdit, g_parentErr, ok, cc };
            for (size_t i = 0; i < sizeof(all) / sizeof(all[0]); i++)
                SendMessageW(all[i], WM_SETFONT, (WPARAM)f, TRUE);
            MakeFonts();
            if (g_fHead) SendMessageW(t1, WM_SETFONT, (WPARAM)g_fHead, TRUE);
        }
        {   /* 去掉标题栏的关闭按钮，避免误操作 */
            HMENU sm = GetSystemMenu(hwnd, FALSE);
            if (sm) DeleteMenu(sm, SC_CLOSE, MF_BYCOMMAND);
        }
        return 0;

    case WM_CTLCOLORSTATIC:
        SetTextColor((HDC)wp, RGB(255, 150, 150));
        return (LRESULT)GetSysColorBrush(COLOR_BTNFACE);

    case WM_COMMAND:
        if (LOWORD(wp) == IDOK) { ParentTry(hwnd); return 0; }
        if (LOWORD(wp) == IDCANCEL) { DestroyWindow(hwnd); return 0; }
        break;

    case WM_CLOSE:
        DestroyWindow(hwnd);
        return 0;

    case WM_DESTROY:
        g_parentDlg = NULL;
        g_parentEdit = NULL;
        g_parentDone = 1;
        return 0;
    }
    return DefWindowProcW(hwnd, msg, wp, lp);
}

static int ParentGate(HWND owner)
{
    static int clsDone = 0;
    if (!clsDone) {
        WNDCLASSEXW wc;
        memset(&wc, 0, sizeof(wc));
        wc.cbSize        = sizeof(wc);
        wc.style         = CS_HREDRAW | CS_VREDRAW;
        wc.lpfnWndProc   = ParentProc;
        wc.hInstance     = g_hInst;
        wc.hCursor       = LoadCursor(NULL, IDC_ARROW);
        wc.hbrBackground = GetSysColorBrush(COLOR_BTNFACE);
        wc.lpszClassName = PARENT_CLASS;
        RegisterClassExW(&wc);
        clsDone = 1;
    }
    if (g_parentDlg) return 0;

    const DWORD style = WS_POPUP | WS_CAPTION | WS_SYSMENU;
    const DWORD exstyle = WS_EX_DLGMODALFRAME | WS_EX_TOPMOST;
    int pw = 0, ph = 0;
    OuterSizeForClient(style, exstyle, PARENT_CW, PARENT_CH, &pw, &ph);

    int sw = GetSystemMetrics(SM_CXSCREEN);
    int sh = GetSystemMetrics(SM_CYSCREEN);
    int x = (sw - pw) / 2, y = (sh - ph) / 2;
    if (owner) {
        RECT rc;
        if (GetWindowRect(owner, &rc)) {
            x = (rc.left + rc.right - pw) / 2;
            y = (rc.top + rc.bottom - ph) / 2;
        }
    }
    if (x + pw > sw) x = sw - pw;      /* 保证整个对话框都在屏幕内 */
    if (y + ph > sh) y = sh - ph;
    if (x < 0) x = 0;
    if (y < 0) y = 0;

    g_parentDone = 0;
    g_parentOk = 0;
    g_parentDlg = CreateWindowExW(exstyle, PARENT_CLASS,
                                  L"家长验证", style,
                                  x, y, pw, ph,
                                  owner, NULL, g_hInst, NULL);
    if (!g_parentDlg) return 0;

    ShowWindow(g_parentDlg, SW_SHOW);
    UpdateWindow(g_parentDlg);
    SetForegroundWindow(g_parentDlg);
    if (g_parentEdit) SetFocus(g_parentEdit);

    MSG m;
    while (!g_parentDone) {
        int r = GetMessageW(&m, NULL, 0, 0);
        if (r == 0 || r == -1) { PostQuitMessage(0); break; }
        TranslateMessage(&m);
        DispatchMessageW(&m);
        if (g_locked && g_parentDlg) {   /* 锁屏触发时立即关闭验证窗口 */
            g_parentOk = 0;
            DestroyWindow(g_parentDlg);
            break;
        }
    }
    return g_parentOk;
}

static void ParentSettings(HWND hwnd)
{
    if (ParentGate(hwnd)) ParentOpen();
    else LogEventW(L"家长设置验证未通过");
}

static LRESULT CALLBACK StatusProc(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp)
{
    switch (msg) {
    case WM_CREATE:
        MakeFonts();
        g_bg = CreateSolidBrush(RGB(18, 24, 34));
        {
            RECT cr;
            GetClientRect(hwnd, &cr);
            const int bh = UiPx(34);
            const int by = cr.bottom - UiPx(14) - bh;   /* 按钮贴底并留边距，不会被裁切 */
            struct { int id; const WCHAR *text; int x; } btns[] = {
                { BTN_REST,   L"立即休息", 14 },
                { BTN_RELOAD, L"重载配置", 128 },
                { BTN_PARENT, L"家长设置", 242 },
            };
            for (size_t i = 0; i < sizeof(btns) / sizeof(btns[0]); i++) {
                HWND b = CreateWindowExW(0, L"BUTTON", btns[i].text,
                                         WS_CHILD | WS_VISIBLE | BS_PUSHBUTTON,
                                         UiPx(btns[i].x), by, UiPx(104), bh, hwnd,
                                         (HMENU)(INT_PTR)btns[i].id, g_hInst, NULL);
                SendMessageW(b, WM_SETFONT, (WPARAM)g_fText, TRUE);
            }
        }
        SetTimer(hwnd, 1, 1000, NULL);
        return 0;

    case WM_TIMER:
        SessionTick();
        if (!g_locked) InvalidateRect(hwnd, NULL, TRUE);
        if (g_hidden && (++g_ticks % 5) == 0) TrayUpdateTip();
        return 0;

    case WM_PAINT:
        OnPaint(hwnd);
        return 0;

    case WM_ERASEBKGND:
        return 1;

    case WM_TRAY:
        switch (LOWORD(lp)) {
        case WM_LBUTTONUP:
        case WM_LBUTTONDBLCLK:
            ShowWindowFromHidden(hwnd);
            break;
        case WM_RBUTTONUP:
        case WM_CONTEXTMENU:
            ShowTrayMenu(hwnd);
            break;
        }
        return 0;

    case WM_SHOWREQ:
        ShowWindowFromHidden(hwnd);
        return 0;

    case WM_COMMAND:
        switch (LOWORD(wp)) {
        case BTN_TRAY_SHOW:
            ShowWindowFromHidden(hwnd);
            break;
        case BTN_REST:
            LogEventW(L"用户手动触发休息");
            LockBegin((LONGLONG)g_cfg.break_minutes * 60000, L"手动休息");
            break;
        case BTN_RELOAD:
            ConfigLoad(&g_cfg, g_configPath);
            ConfigFinalizePassword(&g_cfg, g_configPath);
            LogEventW(L"已重新加载配置文件");
            InvalidateRect(hwnd, NULL, TRUE);
            break;
        case BTN_PARENT:
            ParentSettings(hwnd);
            break;
        }
        return 0;

    /* 点 X：隐藏窗口（右下角托盘），计时与锁屏继续运行 */
    case WM_CLOSE:
        HideWindow(hwnd, 1);
        return 0;

    case WM_SYSCOMMAND:
        if ((wp & 0xFFF0) == SC_CLOSE) {
            HideWindow(hwnd, 1);
            return 0;
        }
        break;

    case WM_DESTROY:
        KillTimer(hwnd, 1);
        TrayRemove();
        if (g_bg) { DeleteObject(g_bg); g_bg = NULL; }
        PostQuitMessage(0);
        return 0;
    }
    return DefWindowProcW(hwnd, msg, wp, lp);
}

HWND StatusWinCreate(void)
{
    if (!g_classDone) {
        WNDCLASSEXW wc;
        memset(&wc, 0, sizeof(wc));
        wc.cbSize        = sizeof(wc);
        wc.style         = CS_HREDRAW | CS_VREDRAW;
        wc.lpfnWndProc   = StatusProc;
        wc.hInstance     = g_hInst;
        wc.hCursor       = LoadCursor(NULL, IDC_ARROW);
        wc.hbrBackground = NULL;
        wc.lpszClassName = L"ScreenTimeStatus";
        RegisterClassExW(&wc);
        g_classDone = 1;
    }

    const DWORD style = WS_OVERLAPPEDWINDOW & ~(WS_MAXIMIZEBOX | WS_THICKFRAME);
    int w = 0, h = 0;
    OuterSizeForClient(style, 0, STATUS_CW, STATUS_CH, &w, &h);

    int sw = GetSystemMetrics(SM_CXSCREEN);
    int sh = GetSystemMetrics(SM_CYSCREEN);
    RECT wa = { 0, 0, sw, sh };
    SystemParametersInfoW(SPI_GETWORKAREA, 0, &wa, 0);   /* 避开任务栏 */
    int x = wa.right - w - UiPx(24);
    int y = wa.bottom - h - UiPx(12);
    if (x < wa.left) x = wa.left;
    if (y < wa.top) y = wa.top;

    g_statusWnd = CreateWindowExW(0, L"ScreenTimeStatus", L"屏幕使用时间管理",
                                  style,
                                  x, y, w, h,
                                  NULL, NULL, g_hInst, NULL);
    if (!g_statusWnd) return NULL;

    if (g_cfg.start_hidden) {
        /* 启动时不显示任何界面：窗口保持隐藏，计时与锁屏照常运行。
           show_tray_icon=1 时右下角有托盘图标可恢复窗口；
           show_tray_icon=0 时完全不可见，重新运行本程序可显示窗口。 */
        g_hidden = 1;
        TrayAdd(g_statusWnd);
    } else {
        ShowWindow(g_statusWnd, SW_SHOW);
        UpdateWindow(g_statusWnd);
    }
    return g_statusWnd;
}
