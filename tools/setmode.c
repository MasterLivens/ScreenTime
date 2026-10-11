/* 切换/恢复显示分辨率，用于验证界面在不同分辨率下的表现。
   用法：
     setmode.exe list            列出所有可用模式
     setmode.exe <w> <h>         切到指定分辨率
     setmode.exe restore         恢复系统保存的分辨率
*/
#include <windows.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

int main(int argc, char **argv)
{
    SetConsoleOutputCP(CP_UTF8);
    SetProcessDPIAware();

    if (argc >= 2 && !_stricmp(argv[1], "list")) {
        DEVMODEW dm;
        DWORD i;
        for (i = 0; ; i++) {
            memset(&dm, 0, sizeof(dm));
            dm.dmSize = sizeof(dm);
            if (!EnumDisplaySettingsW(NULL, i, &dm)) break;
            printf("%2lu: %lux%lu  %lubpp  %luHz\n", (unsigned long)i,
                   (unsigned long)dm.dmPelsWidth, (unsigned long)dm.dmPelsHeight,
                   (unsigned long)dm.dmBitsPerPel, (unsigned long)dm.dmDisplayFrequency);
        }
        printf("--- current ---\n");
        memset(&dm, 0, sizeof(dm));
        dm.dmSize = sizeof(dm);
        if (EnumDisplaySettingsW(NULL, ENUM_CURRENT_SETTINGS, &dm)) {
            printf("current: %lux%lu  %lubpp  %luHz  (screen %dx%d, dpi %d)\n",
                   (unsigned long)dm.dmPelsWidth, (unsigned long)dm.dmPelsHeight,
                   (unsigned long)dm.dmBitsPerPel, (unsigned long)dm.dmDisplayFrequency,
                   GetSystemMetrics(SM_CXSCREEN), GetSystemMetrics(SM_CYSCREEN),
                   GetDeviceCaps(GetDC(NULL), LOGPIXELSX));
        }
        return 0;
    }

    if (argc >= 2 && !_stricmp(argv[1], "restore")) {
        LONG r = ChangeDisplaySettingsW(NULL, 0);
        Sleep(1200);
        printf("restore -> %ld   now %dx%d\n", (long)r,
               GetSystemMetrics(SM_CXSCREEN), GetSystemMetrics(SM_CYSCREEN));
        return r == DISP_CHANGE_SUCCESSFUL ? 0 : 1;
    }

    if (argc >= 3) {
        DEVMODEW dm;
        LONG r;
        memset(&dm, 0, sizeof(dm));
        dm.dmSize = sizeof(dm);
        dm.dmPelsWidth = (DWORD)atoi(argv[1]);
        dm.dmPelsHeight = (DWORD)atoi(argv[2]);
        dm.dmFields = DM_PELSWIDTH | DM_PELSHEIGHT;
        r = ChangeDisplaySettingsW(&dm, 0);
        Sleep(1500);
        printf("set %sx%s -> %ld   now %dx%d\n", argv[1], argv[2], (long)r,
               GetSystemMetrics(SM_CXSCREEN), GetSystemMetrics(SM_CYSCREEN));
        return r == DISP_CHANGE_SUCCESSFUL ? 0 : 1;
    }

    printf("usage: setmode list | setmode <w> <h> | setmode restore\n");
    return 2;
}
