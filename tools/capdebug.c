#include <windows.h>
#include <gdiplus.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define P(...) do { printf(__VA_ARGS__); fflush(stdout); } while (0)

static int GetEncoderClsid(const WCHAR *format, CLSID *pClsid)
{
    UINT n = 0, size = 0;
    GpStatus s = GdipGetImageEncodersSize(&n, &size);
    P("EncodersSize status=%d n=%u size=%u\n", (int)s, n, size);
    if (s != Ok || size == 0) return 0;
    ImageCodecInfo *info = (ImageCodecInfo *)malloc(size);
    s = GdipGetImageEncoders(n, size, info);
    P("GetImageEncoders status=%d\n", (int)s);
    int found = 0;
    if (s == Ok) {
        for (UINT i = 0; i < n; i++) {
            char mb[128];
            WideCharToMultiByte(CP_UTF8, 0, info[i].MimeType, -1, mb, sizeof(mb), NULL, NULL);
            P("  codec[%u] %s\n", i, mb);
            if (_wcsicmp(info[i].MimeType, format) == 0) { *pClsid = info[i].Clsid; found = 1; }
        }
    }
    free(info);
    return found;
}

int main(void)
{
    P("start\n");
    SetProcessDPIAware();
    ULONG_PTR tok = 0;
    GdiplusStartupInput gin;
    memset(&gin, 0, sizeof(gin));
    gin.GdiplusVersion = 1;
    GpStatus gs = GdiplusStartup(&tok, &gin, NULL);
    P("GdiplusStartup status=%d token=%llu\n", (int)gs, (unsigned long long)tok);

    int vx = GetSystemMetrics(SM_XVIRTUALSCREEN);
    int vy = GetSystemMetrics(SM_YVIRTUALSCREEN);
    int vw = GetSystemMetrics(SM_CXVIRTUALSCREEN);
    int vh = GetSystemMetrics(SM_CYVIRTUALSCREEN);
    P("virtual screen x=%d y=%d w=%d h=%d\n", vx, vy, vw, vh);

    HDC hDC = GetDC(NULL);
    P("GetDC(NULL)=%p\n", (void*)hDC);
    HDC mDC = CreateCompatibleDC(hDC);
    HBITMAP full = CreateCompatibleBitmap(hDC, vw, vh);
    P("bitmap=%p\n", (void*)full);
    SelectObject(mDC, full);
    BOOL b = BitBlt(mDC, 0, 0, vw, vh, hDC, vx, vy, SRCCOPY | CAPTUREBLT);
    P("BitBlt=%d err=%lu\n", (int)b, GetLastError());

    CLSID clsid;
    int found = GetEncoderClsid(L"image/png", &clsid);
    P("png clsid found=%d\n", found);

    GpBitmap *img = NULL;
    GpStatus s2 = GdipCreateBitmapFromHBITMAP(full, NULL, &img);
    P("CreateBitmapFromHBITMAP status=%d img=%p\n", (int)s2, (void*)img);

    if (img && found) {
        GpStatus s3 = GdipSaveImageToFile(img, L".selftest\\dbg.png", &clsid, NULL);
        P("SaveImageToFile status=%d err=%lu\n", (int)s3, GetLastError());
    }
    if (img) GdipDisposeImage(img);
    DeleteObject(full);
    DeleteDC(mDC);
    ReleaseDC(NULL, hDC);
    GdiplusShutdown(tok);
    P("done\n");
    return 0;
}
