/* 自动化输入辅助（仅用于本机功能验证）。
   用法：
     input.exe focus <pid>            把目标进程的 SDL 顶层窗口提到前台
     input.exe click <x> <y>          物理像素坐标点击
     input.exe move  <x> <y>          移动鼠标
     input.exe type  <utf8 文本>      用 KEYEVENTF_UNICODE 逐字输入到焦点控件
     input.exe key   <VK 十进制>      按一次键（如 13=回车，27=ESC，8=退格）
     input.exe mouseoff               把鼠标移到右下角（避免遮挡）
*/
#include <windows.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static void SendUnicodeChar(WCHAR c)
{
    INPUT in[2];
    memset(in, 0, sizeof(in));
    in[0].type = INPUT_KEYBOARD;
    in[0].ki.wScan = c;
    in[0].ki.dwFlags = KEYEVENTF_UNICODE;
    in[1] = in[0];
    in[1].ki.dwFlags = KEYEVENTF_UNICODE | KEYEVENTF_KEYUP;
    SendInput(2, in, sizeof(INPUT));
    Sleep(30);
}

static void TapKey(WORD vk)
{
    INPUT in[2];
    UINT n;
    memset(in, 0, sizeof(in));
    in[0].type = INPUT_KEYBOARD;
    in[0].ki.wVk = vk;
    in[0].ki.wScan = (WORD)MapVirtualKeyW(vk, MAPVK_VK_TO_VSC);
    in[1] = in[0];
    in[1].ki.dwFlags = KEYEVENTF_KEYUP;
    n = SendInput(2, in, sizeof(INPUT));
    printf("SendInput vk=%u -> %u (err=%lu)\n", (unsigned)vk, (unsigned)n, GetLastError());
    Sleep(60);
}

static void ClickAt(int x, int y)
{
    SetCursorPos(x, y);
    Sleep(80);
    INPUT in[2];
    memset(in, 0, sizeof(in));
    in[0].type = INPUT_MOUSE;
    in[0].mi.dwFlags = MOUSEEVENTF_LEFTDOWN;
    in[1].type = INPUT_MOUSE;
    in[1].mi.dwFlags = MOUSEEVENTF_LEFTUP;
    SendInput(2, in, sizeof(INPUT));
    Sleep(120);
}

/* 找出 pid 拥有的、类名含 SDL 的可见顶层窗口 */
static DWORD g_pid;
static HWND  g_found;

static BOOL CALLBACK Cb(HWND h, LPARAM lp)
{
    (void)lp;
    DWORD pid = 0;
    GetWindowThreadProcessId(h, &pid);
    if (pid != g_pid) return TRUE;
    WCHAR cls[128] = L"";
    GetClassNameW(h, cls, 127);
    if (wcsstr(cls, L"SDL")) { g_found = h; return FALSE; }
    return TRUE;
}

