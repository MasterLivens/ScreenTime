#ifndef ST_UTIL_H
#define ST_UTIL_H

#include <windows.h>

void ExeDirW(WCHAR *out, size_t n);
void EnsureDirW(const WCHAR *path);
void NowStampW(WCHAR *out, size_t n);
void NowStampA(char *out, size_t n);          /* UTF-8 */
void DateDirW(WCHAR *out, size_t n);          /* YYYY-MM-DD */
void TimeFileW(WCHAR *out, size_t n);         /* HH-MM-SS */

int  WToUtf8(const WCHAR *w, char *out, int outlen);
int  Utf8ToW(const char *u8, WCHAR *out, int outlen);

char *Base64Encode(const unsigned char *data, size_t len);   /* malloc'd, NULL on failure */
void  JsonEscapeA(const char *in, char *out, size_t cap);

/* 界面 DPI 缩放：96 DPI -> 100%，120 DPI -> 125% */
int  UiScalePercent(void);
int  UiPx(int designPx);            /* 把设计像素换算成当前 DPI 下的像素 */
void OuterSizeForClient(DWORD style, DWORD exstyle, int clientW, int clientH,
                        int *outW, int *outH);   /* 由客户区尺寸算出窗口外框尺寸 */

#endif
