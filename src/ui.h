#ifndef ST_UI_H
#define ST_UI_H

#include <windows.h>

/* 单实例：再次运行程序时，用这个消息让已经运行的实例把窗口显示出来 */
#define WM_SHOWREQ (WM_APP + 2)

/* AWTK 界面层 */
int  UiInit(void);        /* 返回 0 表示失败 */
void UiRun(void);         /* 进入 AWTK 主循环（阻塞） */
void UiShutdown(void);    /* 清理托盘图标等 */

/* 锁屏（由界面层实现） */
void LockBegin(LONGLONG breakMs, const WCHAR *reason);
void LockEnd(int autoUnlock);
int  LockIsActive(void);

#endif
