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
#include "ui.h"

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

/* ---------------- 启动阶段跟踪 + 崩溃兜底 ---------------- */
const WCHAR *g_initStage = L"进程启动";

void SetInitStage(const WCHAR *stage)
{
    g_initStage = stage;
    LogEventW(L"阶段：%s", stage);
}

/* 未处理异常：把异常码、出错地址、所属模块和当前阶段写进日志再让系统继续处理。
   远程机器上"启动了但看不到进程"时，靠这一行就能知道是崩在哪一步、崩在哪个 DLL。 */
/* 系统版本：GetVersionEx 会被"兼容性垫片"骗成 6.2，改用 ntdll!RtlGetVersion 取真实版本 */
static void LogOsVersion(void)
{
    typedef LONG (WINAPI *RtlGetVersionFn)(PRTL_OSVERSIONINFOW);
    RTL_OSVERSIONINFOW vi;
    BOOL got = FALSE;

    ZeroMemory(&vi, sizeof(vi));
    vi.dwOSVersionInfoSize = sizeof(vi);
    {
        HMODULE ntdll = GetModuleHandleW(L"ntdll.dll");
        if (ntdll) {
            RtlGetVersionFn fn = (RtlGetVersionFn)(void *)GetProcAddress(ntdll, "RtlGetVersion");
            if (fn && fn(&vi) == 0) got = TRUE;
        }
    }
    if (!got) {
        OSVERSIONINFOW os;
        ZeroMemory(&os, sizeof(os));
        os.dwOSVersionInfoSize = sizeof(os);
        if (GetVersionExW(&os)) {
            vi.dwMajorVersion = os.dwMajorVersion;
            vi.dwMinorVersion = os.dwMinorVersion;
            vi.dwBuildNumber = os.dwBuildNumber;
        }
    }
    LogEventW(L"系统：Windows %lu.%lu.%lu（%d 位程序）",
              (unsigned long)vi.dwMajorVersion, (unsigned long)vi.dwMinorVersion,
              (unsigned long)vi.dwBuildNumber, (int)(sizeof(void *) * 8));
}

static LONG WINAPI OnUnhandledException(EXCEPTION_POINTERS *ep)
{
    DWORD code = (ep && ep->ExceptionRecord) ? ep->ExceptionRecord->ExceptionCode : 0;
    void *addr = (ep && ep->ExceptionRecord) ? ep->ExceptionRecord->ExceptionAddress : NULL;
    WCHAR modPath[MAX_PATH];
    HMODULE hm = NULL;
    StringCchCopyW(modPath, MAX_PATH, L"?");
    if (addr && GetModuleHandleExW(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS |
                                   GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,
                                   (LPCWSTR)addr, &hm) && hm) {
        GetModuleFileNameW(hm, modPath, MAX_PATH);
    }
    LogEventW(L"！！！程序异常退出：code=0x%08X 地址=%p 模块=%s 阶段=%s",
              (unsigned)code, addr, modPath, g_initStage ? g_initStage : L"?");
    return EXCEPTION_CONTINUE_SEARCH;
}

HINSTANCE g_hInst = NULL;

