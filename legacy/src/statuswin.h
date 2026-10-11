#ifndef ST_STATUSWIN_H
#define ST_STATUSWIN_H

#include <windows.h>

#define WM_SHOWREQ (WM_APP + 2)   /* 再次运行程序时请求显示已隐藏的窗口 */

HWND StatusWinCreate(void);

#endif
