/* 列出某个进程创建的所有顶层窗口（含隐藏窗口）。用法：enumwin.exe <pid> */
#include <windows.h>
#include <stdio.h>

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
    LONG style = (LONG)GetWindowLongPtrW(hwnd, GWL_STYLE);
    LONG ex    = (LONG)GetWindowLongPtrW(hwnd, GWL_EXSTYLE);
    g_count++;
    printf("hwnd=%llu class=%-22s title=%-22s win=%dx%d client=%dx%d vis=%d style=%08lx ex=%08lx\n",
           (unsigned long long)(UINT_PTR)hwnd, W2U(cls), W2U(title),
           (int)(wr.right - wr.left), (int)(wr.bottom - wr.top), (int)cr.right, (int)cr.bottom,
           (int)IsWindowVisible(hwnd), style, ex);
    return TRUE;
}

int main(int argc, char **argv)
{
    SetConsoleOutputCP(CP_UTF8);
    SetProcessDPIAware();
    if (argc < 2) { printf("usage: enumwin.exe <pid>\n"); return 2; }
    g_target = (DWORD)strtoul(argv[1], NULL, 10);
    EnumWindows(Cb, 0);
    printf("total top-level windows for pid=%lu: %d\n", g_target, g_count);
    return 0;
}
