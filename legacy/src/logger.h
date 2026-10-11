#ifndef ST_LOGGER_H
#define ST_LOGGER_H

#include <windows.h>

void LogInit(const WCHAR *logDir);
void LogClose(void);
void LogEventW(const WCHAR *fmt, ...);
void LogVisionW(const WCHAR *file, const WCHAR *answer, LONGLONG ms, int ok);

#endif
