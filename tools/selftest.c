/* 独立自测：屏幕截图 -> 模型识别（控制台版，便于定位问题） */
#include <windows.h>
#include <gdiplus.h>
#include <stdio.h>
#include <string.h>
#include <strsafe.h>

#include "config.h"
#include "util.h"
#include "capture.h"
#include "vision.h"

int main(void)
{
    SetProcessDPIAware();

    ULONG_PTR tok = 0;
    GdiplusStartupInput gin;
    memset(&gin, 0, sizeof(gin));
    gin.GdiplusVersion = 1;
    GdiplusStartup(&tok, &gin, NULL);

    Config c;
    ConfigDefaults(&c);

    EnsureDirW(L".selftest");

    WCHAR path[MAX_PATH];
    StringCchCopyW(path, MAX_PATH, L".selftest\\shot.png");

    int w = 0, h = 0;
    if (!CapturePng(path, 1280, &w, &h)) {
        printf("CapturePng FAILED (GetLastError=%lu)\n", GetLastError());
        GdiplusShutdown(tok);
        return 1;
    }
    WIN32_FIND_DATAW fd;
    HANDLE fh = FindFirstFileW(path, &fd);
    long long fsize = (fh != INVALID_HANDLE_VALUE)
        ? ((long long)fd.nFileSizeHigh << 32) | fd.nFileSizeLow : -1;
    if (fh != INVALID_HANDLE_VALUE) FindClose(fh);
    printf("CapturePng OK %dx%d file=%lld bytes\n", w, h, fsize);

    VisionResult r;
    int ok = VisionAnalyze(&c, path, &r);
    printf("VisionAnalyze ok=%d http=%d ms=%lld\n", ok, r.status, r.ms);

    char u8[4000];
    WToUtf8(r.answer, u8, (int)sizeof(u8));
    printf("answer: %s\n", u8);

    GdiplusShutdown(tok);
    return ok ? 0 : 2;
}
