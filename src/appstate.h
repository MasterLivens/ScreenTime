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

/* 版本号（与根目录 VERSION 保持一致） */
#ifndef ST_VERSION
#define ST_VERSION L"1.1.2"
#endif

/* 初始化进度：每进入一个可能失败的步骤调用一次。
   既更新全局，也写一行 activity.log —— 万一进程在启动阶段崩掉，
   日志最后一行就是死在哪一步，远程排查黑屏/闪退时非常关键。 */
extern const WCHAR *g_initStage;
void SetInitStage(const WCHAR *stage);

void SessionReset(LONGLONG lenMs, const WCHAR *reason);
void SessionTick(void);

#endif
