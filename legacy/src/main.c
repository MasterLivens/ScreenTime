#include <windows.h>
#include <process.h>
#include <strsafe.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <gdiplus.h>

#include "appstate.h"
#include "config.h"
#include "util.h"
#include "logger.h"
#include "capture.h"
#include "vision.h"
#include "hooks.h"
#include "lock.h"
#include "statuswin.h"

/* ---------------- 全局状态 ---------------- */
Config  g_cfg;

WCHAR   g_baseDir[MAX_PATH];
WCHAR   g_configPath[MAX_PATH];
WCHAR   g_shotDir[MAX_PATH];
WCHAR   g_logDir[MAX_PATH];

volatile LONGLONG g_usageMs = 0;
volatile LONGLONG g_sessionLenMs = 45 * 60000;
volatile LONGLONG g_lastInputTick = 0;
volatile LONGLONG g_sessionStartTick = 0;
volatile LONGLONG g_lockStartTick = 0;
volatile LONGLONG g_lockEndTick = 0;

volatile LONG g_locked = 0;
volatile LONG g_wrongCount = 0;
volatile LONG g_warned = 0;
volatile LONGLONG g_pendingPenaltyMs = 0;

volatile LONGLONG g_lastShotTick = 0;
WCHAR g_lastShotFile[512] = L"";
WCHAR g_lastAnswer[1024]  = L"";

HINSTANCE g_hInst = NULL;
HWND g_statusWnd = NULL;
HWND g_lockWnd = NULL;

static ULONG_PTR g_gdiToken = 0;
static HANDLE    g_visionThread = NULL;
static volatile LONG g_stop = 0;

/* ---------------- 会话计时 ---------------- */
void SessionReset(LONGLONG lenMs, const WCHAR *reason)
{
    LONGLONG now = (LONGLONG)GetTickCount64();
    InterlockedExchange64(&g_sessionLenMs, lenMs);
    InterlockedExchange64(&g_usageMs, 0);
    InterlockedExchange64(&g_sessionStartTick, now);
    InterlockedExchange64(&g_lastInputTick, now);
    InterlockedExchange64(&g_pendingPenaltyMs, 0);
    InterlockedExchange(&g_warned, 0);
    LogEventW(L"新的使用会话开始：允许 %lld 分钟（%s）", lenMs / 60000, reason ? reason : L"");
}

void SessionTick(void)
{
    if (g_locked) return;

    LONGLONG now = (LONGLONG)GetTickCount64();
    int idle = (g_cfg.idle_pause &&
                (now - g_lastInputTick) > (LONGLONG)g_cfg.idle_seconds * 1000);

    if (!idle)
        InterlockedAdd64(&g_usageMs, 1000);

    LONGLONG remain = g_sessionLenMs - g_usageMs;
    if (remain <= 0) {
        LockBegin((LONGLONG)g_cfg.break_minutes * 60000, L"已达到本次限时使用时长");
        return;
    }

    if (g_cfg.warn_minutes > 0 &&
        remain <= (LONGLONG)g_cfg.warn_minutes * 60000 &&
        !g_warned) {
        InterlockedExchange(&g_warned, 1);
        LogEventW(L"剩余使用时间不足 %d 分钟，即将锁屏休息", g_cfg.warn_minutes);
        MessageBeep(MB_ICONWARNING);
    }
}

/* ---------------- 截图 + 模型识别线程 ---------------- */
static unsigned int __stdcall VisionThread(void *arg)
{
    (void)arg;

    while (!g_stop) {
        int interval = g_cfg.screenshot_interval;
        if (interval < 5) interval = 5;
        for (int i = 0; i < interval * 2 && !g_stop; i++)
            Sleep(500);
        if (g_stop) break;
        if (g_locked || !g_cfg.vision_enabled) continue;

        WCHAR day[32], tm[32];
        DateDirW(day, 32);
        TimeFileW(tm, 32);

        WCHAR dir[MAX_PATH], file[MAX_PATH];
        StringCchCopyW(dir, MAX_PATH, g_shotDir);
        StringCchCatW(dir, MAX_PATH, L"\\");
        StringCchCatW(dir, MAX_PATH, day);
        EnsureDirW(dir);

        StringCchCopyW(file, MAX_PATH, dir);
        StringCchCatW(file, MAX_PATH, L"\\");
        StringCchCatW(file, MAX_PATH, tm);
        StringCchCatW(file, MAX_PATH, L".png");

        int w = 0, h = 0;
        if (!CapturePng(file, g_cfg.max_image_width, &w, &h)) {
            LogEventW(L"屏幕截图失败");
            continue;
        }

        InterlockedExchange64(&g_lastShotTick, (LONGLONG)GetTickCount64());
        StringCchCopyW(g_lastShotFile, 512, file);

        VisionResult res;
        VisionAnalyze(&g_cfg, file, &res);

        StringCchCopyW(g_lastAnswer, 1024, res.answer);

        WCHAR rel[MAX_PATH];
        StringCchCopyW(rel, MAX_PATH, L"screenshots\\");
        StringCchCatW(rel, MAX_PATH, day);
        StringCchCatW(rel, MAX_PATH, L"\\");
        StringCchCatW(rel, MAX_PATH, tm);
        StringCchCatW(rel, MAX_PATH, L".png");

        LogVisionW(rel, res.answer, res.ms, res.ok);

        if (!g_cfg.save_screenshots) DeleteFileW(file);
    }
    return 0;
}

