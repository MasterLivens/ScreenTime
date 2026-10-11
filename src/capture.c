#include "capture.h"
#include <gdiplus.h>
#include <strsafe.h>
#include <stdlib.h>

static int GetEncoderClsid(const WCHAR *format, CLSID *pClsid)
{
    UINT n = 0, size = 0;
    if (GdipGetImageEncodersSize(&n, &size) != Ok || size == 0) return 0;
    ImageCodecInfo *info = (ImageCodecInfo *)malloc(size);
    if (!info) return 0;
    int found = 0;
    if (GdipGetImageEncoders(n, size, info) == Ok) {
        for (UINT i = 0; i < n; i++) {
            if (_wcsicmp(info[i].MimeType, format) == 0) {
                *pClsid = info[i].Clsid;
                found = 1;
                break;
            }
        }
    }
    free(info);
    return found;
}

int CapturePng(const WCHAR *outPath, int maxW, int *outW, int *outH)
{
    int vx = GetSystemMetrics(SM_XVIRTUALSCREEN);
    int vy = GetSystemMetrics(SM_YVIRTUALSCREEN);
    int vw = GetSystemMetrics(SM_CXVIRTUALSCREEN);
    int vh = GetSystemMetrics(SM_CYVIRTUALSCREEN);
    if (vw <= 0 || vh <= 0) {
        vw = GetSystemMetrics(SM_CXSCREEN);
        vh = GetSystemMetrics(SM_CYSCREEN);
        vx = 0; vy = 0;
    }
    if (vw <= 0 || vh <= 0) return 0;

    int tw = vw, th = vh;
    if (maxW > 0 && vw > maxW) {
        tw = maxW;
        th = (int)(((LONGLONG)vh * maxW) / vw);
        if (th < 1) th = 1;
    }

    int status = 0;
    HDC hDC  = GetDC(NULL);
    HDC mDC  = CreateCompatibleDC(hDC);
    HBITMAP full = CreateCompatibleBitmap(hDC, vw, vh);
    HBITMAP oldFull = (HBITMAP)SelectObject(mDC, full);
    if (BitBlt(mDC, 0, 0, vw, vh, hDC, vx, vy, SRCCOPY | CAPTUREBLT)) {
        HBITMAP use = full;
        HDC dDC = NULL;
        HBITMAP scaled = NULL, oldScaled = NULL;

        if (tw != vw || th != vh) {
            scaled = CreateCompatibleBitmap(hDC, tw, th);
            if (scaled) {
                dDC = CreateCompatibleDC(hDC);
                oldScaled = (HBITMAP)SelectObject(dDC, scaled);
                SetStretchBltMode(dDC, HALFTONE);
                SetBrushOrgEx(dDC, 0, 0, NULL);
                if (StretchBlt(dDC, 0, 0, tw, th, mDC, 0, 0, vw, vh, SRCCOPY))
                    use = scaled;
            }
        }

        GpBitmap *img = NULL;
        if (GdipCreateBitmapFromHBITMAP(use, NULL, &img) == Ok && img) {
            CLSID pngClsid;
            if (GetEncoderClsid(L"image/png", &pngClsid) == 1) {
                if (GdipSaveImageToFile(img, outPath, &pngClsid, NULL) == Ok) {
                    status = 1;
                    if (outW) *outW = tw;
                    if (outH) *outH = th;
                }
            }
            GdipDisposeImage(img);
        }

        if (scaled) {
            if (oldScaled) SelectObject(dDC, oldScaled);
            DeleteDC(dDC);
            DeleteObject(scaled);
        }
    }

    SelectObject(mDC, oldFull);
    DeleteObject(full);
    DeleteDC(mDC);
    ReleaseDC(NULL, hDC);
    return status;
}
