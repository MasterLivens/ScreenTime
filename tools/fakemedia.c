/* ============================================================================
 *  fakemedia.exe —— 造一个"假播放器"窗口，用来测试全屏/窗口化判定
 *
 *    fakemedia.exe              铺满整个虚拟屏幕的无边框置顶窗口（模拟全屏播放）
 *    fakemedia.exe --windowed   普通 800x450 窗口（模拟窗口化播放）
 *
 *  ESC 或关闭窗口退出。
 *
 *  编译：
 *    gcc -O2 -std=gnu11 -DUNICODE -D_UNICODE -D_WIN32_WINNT=0x0601 ^
 *        tools\fakemedia.c -o out\fakemedia.exe -lgdi32 -luser32
 * ========================================================================== */

#include <windows.h>
#include <strsafe.h>

static LRESULT CALLBACK FakeProc(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp)
{
    switch (msg) {
    case WM_KEYDOWN:
        if (wp == VK_ESCAPE) { DestroyWindow(hwnd); return 0; }
        return 0;
    case WM_DESTROY:
        PostQuitMessage(0);
        return 0;
    default:
        break;
    }
    return DefWindowProcW(hwnd, msg, wp, lp);
}

int WINAPI wWinMain(HINSTANCE hInst, HINSTANCE hPrev, LPWSTR cmdLine, int nCmdShow)
{
    WNDCLASSEXW wc;
    HWND hwnd;
    MSG msg;
    int fullscreen = 1;

    (void)hPrev; (void)nCmdShow;

    if (cmdLine != NULL && wcsstr(cmdLine, L"--windowed") != NULL) fullscreen = 0;

    memset(&wc, 0, sizeof(wc));
    wc.cbSize = sizeof(wc);
    wc.lpfnWndProc = FakeProc;
    wc.hInstance = hInst;
    wc.hCursor = LoadCursorW(NULL, IDC_ARROW);
    wc.lpszClassName = L"ScreenTimeFakeMedia";
    RegisterClassExW(&wc);

    if (fullscreen) {
        int x = GetSystemMetrics(SM_XVIRTUALSCREEN);
        int y = GetSystemMetrics(SM_YVIRTUALSCREEN);
        int w = GetSystemMetrics(SM_CXVIRTUALSCREEN);
        int h = GetSystemMetrics(SM_CYVIRTUALSCREEN);
        if (w <= 0) w = GetSystemMetrics(SM_CXSCREEN);
        if (h <= 0) h = GetSystemMetrics(SM_CYSCREEN);
        hwnd = CreateWindowExW(WS_EX_TOPMOST, wc.lpszClassName, L"FakeMedia (fullscreen)",
                               WS_POPUP | WS_VISIBLE, x, y, w, h,
                               NULL, NULL, hInst, NULL);
    } else {
        hwnd = CreateWindowExW(0, wc.lpszClassName, L"FakeMedia (windowed)",
                               WS_OVERLAPPEDWINDOW | WS_VISIBLE, 120, 120, 800, 450,
                               NULL, NULL, hInst, NULL);
    }
    if (hwnd == NULL) return 1;

    SetForegroundWindow(hwnd);
    while (GetMessageW(&msg, NULL, 0, 0) > 0) {
        TranslateMessage(&msg);
        DispatchMessageW(&msg);
    }
    return 0;
}