/* ---------------- 清理过期截图 ---------------- */
static void CleanupOldShots(void)
{
    if (g_cfg.keep_days <= 0) return;

    SYSTEMTIME st;
    GetLocalTime(&st);
    FILETIME ft;
    SystemTimeToFileTime(&st, &ft);
    LONGLONG t100 = ((LONGLONG)ft.dwHighDateTime << 32) | ft.dwLowDateTime;
    t100 -= (LONGLONG)g_cfg.keep_days * 24 * 3600 * 10000000LL;
    ft.dwLowDateTime = (DWORD)(t100 & 0xFFFFFFFF);
    ft.dwHighDateTime = (DWORD)(t100 >> 32);
    FileTimeToSystemTime(&ft, &st);
    int cutoff = st.wYear * 10000 + st.wMonth * 100 + st.wDay;

    WCHAR pat[MAX_PATH];
    StringCchCopyW(pat, MAX_PATH, g_shotDir);
    StringCchCatW(pat, MAX_PATH, L"\\*");

    WIN32_FIND_DATAW fd;
    HANDLE h = FindFirstFileW(pat, &fd);
    if (h == INVALID_HANDLE_VALUE) return;

    do {
        if (!(fd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY)) continue;
        if (fd.cFileName[0] == L'.') continue;
        if (wcslen(fd.cFileName) != 10) continue;
        int y = _wtoi(fd.cFileName);
        int m = _wtoi(fd.cFileName + 5);
        int d = _wtoi(fd.cFileName + 8);
        if (y < 2000 || m < 1 || m > 12 || d < 1 || d > 31) continue;
        if (y * 10000 + m * 100 + d >= cutoff) continue;

        WCHAR sub[MAX_PATH];
        StringCchCopyW(sub, MAX_PATH, g_shotDir);
        StringCchCatW(sub, MAX_PATH, L"\\");
        StringCchCatW(sub, MAX_PATH, fd.cFileName);

        WCHAR inner[MAX_PATH];
        StringCchCopyW(inner, MAX_PATH, sub);
        StringCchCatW(inner, MAX_PATH, L"\\*");
        WIN32_FIND_DATAW f2;
        HANDLE h2 = FindFirstFileW(inner, &f2);
        if (h2 != INVALID_HANDLE_VALUE) {
            do {
                if (f2.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) continue;
                WCHAR fp[MAX_PATH];
                StringCchCopyW(fp, MAX_PATH, sub);
                StringCchCatW(fp, MAX_PATH, L"\\");
                StringCchCatW(fp, MAX_PATH, f2.cFileName);
                DeleteFileW(fp);
            } while (FindNextFileW(h2, &f2));
            FindClose(h2);
        }
        RemoveDirectoryW(sub);
    } while (FindNextFileW(h, &fd));
    FindClose(h);
}

/* ---------------- 命令行 ---------------- */
static void ParseCmdLine(WCHAR *configOverride, size_t n)
{
    WCHAR *cmd = GetCommandLineW();
    int argc = 0;
    LPWSTR *argv = CommandLineToArgvW(cmd, &argc);
    if (!argv) return;
    for (int i = 1; i < argc; i++) {
        if (!_wcsicmp(argv[i], L"--config") && i + 1 < argc) {
            StringCchCopyW(configOverride, n, argv[i + 1]);
            i++;
        }
    }
    LocalFree(argv);
}

