#include "lock.h"
#include "appstate.h"
#include "hooks.h"
#include "logger.h"
#include "passwd.h"
#include "util.h"
#include <strsafe.h>
#include <stdio.h>
#include <wchar.h>

#define TM_TICK    ((UINT_PTR)1)
#define TM_TOPMOST ((UINT_PTR)2)

static HWND    g_edit = NULL;
static HWND    g_btn  = NULL;
static HFONT   g_fTitle = NULL, g_fBig = NULL, g_fText = NULL, g_fSmall = NULL;
static HBRUSH  g_bg = NULL, g_editBg = NULL, g_editBad = NULL;
static WCHAR   g_reason[256] = L"";
static WCHAR   g_hint[256]   = L"";
static WNDPROC g_oldEdit = NULL;
static int     g_bad = 0;
static int     g_topCnt = 0;
static int     g_classDone = 0;

static void MakeFonts(void)
{
    if (g_fTitle) return;
    g_fTitle = CreateFontW(-UiPx(72), 0, 0, 0, FW_BOLD, FALSE, FALSE, FALSE,
                           DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS,
                           CLEARTYPE_QUALITY, DEFAULT_PITCH | FF_DONTCARE, L"Microsoft YaHei UI");
    g_fBig   = CreateFontW(-UiPx(116), 0, 0, 0, FW_BOLD, FALSE, FALSE, FALSE,
                           DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS,
                           CLEARTYPE_QUALITY, DEFAULT_PITCH | FF_DONTCARE, L"Microsoft YaHei UI");
    g_fText  = CreateFontW(-UiPx(30), 0, 0, 0, FW_NORMAL, FALSE, FALSE, FALSE,
                           DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS,
                           CLEARTYPE_QUALITY, DEFAULT_PITCH | FF_DONTCARE, L"Microsoft YaHei UI");
    g_fSmall = CreateFontW(-UiPx(22), 0, 0, 0, FW_NORMAL, FALSE, FALSE, FALSE,
                           DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS,
                           CLEARTYPE_QUALITY, DEFAULT_PITCH | FF_DONTCARE, L"Microsoft YaHei UI");
    if (!g_fTitle) g_fTitle = (HFONT)GetStockObject(DEFAULT_GUI_FONT);
    if (!g_fBig)   g_fBig   = g_fTitle;
    if (!g_fText)  g_fText  = g_fTitle;
    if (!g_fSmall) g_fSmall = g_fTitle;
}

static void LayoutControls(HWND hwnd)
{
    RECT rc;
    GetClientRect(hwnd, &rc);
    int W = rc.right, H = rc.bottom;
    int ew = UiPx(340), eh = UiPx(44);
    int ex = (W - ew) / 2;
    int ey = (int)(H * 0.62);
    if (g_edit) MoveWindow(g_edit, ex, ey, ew, eh, TRUE);
    if (g_btn)  MoveWindow(g_btn, (W - UiPx(180)) / 2, ey + eh + UiPx(14), UiPx(180), UiPx(42), TRUE);
}

static void SubmitPassword(HWND hwnd)
{
    WCHAR pass[128];
    pass[0] = 0;
    GetWindowTextW(g_edit, pass, 126);

    if (pass[0] && PwVerify(pass, g_cfg.pw_hash, g_cfg.pw_salt,
                            g_cfg.pw_iterations, g_cfg.password)) {
        LONGLONG now = GetTickCount64();
        LONGLONG remain = g_lockEndTick - now;
        if (remain < 0) remain = 0;
        LogEventW(L"密码正确，提前解锁（剩余休息 %lld 分 %lld 秒）",
                  remain / 60000, (remain / 1000) % 60);
        if (g_cfg.penalty)
            InterlockedExchange64(&g_pendingPenaltyMs, remain);
        LockEnd(0);
        return;
    }

    LONG n = InterlockedIncrement(&g_wrongCount);
    if (g_cfg.wrong_penalty_seconds > 0)
        InterlockedAdd64(&g_lockEndTick, (LONGLONG)g_cfg.wrong_penalty_seconds * 1000);
    StringCchPrintfW(g_hint, 256, L"密码错误（第 %ld 次），休息时间增加 %d 秒",
                     n, g_cfg.wrong_penalty_seconds);
    LogEventW(L"解锁密码错误，累计错误 %ld 次", n);
    SetWindowTextW(g_edit, L"");
    g_bad = 1;
    MessageBeep(MB_ICONWARNING);
    InvalidateRect(hwnd, NULL, TRUE);
}

