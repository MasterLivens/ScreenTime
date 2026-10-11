#ifndef ST_VISION_H
#define ST_VISION_H

#include <windows.h>
#include "config.h"

typedef struct {
    int    ok;
    int    status;
    LONGLONG ms;
    WCHAR  answer[2048];
} VisionResult;

int VisionAnalyze(const Config *cfg, const WCHAR *pngPath, VisionResult *res);

#endif