int WINAPI WinMain(HINSTANCE hInstance, HINSTANCE hPrev, LPSTR lpCmdLine, int nShowCmd)
{
    (void)hPrev; (void)lpCmdLine; (void)nShowCmd;
    g_hInst = hInstance;

    SetProcessDPIAware();

    /* 单实例：已在运行时，让已运行实例把窗口显示出来（用于隐藏模式下家长重新打开设置） */
    HANDLE mutex = CreateMutexW(NULL, TRUE, L"Local\\ScreenTimeSingleInstance");
    if (mutex && GetLastError() == ERROR_ALREADY_EXISTS) {
        HWND other = FindWindowW(L"ScreenTimeStatus", NULL);
        if (other) PostMessageW(other, WM_SHOWREQ, 0, 0);
        else MessageBoxW(NULL, L"屏幕使用时间管理已在运行中。", L"ScreenTime",
                         MB_OK | MB_ICONINFORMATION);
        return 0;
    }

    WCHAR override[MAX_PATH] = L"";
    ParseCmdLine(override, MAX_PATH);

    ExeDirW(g_baseDir, MAX_PATH);
    if (override[0]) {
        StringCchCopyW(g_configPath, MAX_PATH, override);
    } else {
        StringCchCopyW(g_configPath, MAX_PATH, g_baseDir);
        StringCchCatW(g_configPath, MAX_PATH, L"\\config.ini");
        if (GetFileAttributesW(g_configPath) == INVALID_FILE_ATTRIBUTES)
            ConfigSaveDefault(&g_cfg, g_configPath);
    }

    ConfigLoad(&g_cfg, g_configPath);
    ConfigFinalizePassword(&g_cfg, g_configPath);

    if (g_cfg.block_hotkeys) HooksSetBlockHotkeys(1);
    else                     HooksSetBlockHotkeys(0);

    /* 目录：配置同级的 screenshots / logs */
    WCHAR cfgDir[MAX_PATH];
    StringCchCopyW(cfgDir, MAX_PATH, g_configPath);
    WCHAR *slash = wcsrchr(cfgDir, L'\\');
    if (slash) *slash = 0;

    StringCchCopyW(g_shotDir, MAX_PATH, cfgDir);
    StringCchCatW(g_shotDir, MAX_PATH, L"\\screenshots");
    StringCchCopyW(g_logDir, MAX_PATH, cfgDir);
    StringCchCatW(g_logDir, MAX_PATH, L"\\logs");
    EnsureDirW(g_shotDir);
    EnsureDirW(g_logDir);

    LogInit(g_logDir);
    LogEventW(L"ScreenTime 启动：限时 %d 分钟，休息 %d 分钟，截图间隔 %d 秒，模型 %s",
              g_cfg.session_minutes, g_cfg.break_minutes, g_cfg.screenshot_interval, g_cfg.model);

    CleanupOldShots();

    GdiplusStartupInput gdiIn;
    memset(&gdiIn, 0, sizeof(gdiIn));
    gdiIn.GdiplusVersion = 1;
    gdiIn.DebugEventCallback = NULL;
    gdiIn.SuppressBackgroundThread = FALSE;
    gdiIn.SuppressExternalCodecs = FALSE;
    GpStatus gstat = GdiplusStartup(&g_gdiToken, &gdiIn, NULL);
    if (gstat != Ok) {
        LogEventW(L"GDI+ 初始化失败（status=%d），截图功能不可用", (int)gstat);
    }

    HooksInstall();

    LONGLONG len = (LONGLONG)g_cfg.session_minutes * 60000;
    SessionReset(len, L"程序启动");

    if (!StatusWinCreate()) {
        LogEventW(L"状态窗口创建失败");
    } else if (g_cfg.start_hidden) {
        LogEventW(L"启动模式：界面隐藏（start_hidden=1，show_tray_icon=%d）",
                  g_cfg.show_tray_icon);
    }

    g_lastInputTick = (LONGLONG)GetTickCount64();

    if (g_cfg.vision_enabled) {
        g_visionThread = (HANDLE)_beginthreadex(NULL, 0, VisionThread, NULL, 0, NULL);
    }

    MSG msg;
    while (GetMessageW(&msg, NULL, 0, 0)) {
        TranslateMessage(&msg);
        DispatchMessageW(&msg);
    }

    InterlockedExchange(&g_stop, 1);
    if (g_visionThread) {
        WaitForSingleObject(g_visionThread, 3000);
        CloseHandle(g_visionThread);
    }

    HooksUninstall();
    LogEventW(L"ScreenTime 退出");
    LogClose();
    if (g_gdiToken) GdiplusShutdown(g_gdiToken);
    if (mutex) CloseHandle(mutex);
    return 0;
}
