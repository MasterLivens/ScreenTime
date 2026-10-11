/* 验证：能否用 CreateFileW 覆写一个带隐藏属性的文件（模拟 RewritePasswordLines 的写入步骤） */
#include <windows.h>
#include <stdio.h>
#include <string.h>

int main(void)
{
    const WCHAR *p = L"D:\\Work\\ScreenTime\\_hidden_test.txt";
    DWORD attrs = GetFileAttributesW(p);
    if (attrs == INVALID_FILE_ATTRIBUTES) {
        HANDLE h = CreateFileW(p, GENERIC_WRITE, 0, NULL, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, NULL);
        if (h == INVALID_HANDLE_VALUE) { wprintf(L"create fail err=%lu\n", GetLastError()); return 1; }
        CloseHandle(h);
        attrs = GetFileAttributesW(p);
    }
    if (!SetFileAttributesW(p, FILE_ATTRIBUTE_HIDDEN)) { wprintf(L"SetFileAttributes fail err=%lu\n", GetLastError()); }
    wprintf(L"attrs before rewrite = %lu (hidden=%d)\n", GetFileAttributesW(p),
            (GetFileAttributesW(p) & FILE_ATTRIBUTE_HIDDEN) ? 1 : 0);

    HANDLE w = CreateFileW(p, GENERIC_WRITE, 0, NULL, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, NULL);
    if (w == INVALID_HANDLE_VALUE) {
        wprintf(L"plain CREATE_ALWAYS failed err=%lu -> retry with preserved attributes\n", GetLastError());
        DWORD attrs = GetFileAttributesW(p);
        w = CreateFileW(p, GENERIC_WRITE, 0, NULL, CREATE_ALWAYS, attrs, NULL);
        if (w == INVALID_HANDLE_VALUE) {
            wprintf(L"retry-with-attrs failed err=%lu\n", GetLastError());
            SetFileAttributesW(p, FILE_ATTRIBUTE_NORMAL);
            w = CreateFileW(p, GENERIC_WRITE, 0, NULL, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, NULL);
            if (w == INVALID_HANDLE_VALUE) { wprintf(L"final fallback failed err=%lu\n", GetLastError()); return 1; }
            wprintf(L"final fallback (clear attrs) ok\n");
        } else {
            wprintf(L"retry-with-attrs ok\n");
        }
    }
    const char *s = "rewritten\n";
    DWORD written = 0;
    BOOL ok = WriteFile(w, s, (DWORD)strlen(s), &written, NULL);
    CloseHandle(w);
    wprintf(L"rewrite ok=%d written=%lu attrs after=%lu\n", ok, written, GetFileAttributesW(p));

    HANDLE r = CreateFileW(p, GENERIC_READ, 0, NULL, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, NULL);
    if (r == INVALID_HANDLE_VALUE) { wprintf(L"read fail err=%lu\n", GetLastError()); return 1; }
    CloseHandle(r);
    wprintf(L"read of hidden file ok\n");
    DeleteFileW(p);
    return 0;
}
