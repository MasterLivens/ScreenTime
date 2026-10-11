#ifndef ST_CAPTURE_H
#define ST_CAPTURE_H

#include <windows.h>

/* 抓取整个虚拟桌面（多屏）并保存为 PNG。maxW<=0 表示原始分辨率。 */
int CapturePng(const WCHAR *outPath, int maxW, int *outW, int *outH);

#endif
