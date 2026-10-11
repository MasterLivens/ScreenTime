#ifndef ST_HOOKS_H
#define ST_HOOKS_H

#include <windows.h>

void HooksInstall(void);
void HooksUninstall(void);
void HooksSetLocked(int locked);
void HooksSetBlockHotkeys(int block);

#endif
