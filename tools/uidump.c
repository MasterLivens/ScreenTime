/* 打印 ScreenTime 各窗口的窗口/客户区尺寸、DPI 与子控件位置，用于检查界面是否被裁切。
   用法：uidump.exe [窗口类名 ...]  （默认 ScreenTimeStatus ScreenTimeParentDlg ScreenTimeLock） */
#include <windows.h>
#include <stdio.h>

typedef int DSH_AWARENESS;   /* 0=unaware 1=system 2=permonitor 3=permonitorv2 -1=未知 */
typedef BOOL (WINAPI *GETPROCDEP)(HANDLE, DSH_AWARENESS *);
typedef UINT (WINAPI *GETDPIFORWINDOW)(HWND);
typedef UINT (WINAPI *GETSYSTEMDPIFORWINDOW)(HWND);

static const char *W2U(const WCHAR *w)
{
    static char buf[4][512];
    static int idx = 0;
    idx = (idx + 1) & 3;
    buf[idx][0] = 0;
    if (w) WideCharToMultiByte(CP_UTF8, 0, w, -1, buf[idx], 512, NULL, NULL);
    return buf[idx];
}

static BOOL CALLBACK ChildCb(HWND child, LPARAM lparam)
{
    (void)lparam;
    RECT wr, cr;
    GetWindowRect(child, &wr);
    GetClientRect(child, &cr);
    WCHAR text[128] = L"", cls[128] = L"";
    GetWindowTextW(child, text, 127);
    GetClassNameW(child, cls, 127);
    printf("   child id=%-5d cls=%-8s text=%-12s win=%dx%d client=%dx%d vis=%d\n",
           GetDlgCtrlID(child), W2U(cls), W2U(text),
           (int)(wr.right - wr.left), (int)(wr.bottom - wr.top), (int)cr.right, (int)cr.bottom,
           (int)IsWindowVisible(child));
    return TRUE;
}

int main(int argc, char **argv)
{
    SetConsoleOutputCP(CP_UTF8);
    SetProcessDPIAware();   /* 本工具按物理像素测量，避免被 DPI 虚拟化 */
    static WCHAR store[6][128];
    const WCHAR *classes[6];
    int n = 0;
    if (argc > 1) {
        for (int i = 1; i < argc && n < 6; i++) {
            int k = n;
            MultiByteToWideChar(CP_UTF8, 0, argv[i], -1, store[k], 127);
            classes[k] = store[k];
            n = k + 1;
        }
    } else {
        const WCHAR *dflt[] = { L"ScreenTimeStatus", L"ScreenTimeParentDlg", L"ScreenTimeLock" };
        for (int i = 0; i < 3; i++) classes[n++] = dflt[i];
    }

    HMODULE u32 = GetModuleHandleW(L"user32.dll");
    GETDPIFORWINDOW pDpi = (GETDPIFORWINDOW)GetProcAddress(u32, "GetDpiForWindow");
    GETSYSTEMDPIFORWINDOW pSysDpi = (GETSYSTEMDPIFORWINDOW)GetProcAddress(u32, "GetDpiForSystem");
    HMODULE sh32 = GetModuleHandleW(L"shell32.dll");
    GETPROCDEP pAware = (GETPROCDEP)GetProcAddress(sh32, "GetProcessDpiAwareness");

    HDC screen = GetDC(NULL);
    int logpix = GetDeviceCaps(screen, LOGPIXELSX);
    ReleaseDC(NULL, screen);
    printf("screen LOGPIXELSX=%d  SM_CXSCREEN=%d SM_CYSCREEN=%d  SM_CYCAPTION=%d SM_CXDLGFRAME=%d SM_CXBORDER=%d\n",
           logpix, GetSystemMetrics(SM_CXSCREEN), GetSystemMetrics(SM_CYSCREEN),
           GetSystemMetrics(SM_CYCAPTION), GetSystemMetrics(SM_CXDLGFRAME), GetSystemMetrics(SM_CXBORDER));

    for (int i = 0; i < n; i++) {
        HWND hwnd = FindWindowW(classes[i], NULL);
        if (!hwnd) { printf("no window of class %s\n", W2U(classes[i])); continue; }
        RECT wr, cr;
        GetWindowRect(hwnd, &wr);
        GetClientRect(hwnd, &cr);
        WCHAR title[128];
        GetWindowTextW(hwnd, title, 127);
        DWORD pid = 0;
        GetWindowThreadProcessId(hwnd, &pid);
        UINT dpi = pDpi ? pDpi(hwnd) : 0;
        DSH_AWARENESS aware = -1;
        if (pAware) {
            HANDLE h = OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION, FALSE, pid);
            if (h) { pAware(h, &aware); CloseHandle(h); }
        }
        printf("class=%s title=%s pid=%u\n", W2U(classes[i]), W2U(title), (unsigned)pid);
        printf("   window=%dx%d client=%dx%d  dpi=%u sysDpi=%u awareness=%d\n",
               (int)(wr.right - wr.left), (int)(wr.bottom - wr.top), (int)cr.right, (int)cr.bottom,
               dpi, pSysDpi ? pSysDpi(NULL) : 0, (int)aware);
        EnumChildWindows(hwnd, ChildCb, 0);
    }
    return 0;
}
