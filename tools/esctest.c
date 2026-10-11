/* Test: does GetWindowTextW return text for an ES_PASSWORD edit in the same process? */
#include <windows.h>
#include <stdio.h>

static LRESULT CALLBACK WndProc(HWND h, UINT m, WPARAM w, LPARAM l)
{
    return DefWindowProcW(h, m, w, l);
}

static void Run(const WCHAR *label, DWORD extraStyle)
{
    WNDCLASSEXW wc;
    memset(&wc, 0, sizeof(wc));
    wc.cbSize = sizeof(wc);
    wc.lpfnWndProc = WndProc;
    wc.hInstance = GetModuleHandleW(NULL);
    wc.lpszClassName = L"EsTestClass";
    RegisterClassExW(&wc);

    HWND top = CreateWindowExW(0, L"EsTestClass", L"t", WS_POPUP, 0, 0, 200, 100,
                               NULL, NULL, wc.hInstance, NULL);
    HWND edit = CreateWindowExW(WS_EX_CLIENTEDGE, L"EDIT", L"",
                                WS_CHILD | WS_VISIBLE | ES_AUTOHSCROLL | extraStyle,
                                10, 10, 150, 26, top, (HMENU)305, wc.hInstance, NULL);

    SetWindowTextW(edit, L"642101");

    WCHAR buf[128];
    memset(buf, 0, sizeof(buf));
    BOOL ok = GetWindowTextW(edit, buf, 127);
    int len = GetWindowTextLengthW(edit);

    WCHAR buf2[128];
    memset(buf2, 0, sizeof(buf2));
    LRESULT sm = SendMessageW(edit, WM_GETTEXT, 127, (LPARAM)buf2);

    WCHAR buf3[128];
    memset(buf3, 0, sizeof(buf3));
    LRESULT sm2 = SendMessageW(edit, EM_GETLINE, 0, (LPARAM)buf3);

    wprintf(L"%ls: GetWindowTextLength=%d  GetWindowText ret=%d text=[%ls]  WM_GETTEXT ret=%d text=[%ls]  EM_GETLINE ret=%d text=[%ls]\n",
            label, len, (int)ok, buf, (int)sm, buf2, (int)sm2, buf3);

    DestroyWindow(edit);
    DestroyWindow(top);
}

int main(void)
{
    Run(L"plain EDIT", 0);
    Run(L"ES_PASSWORD", ES_PASSWORD);
    return 0;
}