static LRESULT CALLBACK EditProc(HWND h, UINT m, WPARAM w, LPARAM l)
{
    if (m == WM_CHAR && (w == VK_RETURN || w == '\r' || w == '\n')) {
        SubmitPassword(GetParent(h));
        return 0;
    }
    if (m == WM_KEYDOWN && w == VK_ESCAPE) return 0;
    return CallWindowProcW(g_oldEdit, h, m, w, l);
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
    HBITMAP oldBmp = (HBITMAP)SelectObject(mem, bmp);

    FillRect(mem, &rc, g_bg);
    SetBkMode(mem, TRANSPARENT);

    LONGLONG now = GetTickCount64();
    LONGLONG remain = g_lockEndTick - now;
    if (remain < 0) remain = 0;
    LONGLONG total = (g_lockEndTick - g_lockStartTick) / 1000;
    LONGLONG secs = remain / 1000;
    WCHAR cd[64];
    StringCchPrintfW(cd, 64, L"%02lld:%02lld", (secs / 60) % 100, secs % 60);

    WCHAR usedBuf[128];
    LONGLONG used = g_usageMs / 60000;
    StringCchPrintfW(usedBuf, 128, L"本次已连续使用 %lld 分钟，强制休息 %lld 分钟",
                     used, total / 60);

    RECT r;

    SelectObject(mem, g_fTitle);
    SetTextColor(mem, RGB(255, 92, 92));
    r.left = 0; r.top = (int)(H * 0.14); r.right = W; r.bottom = (int)(H * 0.14) + 110;
    DrawTextW(mem, L"强制休息中", -1, &r, DT_CENTER | DT_TOP | DT_SINGLELINE);

    SelectObject(mem, g_fText);
    SetTextColor(mem, RGB(225, 230, 240));
    r.top = (int)(H * 0.28); r.bottom = r.top + 60;
    DrawTextW(mem, usedBuf, -1, &r, DT_CENTER | DT_TOP | DT_SINGLELINE);

    if (g_reason[0]) {
        SelectObject(mem, g_fSmall);
        SetTextColor(mem, RGB(170, 180, 195));
        r.top = (int)(H * 0.33); r.bottom = r.top + 40;
        DrawTextW(mem, g_reason, -1, &r, DT_CENTER | DT_TOP | DT_SINGLELINE);
    }

    SelectObject(mem, g_fBig);
    SetTextColor(mem, RGB(120, 200, 255));
    r.top = (int)(H * 0.40); r.bottom = r.top + 150;
    DrawTextW(mem, cd, -1, &r, DT_CENTER | DT_TOP | DT_SINGLELINE);

    SelectObject(mem, g_fText);
    SetTextColor(mem, g_bad ? RGB(255, 120, 120) : RGB(200, 210, 225));
    r.top = (int)(H * 0.55); r.bottom = r.top + 60;
    DrawTextW(mem, g_hint, -1, &r, DT_CENTER | DT_TOP | DT_SINGLELINE);

    SelectObject(mem, g_fSmall);
    SetTextColor(mem, RGB(150, 160, 175));
    r.top = (int)(H * 0.78); r.bottom = H - 20;
    DrawTextW(mem, L"提前解锁必须输入密码",
              -1, &r, DT_CENTER | DT_TOP | DT_WORDBREAK);

    BitBlt(hdc, 0, 0, W, H, mem, 0, 0, SRCCOPY);

    SelectObject(mem, oldBmp);
    DeleteObject(bmp);
    DeleteDC(mem);
    EndPaint(hwnd, &ps);
}

