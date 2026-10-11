/* 打印某进程全部顶层窗口的完整几何（含 left/top），用于核对 AWTK/SDL 窗口是否完整落在屏幕内。
   用法：awtkinfo.exe <pid|进程名> */
#include <windows.h>
#include <tlhelp32.h>
#include <stdio.h>
#include <stdlib.h>

static const char *W2U(const WCHAR *w)
{
    static char buf[4][512];
    static int idx = 0;
    idx = (idx + 1) & 3;
    buf[idx][0] = 0;
    if (w) WideCharToMultiByte(CP_UTF8, 0, w, -1, buf[idx], 512, NULL, NULL);
    return buf[idx];
}

static DWORD g_target;
static int   g_count;

static BOOL CALLBACK Cb(HWND hwnd, LPARAM lp)
{
    (void)lp;
    DWORD pid = 0;
    GetWindowThreadProcessId(hwnd, &pid);
    if (pid != g_target) return TRUE;

    WCHAR cls[256] = L"", title[256] = L"";
    GetClassNameW(hwnd, cls, 255);
    GetWindowTextW(hwnd, title, 255);
    RECT wr, cr;
    GetWindowRect(hwnd, &wr);
    GetClientRect(hwnd, &cr);
    POINT tl = { cr.left, cr.top };
    ClientToScreen(hwnd, &tl);
    LONG style = (LONG)GetWindowLongPtrW(hwnd, GWL_STYLE);
    LONG ex    = (LONG)GetWindowLongPtrW(hwnd, GWL_EXSTYLE);

    RECT wa;
    SystemParametersInfoW(SPI_GETWORKAREA, 0, &wa, 0);

    g_count++;
    printf("hwnd=0x%llX class=%s title=%s\n",
           (unsigned long long)(UINT_PTR)hwnd, W2U(cls), W2U(title));
    printf("   win  L=%ld T=%ld R=%ld B=%ld  (%dx%d)\n",
           wr.left, wr.top, wr.right, wr.bottom,
           (int)(wr.right - wr.left), (int)(wr.bottom - wr.top));
    printf("   cli  L=%ld T=%ld R=%ld B=%ld  (%dx%d)\n",
           tl.x, tl.y, tl.x + cr.right, tl.y + cr.bottom,
           (int)cr.right, (int)cr.bottom);
    printf("   vis=%d topmost=%d style=%08lX ex=%08lX\n",
           (int)IsWindowVisible(hwnd),
           (int)((ex & WS_EX_TOPMOST) != 0), style, ex);
    printf("   workarea L=%ld T=%ld R=%ld B=%ld   fully_inside=%d\n",
           wa.left, wa.top, wa.right, wa.bottom,
           (int)(wr.left >= wa.left && wr.top >= wa.top &&
                 wr.right <= wa.right && wr.bottom <= wa.bottom));
    return TRUE;
}

int main(int argc, char **argv)
{
    SetConsoleOutputCP(CP_UTF8);
    SetProcessDPIAware();
    if (argc < 2) { printf("usage: awtkinfo.exe <pid|exename>\n"); return 2; }

    char *end = NULL;
    unsigned long v = strtoul(argv[1], &end, 10);
    if (end && *end == 0 && v != 0) {
        g_target = (DWORD)v;
    } else {
        WCHAR name[128];
        MultiByteToWideChar(CP_UTF8, 0, argv[1], -1, name, 127);
        if (name[0] && name[wcslen(name) - 1] != L'.') wcscat(name, L".exe");
        HANDLE snap = CreateToolhelp32Snapshot(TH32CS_SNAPPROCESS, 0);
        PROCESSENTRY32W pe;
        pe.dwSize = sizeof(pe);
        if (snap != INVALID_HANDLE_VALUE && Process32FirstW(snap, &pe)) {
            do {
                if (!_wcsicmp(pe.szExeFile, name)) { g_target = pe.th32ProcessID; break; }
            } while (Process32NextW(snap, &pe));
        }
        if (snap != INVALID_HANDLE_VALUE) CloseHandle(snap);
        if (!g_target) { printf("process not found: %s\n", argv[1]); return 3; }
    }

    printf("pid=%lu\n", g_target);
    EnumWindows(Cb, 0);
    if (!g_count) printf("no top-level windows\n");
    return 0;
}
