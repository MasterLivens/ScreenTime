/* 测试辅助：向锁屏窗口的密码框写入文本并触发解锁（仅用于自动化验证） */
#include <windows.h>
#include <stdio.h>
#include <string.h>
#include <wchar.h>

int main(int argc, char **argv)
{
    SetConsoleOutputCP(CP_UTF8);
    if (argc < 2) { printf("usage: keyinject <text> [click]\n"); return 2; }

    HWND lock = FindWindowW(L"ScreenTimeLock", NULL);
    if (!lock) { printf("lock window not found\n"); return 1; }

    HWND edit = FindWindowExW(lock, NULL, L"EDIT", NULL);
    HWND btn  = FindWindowExW(lock, NULL, L"BUTTON", NULL);
    if (!edit) { printf("edit control not found\n"); return 1; }

    SendMessageW(edit, WM_SETTEXT, 0, (LPARAM)L"");

    WCHAR text[256];
    MultiByteToWideChar(CP_UTF8, 0, argv[1], -1, text, 256);
    for (int i = 0; text[i]; i++)
        SendMessageW(edit, WM_CHAR, (WPARAM)text[i], 0);

    if (argc > 2 && !_stricmp(argv[2], "click")) {
        if (!btn) { printf("button not found\n"); return 1; }
        SendMessageW(btn, BM_CLICK, 0, 0);
    } else {
        SendMessageW(edit, WM_CHAR, (WPARAM)VK_RETURN, 0);
    }

    printf("injected: %s\n", argv[1]);
    return 0;
}
