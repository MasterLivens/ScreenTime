/* ============================================================================
 *  mediaprobe.exe —— 单独看"视频检测"的真实读数（不启动界面）
 *
 *  用途：排查"为什么没进入/为什么一直不进观影模式"。
 *  每 500ms 打印一行：音频会话 / GPU 硬解 / 全屏 / 视频 / 观影 + 明细。
 *  按 Ctrl+C 退出。
 *
 *  编译：
 *    gcc -O2 -std=gnu11 -DUNICODE -D_UNICODE -D_WIN32_WINNT=0x0601 ^
 *        tools\mediaprobe.c src\media.c -o out\mediaprobe.exe ^
 *        -lole32 -luuid -lpdh -lshell32 -luser32 -lgdi32
 * ========================================================================== */

#include <windows.h>
#include <stdio.h>
#include <strsafe.h>

#include "media.h"

int main(int argc, char **argv)
{
    int i;
    int rounds = 40;
    int intervalMs = 500;

    if (argc > 1) {
        int v = atoi(argv[1]);
        if (v > 0 && v <= 600) rounds = v;
        if (argc > 2) {
            int t = atoi(argv[2]);
            if (t >= 50 && t <= 5000) intervalMs = t;
        }
    }

    MediaStart();
    printf("mediaprobe: %d 次采样，每次间隔 %d ms（Ctrl+C 退出）\n", rounds, intervalMs);

    for (i = 0; i < rounds; i++) {
        const WCHAR *d = MediaLastDetail();
        char u8[512];

        u8[0] = 0;
        WideCharToMultiByte(CP_UTF8, 0, d ? d : L"", -1, u8, (int)sizeof(u8), NULL, NULL);
        printf("[%2d] video=%d fullscreen=%d | %s\n", i,
               MediaVideoActive(), MediaFullscreenVideo(), u8);
        fflush(stdout);
        Sleep(intervalMs);
    }

    MediaStop();
    return 0;
}
