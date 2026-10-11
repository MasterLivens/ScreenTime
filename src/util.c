#include "util.h"
#include <strsafe.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

void ExeDirW(WCHAR *out, size_t n)
{
    GetModuleFileNameW(NULL, out, (DWORD)n);
    WCHAR *p = wcsrchr(out, L'\\');
    if (p) *p = 0;
}

void EnsureDirW(const WCHAR *path)
{
    if (CreateDirectoryW(path, NULL)) return;
    if (GetLastError() == ERROR_ALREADY_EXISTS) return;
    /* create parents then retry once */
    WCHAR tmp[MAX_PATH];
    StringCchCopyW(tmp, MAX_PATH, path);
    WCHAR *p = wcsrchr(tmp, L'\\');
    if (p) {
        *p = 0;
        EnsureDirW(tmp);
        CreateDirectoryW(path, NULL);
    }
}

void NowStampW(WCHAR *out, size_t n)
{
    SYSTEMTIME st;
    GetLocalTime(&st);
    StringCchPrintfW(out, n, L"%04d-%02d-%02d %02d:%02d:%02d",
                     st.wYear, st.wMonth, st.wDay, st.wHour, st.wMinute, st.wSecond);
}

void DateDirW(WCHAR *out, size_t n)
{
    SYSTEMTIME st;
    GetLocalTime(&st);
    StringCchPrintfW(out, n, L"%04d-%02d-%02d", st.wYear, st.wMonth, st.wDay);
}

void TimeFileW(WCHAR *out, size_t n)
{
    SYSTEMTIME st;
    GetLocalTime(&st);
    StringCchPrintfW(out, n, L"%02d-%02d-%02d", st.wHour, st.wMinute, st.wSecond);
}

void NowStampA(char *out, size_t n)
{
    WCHAR w[64];
    NowStampW(w, 64);
    WToUtf8(w, out, (int)n);
}

int WToUtf8(const WCHAR *w, char *out, int outlen)
{
    if (!w || !out || outlen <= 0) return 0;
    int n = WideCharToMultiByte(CP_UTF8, 0, w, -1, out, outlen, NULL, NULL);
    if (n <= 0) { out[0] = 0; return 0; }
    return n;
}

int Utf8ToW(const char *u8, WCHAR *out, int outlen)
{
    if (!u8 || !out || outlen <= 0) return 0;
    int n = MultiByteToWideChar(CP_UTF8, 0, u8, -1, out, outlen);
    if (n <= 0) { out[0] = 0; return 0; }
    return n;
}

static const char B64[] = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";

char *Base64Encode(const unsigned char *data, size_t len)
{
    size_t olen = 4 * ((len + 2) / 3);
    char *out = (char *)malloc(olen + 1);
    if (!out) return NULL;
    size_t i, o;
    for (i = 0, o = 0; i + 2 < len; i += 3) {
        out[o++] = B64[(data[i] >> 2) & 0x3F];
        out[o++] = B64[((data[i] & 0x3) << 4) | ((data[i + 1] >> 4) & 0xF)];
        out[o++] = B64[((data[i + 1] & 0xF) << 2) | ((data[i + 2] >> 6) & 0x3)];
        out[o++] = B64[data[i + 2] & 0x3F];
    }
    if (i < len) {
        out[o++] = B64[(data[i] >> 2) & 0x3F];
        if (i + 1 == len) {
            out[o++] = B64[(data[i] & 0x3) << 4];
            out[o++] = '=';
        } else {
            out[o++] = B64[((data[i] & 0x3) << 4) | ((data[i + 1] >> 4) & 0xF)];
            out[o++] = B64[(data[i + 1] & 0xF) << 2];
        }
        out[o++] = '=';
    }
    out[o] = 0;
    return out;
}

void JsonEscapeA(const char *in, char *out, size_t cap)
{
    size_t o = 0;
    if (!in) { out[0] = 0; return; }
    while (*in && o + 2 < cap) {
        unsigned char c = (unsigned char)*in++;
        switch (c) {
        case '"':  out[o++] = '\\'; out[o++] = '"';  break;
        case '\\': out[o++] = '\\'; out[o++] = '\\'; break;
        case '\n': out[o++] = '\\'; out[o++] = 'n';  break;
        case '\r': out[o++] = '\\'; out[o++] = 'r';  break;
        case '\t': out[o++] = '\\'; out[o++] = 't';  break;
        default:
            if (c < 0x20) {
                if (o + 6 >= cap) { in = ""; break; }
                o += (size_t)sprintf(out + o, "\\u%04x", c);
            } else {
                out[o++] = (char)c;
            }
        }
    }
    out[o] = 0;
}

/* ---------------- 界面 DPI 缩放 ---------------- */

int UiScalePercent(void)
{
    static int cached = 0;
    if (cached) return cached;
    int dpi = 0;
    HDC hdc = GetDC(NULL);
    if (hdc) {
        dpi = GetDeviceCaps(hdc, LOGPIXELSX);
        ReleaseDC(NULL, hdc);
    }
    if (dpi < 96) dpi = 96;
    if (dpi > 384) dpi = 384;
    cached = dpi * 100 / 96;
    return cached;
}

int UiPx(int designPx)
{
    return (int)((designPx * UiScalePercent() + 50) / 100);
}

void OuterSizeForClient(DWORD style, DWORD exstyle, int clientW, int clientH,
                        int *outW, int *outH)
{
    RECT rc;
    rc.left = 0; rc.top = 0;
    rc.right = UiPx(clientW);
    rc.bottom = UiPx(clientH);
    if (!AdjustWindowRectEx(&rc, style, FALSE, exstyle)) {
        rc.right += 6;
        rc.bottom += 35;
    }
    if (outW) *outW = rc.right - rc.left;
    if (outH) *outH = rc.bottom - rc.top;
}
