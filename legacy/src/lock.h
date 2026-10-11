#ifndef ST_LOCK_H
#define ST_LOCK_H

#include <windows.h>

void LockBegin(LONGLONG breakMs, const WCHAR *reason);
void LockEnd(int autoUnlock);
int  LockIsActive(void);

#endif