static LRESULT CALLBACK LockProc(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp)
{
    switch (msg) {
    case WM_CREATE:
        MakeFonts();
        g_bg     = CreateSolidBrush(RGB(10, 14, 22));
        g_editBg = CreateSolidBrush(RGB(30, 38, 54));
        g_editBad = CreateSolidBrush(RGB(70, 26, 30));

        g_edit = CreateWindowExW(0, L"EDIT", L"",
                                 WS_CHILD | WS_VISIBLE | WS_BORDER | ES_CENTER | ES_AUTOHSCROLL,
                                 0, 0, 10, 10, hwnd, (HMENU)101, g_hInst, NULL);
        SendMessageW(g_edit, WM_SETFONT, (WPARAM)g_fText, TRUE);
        SendMessageW(g_edit, EM_SETPASSWORDCHAR, (WPARAM)L'*', 0);
        g_oldEdit = (WNDPROC)SetWindowLongPtrW(g_edit, GWLP_WNDPROC, (LONG_PTR)EditProc);

        g_btn = CreateWindowExW(0, L"BUTTON", L"解锁",
                                WS_CHILD | WS_VISIBLE | BS_DEFPUSHBUTTON,
                                0, 0, 10, 10, hwnd, (HMENU)102, g_hInst, NULL);
        SendMessageW(g_btn, WM_SETFONT, (WPARAM)g_fText, TRUE);

        LayoutControls(hwnd);
        SetFocus(g_edit);
        SetTimer(hwnd, TM_TICK, 250, NULL);
        SetTimer(hwnd, TM_TOPMOST, 1500, NULL);
        return 0;

    case WM_CTLCOLORSTATIC:
        SetBkMode((HDC)wp, TRANSPARENT);
        SetTextColor((HDC)wp, RGB(230, 235, 245));
        return (LRESULT)g_bg;

    case WM_CTLCOLOREDIT:
        SetTextColor((HDC)wp, RGB(255, 255, 255));
        return (LRESULT)(g_bad ? g_editBad : g_editBg);

    case WM_TIMER:
        if (wp == TM_TICK) {
            if (GetTickCount64() >= (ULONGLONG)g_lockEndTick) {
                LogEventW(L"休息结束，自动解锁");
                LockEnd(1);
                return 0;
            }
            InvalidateRect(hwnd, NULL, TRUE);
        } else if (wp == TM_TOPMOST) {
            int vx = GetSystemMetrics(SM_XVIRTUALSCREEN);
            int vy = GetSystemMetrics(SM_YVIRTUALSCREEN);
            int vw = GetSystemMetrics(SM_CXVIRTUALSCREEN);
            int vh = GetSystemMetrics(SM_CYVIRTUALSCREEN);
            SetWindowPos(hwnd, HWND_TOPMOST, vx, vy, vw, vh, SWP_NOACTIVATE);
            RECT clip = { vx, vy, vx + vw, vy + vh };
            ClipCursor(&clip);
            if (++g_topCnt % 4 == 0) SetForegroundWindow(hwnd);
        }
        return 0;

    case WM_PAINT:
        OnPaint(hwnd);
        return 0;

    case WM_ERASEBKGND:
        return 1;

    case WM_SIZE:
        LayoutControls(hwnd);
        return 0;

    case WM_WINDOWPOSCHANGING: {
        WINDOWPOS *pos = (WINDOWPOS *)lp;
        pos->hwndInsertAfter = HWND_TOPMOST;
        pos->flags &= ~SWP_NOZORDER;
        return 0;
    }

    case WM_NCLBUTTONDOWN:      /* 禁止标题栏/边框操作（本窗口无标题栏，双保险） */
        return 0;

    case WM_SYSCOMMAND:
        switch (wp & 0xFFF0) {
        case SC_CLOSE:
        case SC_MINIMIZE:
        case SC_MAXIMIZE:
        case SC_RESTORE:
        case SC_MOVE:
            return 0;
        }
        break;

    case WM_CLOSE:
        return 0;             /* 不允许关闭 */

    case WM_COMMAND:
        if (LOWORD(wp) == 102) SubmitPassword(hwnd);
        return 0;

    case WM_DESTROY:
        KillTimer(hwnd, TM_TICK);
        KillTimer(hwnd, TM_TOPMOST);
        if (g_edit && g_oldEdit) SetWindowLongPtrW(g_edit, GWLP_WNDPROC, (LONG_PTR)g_oldEdit);
        g_edit = NULL; g_btn = NULL; g_oldEdit = NULL;
        if (g_bg)     { DeleteObject(g_bg);     g_bg = NULL; }
        if (g_editBg) { DeleteObject(g_editBg); g_editBg = NULL; }
        if (g_editBad){ DeleteObject(g_editBad);g_editBad = NULL; }
        ClipCursor(NULL);
        g_lockWnd = NULL;
        return 0;
    }
    return DefWindowProcW(hwnd, msg, wp, lp);
}

