#ifndef ST_APPSTATE_H
#define ST_APPSTATE_H

#include <windows.h>
#include "config.h"

extern Config  g_cfg;

extern WCHAR   g_baseDir[];
extern WCHAR   g_configPath[];
extern WCHAR   g_shotDir[];
extern WCHAR   g_logDir[];

/* 计时（毫秒 / GetTickCount64） */
extern volatile LONGLONG g_usageMs;         /* 本次会话已使用毫秒 */
extern volatile LONGLONG g_sessionLenMs;    /* 本次会话允许时长毫秒 */
extern volatile LONGLONG g_lastInputTick;   /* 最近一次键盘/鼠标输入 */
extern volatile LONGLONG g_sessionStartTick;
extern volatile LONGLONG g_lockStartTick;
extern volatile LONGLONG g_lockEndTick;

extern volatile LONG g_locked;
extern volatile LONG g_wrongCount;
extern volatile LONG g_warned;
extern volatile LONGLONG g_pendingPenaltyMs;  /* 下次会话要扣减的毫秒 */

extern volatile LONGLONG g_lastShotTick;
extern WCHAR g_lastShotFile[];
extern WCHAR g_lastAnswer[];

extern HINSTANCE g_hInst;
extern HWND g_statusWnd;
extern HWND g_lockWnd;

void SessionReset(LONGLONG lenMs, const WCHAR *reason);
void SessionTick(void);

#endif
