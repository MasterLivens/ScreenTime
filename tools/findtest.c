/* 对比 FindWindowW / FindWindowExW 能否找到隐藏窗口 */
#include <windows.h>
#include <stdio.h>

int main(void)
{
    SetConsoleOutputCP(CP_UTF8);
    HWND a = FindWindowW(L"ScreenTimeStatus", NULL);
    DWORD e1 = GetLastError();
    HWND b = FindWindowExW(NULL, NULL, L"ScreenTimeStatus", NULL);
    DWORD e2 = GetLastError();
    HWND c = FindWindowW(NULL, L"屏幕使用时间管理");
    DWORD e3 = GetLastError();
    HWND d = FindWindowW(L"ScreenTimeStatus", L"屏幕使用时间管理");
    DWORD e4 = GetLastError();
    printf("FindWindowW(cls,NULL)=%llu err=%lu\n", (unsigned long long)(UINT_PTR)a, e1);
    printf("FindWindowExW(0,0,cls,NULL)=%llu err=%lu\n", (unsigned long long)(UINT_PTR)b, e2);
    printf("FindWindowW(NULL,title)=%llu err=%lu\n", (unsigned long long)(UINT_PTR)c, e3);
    printf("FindWindowW(cls,title)=%llu err=%lu\n", (unsigned long long)(UINT_PTR)d, e4);
    if (a) printf("visible=%d\n", (int)IsWindowVisible(a));
    return 0;
}