int LockIsActive(void)
{
    return g_locked != 0;
}

void LockBegin(LONGLONG breakMs, const WCHAR *reason)
{
    if (g_locked) return;

    LONGLONG now = GetTickCount64();
    InterlockedExchange64(&g_lockStartTick, now);
    InterlockedExchange64(&g_lockEndTick, now + breakMs);
    InterlockedExchange(&g_wrongCount, 0);
    InterlockedExchange(&g_locked, 1);
    g_bad = 0;
    g_topCnt = 0;
    StringCchCopyW(g_reason, 256, reason ? reason : L"");
    StringCchCopyW(g_hint, 256, L"时间未到，如需提前解锁请输入密码");

    HooksSetLocked(1);
    LogEventW(L"触发锁屏：已使用 %lld 分钟，强制休息 %lld 分钟",
              g_usageMs / 60000, breakMs / 60000);

    if (g_statusWnd) ShowWindow(g_statusWnd, SW_HIDE);

    int vx = GetSystemMetrics(SM_XVIRTUALSCREEN);
    int vy = GetSystemMetrics(SM_YVIRTUALSCREEN);
    int vw = GetSystemMetrics(SM_CXVIRTUALSCREEN);
    int vh = GetSystemMetrics(SM_CYVIRTUALSCREEN);
    if (vw <= 0 || vh <= 0) {
        vx = 0; vy = 0;
        vw = GetSystemMetrics(SM_CXSCREEN);
        vh = GetSystemMetrics(SM_CYSCREEN);
    }

    if (!g_classDone) {
        WNDCLASSEXW wc;
        memset(&wc, 0, sizeof(wc));
        wc.cbSize        = sizeof(wc);
        wc.style         = CS_HREDRAW | CS_VREDRAW;
        wc.lpfnWndProc   = LockProc;
        wc.hInstance     = g_hInst;
        wc.hCursor       = LoadCursor(NULL, IDC_ARROW);
        wc.lpszClassName = L"ScreenTimeLock";
        RegisterClassExW(&wc);
        g_classDone = 1;
    }

    g_lockWnd = CreateWindowExW(WS_EX_TOPMOST | WS_EX_TOOLWINDOW, L"ScreenTimeLock",
                                L"强制休息", WS_POPUP,
                                vx, vy, vw, vh, NULL, NULL, g_hInst, NULL);
    if (!g_lockWnd) {
        InterlockedExchange(&g_locked, 0);
        HooksSetLocked(0);
        LogEventW(L"锁屏窗口创建失败");
        return;
    }

    ShowWindow(g_lockWnd, SW_SHOW);
    SetWindowPos(g_lockWnd, HWND_TOPMOST, vx, vy, vw, vh, SWP_SHOWWINDOW | SWP_FRAMECHANGED);
    SetForegroundWindow(g_lockWnd);
    if (g_edit) SetFocus(g_edit);

    RECT clip = { vx, vy, vx + vw, vy + vh };
    ClipCursor(&clip);
}

void LockEnd(int autoUnlock)
{
    if (!g_locked) return;

    InterlockedExchange(&g_locked, 0);
    HooksSetLocked(0);
    ClipCursor(NULL);

    if (g_lockWnd) {
        HWND w = g_lockWnd;
        g_lockWnd = NULL;
        DestroyWindow(w);
    }

    LONGLONG len = (LONGLONG)g_cfg.session_minutes * 60000;
    LONGLONG pen = g_pendingPenaltyMs;
    if (pen > 0) {
        LONGLONG min5 = 5 * 60000;
        len -= pen;                       /* 提前解锁：把没休息完的时间从下一次可用时长里扣掉 */
        if (len < min5) len = min5;
    }

    if (g_statusWnd && IsWindowVisible(g_statusWnd)) {
        ShowWindow(g_statusWnd, SW_SHOW);
        SetForegroundWindow(g_statusWnd);
    }

    SessionReset(len, autoUnlock ? L"休息结束自动解锁" : L"密码提前解锁");
}