int main(int argc, char **argv)
{
    SetConsoleOutputCP(CP_UTF8);
    SetProcessDPIAware();
    if (argc < 2) { printf("usage: input.exe focus|click|move|type|key ...\n"); return 2; }

    if (!strcmp(argv[1], "focus") && argc >= 3) {
        g_pid = (DWORD)strtoul(argv[2], NULL, 10);
        g_found = NULL;
        EnumWindows(Cb, 0);
        if (!g_found) { printf("no SDL window for pid=%lu\n", g_pid); return 1; }
        HWND fg = GetForegroundWindow();
        DWORD fgTid = fg ? GetWindowThreadProcessId(fg, NULL) : 0;
        DWORD myTid = GetCurrentThreadId();
        if (fgTid && fgTid != myTid) AttachThreadInput(myTid, fgTid, TRUE);
        ShowWindow(g_found, SW_SHOW);
        SetForegroundWindow(g_found);
        BringWindowToTop(g_found);
        SetActiveWindow(g_found);
        SetFocus(g_found);
        if (fgTid && fgTid != myTid) AttachThreadInput(myTid, fgTid, FALSE);
        Sleep(300);
        {
            HWND fgc = GetForegroundWindow();
            DWORD tid = fgc ? GetWindowThreadProcessId(fgc, NULL) : 0;
            HWND foc = NULL;
            if (tid) {
                AttachThreadInput(myTid, tid, TRUE);
                foc = GetFocus();
                AttachThreadInput(myTid, tid, FALSE);
            }
            printf("focused hwnd=0x%llX fg=0x%llX focus=0x%llX\n",
                   (unsigned long long)(UINT_PTR)g_found,
                   (unsigned long long)(UINT_PTR)fgc,
                   (unsigned long long)(UINT_PTR)foc);
        }
        return 0;
    }

    if (!strcmp(argv[1], "click") && argc >= 4) {
        ClickAt(atoi(argv[2]), atoi(argv[3]));
        printf("clicked %s,%s\n", argv[2], argv[3]);
        return 0;
    }

    if (!strcmp(argv[1], "move") && argc >= 4) {
        SetCursorPos(atoi(argv[2]), atoi(argv[3]));
        printf("moved\n");
        return 0;
    }

    if (!strcmp(argv[1], "type") && argc >= 3) {
        WCHAR w[512];
        MultiByteToWideChar(CP_UTF8, 0, argv[2], -1, w, 511);
        for (int i = 0; w[i]; i++) SendUnicodeChar(w[i]);
        printf("typed %s\n", argv[2]);
        return 0;
    }

    if (!strcmp(argv[1], "key") && argc >= 3) {
        int vk = (int)strtol(argv[2], NULL, 0);
        TapKey((WORD)vk);
        printf("key %d\n", vk);
        return 0;
    }

    if (!strcmp(argv[1], "vkey") && argc >= 4) {
        int vk = (int)strtol(argv[3], NULL, 0);
        HWND h;
        LPARAM lp;
        g_pid = (DWORD)strtoul(argv[2], NULL, 10);
        g_found = NULL;
        EnumWindows(Cb, 0);
        h = g_found;
        if (!h) { printf("no SDL window for pid=%lu\n", g_pid); return 1; }
        lp = (LPARAM)(MapVirtualKeyW((UINT)vk, MAPVK_VK_TO_VSC) << 16);
        PostMessageW(h, WM_KEYDOWN, (WPARAM)vk, lp);
        Sleep(60);
        PostMessageW(h, WM_KEYUP, (WPARAM)vk, lp | (LPARAM)0xC0000000);
        printf("posted vk=%d to 0x%llX\n", vk, (unsigned long long)(UINT_PTR)h);
        return 0;
    }

    if (!strcmp(argv[1], "minimize") && argc >= 3) {
        DWORD pid = (DWORD)strtoul(argv[2], NULL, 10);
        HWND h;
        g_pid = pid;
        g_found = NULL;
        EnumWindows(Cb, 0);
        h = g_found;
        if (!h) { printf("no SDL window\n"); return 1; }
        PostMessageW(h, WM_SYSCOMMAND, SC_MINIMIZE, 0);
        Sleep(400);
        printf("iconic=%d\n", (int)IsIconic(h));
        return 0;
    }

    if (!strcmp(argv[1], "restore") && argc >= 3) {
        DWORD pid = (DWORD)strtoul(argv[2], NULL, 10);
        HWND h;
        g_pid = pid;
        g_found = NULL;
        EnumWindows(Cb, 0);
        h = g_found;
        if (!h) { printf("no SDL window\n"); return 1; }
        printf("before: iconic=%d\n", (int)IsIconic(h));
        ShowWindow(h, SW_RESTORE);
        Sleep(400);
        printf("after : iconic=%d\n", (int)IsIconic(h));
        return 0;
    }

    if (!strcmp(argv[1], "place") && argc >= 3) {
        DWORD pid = (DWORD)strtoul(argv[2], NULL, 10);
        HWND h;
        WINDOWPLACEMENT wp;
        RECT r;
        g_pid = pid;
        g_found = NULL;
        EnumWindows(Cb, 0);
        h = g_found;
        if (!h) { printf("no SDL window\n"); return 1; }
        memset(&wp, 0, sizeof(wp));
        wp.length = sizeof(wp);
        GetWindowPlacement(h, &wp);
        GetWindowRect(h, &r);
        printf("showCmd=%u iconic=%d rect=%ld,%ld,%ld,%ld normal=%ld,%ld,%ld,%ld\n",
               (unsigned)wp.showCmd, (int)IsIconic(h),
               r.left, r.top, r.right, r.bottom,
               wp.rcNormalPosition.left, wp.rcNormalPosition.top,
               wp.rcNormalPosition.right, wp.rcNormalPosition.bottom);
        return 0;
    }

    if (!strcmp(argv[1], "close") || !strcmp(argv[1], "traydbl") || !strcmp(argv[1], "trayr")) {
        DWORD pid = (argc >= 3) ? (DWORD)strtoul(argv[2], NULL, 10) : 0;
        HWND h;
        g_pid = pid;
        g_found = NULL;
        EnumWindows(Cb, 0);
        h = g_found;
        if (!strcmp(argv[1], "close")) {
            if (!h) { printf("no SDL window\n"); return 1; }
            PostMessageW(h, WM_CLOSE, 0, 0);
            printf("posted WM_CLOSE to 0x%llX\n", (unsigned long long)(UINT_PTR)h);
        } else {
            HWND bridge = FindWindowW(L"ScreenTimeStatus", NULL);
            if (!bridge) { printf("no bridge window\n"); return 1; }
            PostMessageW(bridge, WM_APP + 1, 1,
                         (LPARAM)((!strcmp(argv[1], "traydbl")) ? WM_LBUTTONDBLCLK : WM_RBUTTONUP));
            printf("posted tray msg to 0x%llX\n", (unsigned long long)(UINT_PTR)bridge);
        }
        return 0;
    }

    if (!strcmp(argv[1], "atpoint") && argc >= 4) {
        POINT p;
        HWND h, root;
        DWORD pid = 0;
        char cls[256] = "", title[256] = "";
        WCHAR wcls[128] = L"", wtitle[128] = L"";
        p.x = atoi(argv[2]);
        p.y = atoi(argv[3]);
        h = WindowFromPoint(p);
        root = GetAncestor(h, GA_ROOT);
        GetWindowThreadProcessId(root, &pid);
        GetClassNameW(root, wcls, 127);
        GetWindowTextW(root, wtitle, 127);
        WideCharToMultiByte(CP_UTF8, 0, wcls, -1, cls, 255, NULL, NULL);
        WideCharToMultiByte(CP_UTF8, 0, wtitle, -1, title, 255, NULL, NULL);
        printf("at %ld,%ld -> hwnd=0x%llX root=0x%llX pid=%lu class=%s title=%s\n",
               p.x, p.y, (unsigned long long)(UINT_PTR)h,
               (unsigned long long)(UINT_PTR)root, pid, cls, title);
        return 0;
    }

    if (!strcmp(argv[1], "mouseoff")) {
        SetCursorPos(GetSystemMetrics(SM_CXSCREEN) - 3,
                     GetSystemMetrics(SM_CYSCREEN) - 3);
        printf("mouse parked\n");
        return 0;
    }

    printf("unknown command\n");
    return 2;
}