static ULONG_PTR g_gdiToken = 0;
static HANDLE    g_visionThread = NULL;
static volatile LONG g_stop = 0;
static int       g_cfgLoadSrc = 0;   /* ConfigLoad 的返回码，用于启动日志 */

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
        int wantCapture, wantModel;
        if (interval < 5) interval = 5;
        for (int i = 0; i < interval * 2 && !g_stop; i++)
            Sleep(500);
        if (g_stop) break;

        /* 截图总开关；只留截图不识别、或只识别不留图，都算"要截图" */
        wantModel   = g_cfg.vision_enabled ? 1 : 0;
        wantCapture = g_cfg.screenshot_enabled ? 1 : 0;
        if (!wantCapture) continue;                       /* 完全关掉：不截、不识别、不落盘 */
        if (!wantModel && !g_cfg.save_screenshots) continue; /* 截了也没用 */
        if (g_locked) continue;                           /* 锁屏期间不截 */

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

        WCHAR rel[MAX_PATH];
        StringCchCopyW(rel, MAX_PATH, L"screenshots\\");
        StringCchCatW(rel, MAX_PATH, day);
        StringCchCatW(rel, MAX_PATH, L"\\");
        StringCchCatW(rel, MAX_PATH, tm);
        StringCchCatW(rel, MAX_PATH, L".png");

        if (wantModel) {
            VisionResult res;
            VisionAnalyze(&g_cfg, file, &res);
            StringCchCopyW(g_lastAnswer, 1024, res.answer);
            LogVisionW(rel, res.answer, res.ms, res.ok);
        } else {
            LogEventW(L"截图 %ls（未送模型识别）", rel);
        }

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
        StringCchCatW(g_configPath, MAX_PATH, L"\\config.dat");
    }

    /* 配置：加密文件 config.dat 优先；读到老版明文 config.ini 时立刻迁移成加密存储 */
    {
        int src = ConfigLoad(&g_cfg, g_configPath);
        g_cfgLoadSrc = src;
        if (src == 2) ConfigSave(&g_cfg, g_configPath);
    }
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
    SetUnhandledExceptionFilter(OnUnhandledException);
    {
        WCHAR exePath[MAX_PATH] = L"?";
        GetModuleFileNameW(NULL, exePath, MAX_PATH);
        LogEventW(L"ScreenTime %s 启动：%s", ST_VERSION, exePath);
        LogOsVersion();
    }
    SetInitStage(L"配置已加载");
    LogEventW(L"ScreenTime 启动：限时 %d 分钟，休息 %d 分钟，截图 %s（间隔 %d 秒，%s），模型 %s",
              g_cfg.session_minutes, g_cfg.break_minutes,
              g_cfg.screenshot_enabled ? L"开" : L"关",
              g_cfg.screenshot_interval,
              g_cfg.save_screenshots ? L"留存图片" : L"不存图片",
              g_cfg.model);
    if (g_cfg.screenshot_enabled && !g_cfg.vision_enabled)
        LogEventW(L"提示：截图已开启但未开启模型识别，只会本地存图");
    LogEventW(L"配置：%s", g_configPath);
    if (g_cfgLoadSrc == 2)
        LogEventW(L"已把旧版明文 config.ini 迁移成加密配置（原文件改名为 config.ini.bak）");
    else if (g_cfgLoadSrc == 1)
        LogEventW(L"已读取加密配置（AES-256-CBC）");
    else
        LogEventW(L"没有找到配置，已用默认值生成加密配置");

    CleanupOldShots();

    SetInitStage(L"初始化 GDI+");
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

    SetInitStage(L"安装键盘/鼠标钩子");
    HooksInstall();

    LONGLONG len = (LONGLONG)g_cfg.session_minutes * 60000;
    SessionReset(len, L"程序启动");

    SetInitStage(L"开始初始化界面（AWTK）");
    if (!UiInit()) {
        LogEventW(L"界面初始化失败：进程无法继续（见上面的阶段/AWTK 日志）");
        /* 在别的机器上"启动了但看不到进程"时，至少弹一个能读的提示 */
        MessageBoxW(NULL,
                    L"界面初始化失败：无法创建窗口。\n\n"
                    L"常见原因：\n"
                    L"  1) 显卡驱动不支持 OpenGL 3（虚拟机/远程桌面/旧驱动常见）；\n"
                    L"  2) 安装目录下缺少 res 界面资源（应重装或重新解压绿色包）。\n\n"
                    L"请把安装目录下 logs\\activity.log 的最后几十行发给开发者。",
                    L"屏幕使用时间管理", MB_ICONERROR | MB_OK);
        return 1;
    } else if (g_cfg.start_hidden) {
        LogEventW(L"启动模式：界面隐藏（start_hidden=1，show_tray_icon=%d）",
                  g_cfg.show_tray_icon);
    }
    SetInitStage(L"界面就绪，进入主循环");

    g_lastInputTick = (LONGLONG)GetTickCount64();

    /* 线程常驻：内部每轮重新读 g_cfg 的开关，所以界面上改了开关立刻生效 */
    g_visionThread = (HANDLE)_beginthreadex(NULL, 0, VisionThread, NULL, 0, NULL);

    UiRun();

    InterlockedExchange(&g_stop, 1);
    if (g_visionThread) {
        WaitForSingleObject(g_visionThread, 3000);
        CloseHandle(g_visionThread);
    }

    UiShutdown();
    HooksUninstall();
    LogEventW(L"ScreenTime 退出");
    LogClose();
    if (g_gdiToken) GdiplusShutdown(g_gdiToken);
    if (mutex) CloseHandle(mutex);
    return 0;
}
