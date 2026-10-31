/* ============================================================================
 *  媒体/视频检测（不截图）
 *
 *  信号：
 *    A) WASAPI 音频会话：有 Active 会话且峰值 > 0  -> "有声音在播"
 *    B) PDH GPU 计数器：GPU Engine(*engtype_VideoDecode) 总占用 > 0.5%
 *                       -> "有进程在用硬件解码视频"（静音视频也能抓到）
 *    C) 前景窗口是否铺满所在显示器 -> "全屏"
 *
 *  判定：
 *    video        = A || B                     有视频/媒体在播（窗口化或全屏）
 *    fullscreen   = C && video                 观影模式（全屏且在播）
 *
 *  带迟滞：fullscreen 连续 2 次命中才置 1（避免切窗口抖动），
 *          连续 3 次不命中才清 0（片尾/切集时不至于反复进出）。
 *
 *  采集放在独立线程，每秒一轮；主线程只读全局变量，不会被阻塞。
 * ========================================================================== */

#include <windows.h>
#include <mmdeviceapi.h>
#include <audiopolicy.h>
#include <endpointvolume.h>
#include <pdh.h>
#include <pdhmsg.h>
#include <shellapi.h>
#include <strsafe.h>
#include <process.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "media.h"

/* ---------------------------------------------------------------------------
 * MinGW 的 endpointvolume.h 只前向声明了 IAudioMeterInformation，没有 vtable 定义，
 * 也没有它的 IID，所以这里按 Windows SDK 的定义补一份（只用到 GetPeakValue）。
 * ------------------------------------------------------------------------- */
/* ---------------------------------------------------------------------------
 * 这几个 GUID MinGW 的头文件有声明、但 libuuid 里没有定义，所以自带一份。
 * ------------------------------------------------------------------------- */
static const GUID ST_CLSID_MMDeviceEnumerator =
    { 0xbcde0395, 0xe52f, 0x467c, { 0x8e, 0x3d, 0xc4, 0x57, 0x92, 0x91, 0x69, 0x2e } };
static const GUID ST_IID_IMMDeviceEnumerator =
    { 0xa95664d2, 0x9614, 0x4f35, { 0xa7, 0x46, 0xde, 0x8d, 0xb6, 0x36, 0x17, 0xe6 } };
static const GUID ST_IID_IAudioSessionManager2 =
    { 0x77aa99a0, 0x1bd6, 0x484f, { 0x8b, 0xc7, 0x2c, 0x65, 0x4c, 0x9a, 0x9b, 0x6f } };
static const GUID ST_IID_IAudioMeterInformation =
    { 0xc02216f6, 0x8c67, 0x4b5b, { 0x9d, 0x00, 0xd0, 0x08, 0xe7, 0x3e, 0x00, 0x64 } };

typedef struct ST_IAudioMeterInformationVtbl ST_IAudioMeterInformationVtbl;
typedef struct ST_IAudioMeterInformation {
    ST_IAudioMeterInformationVtbl *lpVtbl;
} ST_IAudioMeterInformation;

struct ST_IAudioMeterInformationVtbl {
    HRESULT (STDMETHODCALLTYPE *QueryInterface)(ST_IAudioMeterInformation *This, const IID *const riid, void **ppvObject);
    ULONG   (STDMETHODCALLTYPE *AddRef)(ST_IAudioMeterInformation *This);
    ULONG   (STDMETHODCALLTYPE *Release)(ST_IAudioMeterInformation *This);
    HRESULT (STDMETHODCALLTYPE *GetPeakValue)(ST_IAudioMeterInformation *This, float *pfPeak);
    HRESULT (STDMETHODCALLTYPE *GetMeteringChannelCount)(ST_IAudioMeterInformation *This, UINT *pnChannelCount);
    HRESULT (STDMETHODCALLTYPE *GetChannelsPeakValues)(ST_IAudioMeterInformation *This, UINT32 u32ChannelCount, float *afPeakValues);
    HRESULT (STDMETHODCALLTYPE *QueryHardwareSupport)(ST_IAudioMeterInformation *This, DWORD *pdwHardwareSupportMask);
};

/* ---------------- 对外的状态 ---------------- */

static volatile LONG g_videoActive = 0;   /* 音频或硬解，或模式2下的全屏 */
static volatile LONG g_fullscreen = 0;    /* 前景窗口全屏 */
static volatile LONG g_signals = 0;       /* 音频或硬解（真实信号） */
static volatile LONG g_fullscreenVideo = 0; /* = fullscreen && signals，仅作展示 */
static volatile LONG g_mode = 1;      /* 1=音频/硬解+全屏  2=只看全屏 */
static WCHAR g_detail[192] = L"(未采集)";
static CRITICAL_SECTION g_detailCs;
static int g_detailCsReady = 0;

int MediaVideoActive(void) { return (int)InterlockedCompareExchange(&g_videoActive, 0, 0); }
int MediaFullscreen(void) { return (int)InterlockedCompareExchange(&g_fullscreen, 0, 0); }
int MediaVideoSignals(void) { return (int)InterlockedCompareExchange(&g_signals, 0, 0); }
int MediaFullscreenVideo(void) { return (int)InterlockedCompareExchange(&g_fullscreenVideo, 0, 0); }

void MediaSetMode(int mode) { InterlockedExchange(&g_mode, (LONG)mode); }
int  MediaMode(void) { return (int)InterlockedCompareExchange(&g_mode, 0, 0); }

const WCHAR *MediaLastDetail(void) { return g_detail; }

static void SetDetail(const WCHAR *text)
{
    if (!g_detailCsReady) return;
    EnterCriticalSection(&g_detailCs);
    StringCchCopyW(g_detail, 192, text ? text : L"");
    LeaveCriticalSection(&g_detailCs);
}

/* ---------------- A) WASAPI 音频会话 ---------------- */

/* 有 Active 会话且峰值超过阈值就返回 1；同时把"会话数/峰值"写进 detail */
static int AudioPlaying(WCHAR *detail, int cap)
{
    IMMDeviceEnumerator *en = NULL;
    IMMDevice *dev = NULL;
    IAudioSessionManager2 *mgr = NULL;
    IAudioSessionEnumerator *sess = NULL;
    int count = 0, i, playing = 0;
    float maxPeak = 0.0f;
    int activeCount = 0;

    detail[0] = 0;

    if (FAILED(CoCreateInstance(&ST_CLSID_MMDeviceEnumerator, NULL, CLSCTX_ALL,
                               &ST_IID_IMMDeviceEnumerator, (void **)&en)) || en == NULL) {
        StringCchCopyW(detail, cap, L"音频:枚举器不可用");
        return 0;
    }
    if (FAILED(en->lpVtbl->GetDefaultAudioEndpoint(en, eRender, eConsole, &dev)) || dev == NULL) {
        StringCchCopyW(detail, cap, L"音频:没有默认输出设备");
        en->lpVtbl->Release(en);
        return 0;
    }
    if (FAILED(dev->lpVtbl->Activate(dev, &ST_IID_IAudioSessionManager2, CLSCTX_ALL,
                                     NULL, (void **)&mgr)) || mgr == NULL) {
        StringCchCopyW(detail, cap, L"音频:拿不到会话管理器");
        goto done;
    }
    if (FAILED(mgr->lpVtbl->GetSessionEnumerator(mgr, &sess)) || sess == NULL) {
        StringCchCopyW(detail, cap, L"音频:枚举会话失败");
        goto done;
    }
    if (FAILED(sess->lpVtbl->GetCount(sess, &count))) {
        StringCchCopyW(detail, cap, L"音频:会话计数失败");
        goto done;
    }

    for (i = 0; i < count; i++) {
        IAudioSessionControl *ctl = NULL;
        ST_IAudioMeterInformation *meter = NULL;
        AudioSessionState st = AudioSessionStateInactive;

        if (FAILED(sess->lpVtbl->GetSession(sess, i, &ctl)) || ctl == NULL) continue;
        if (FAILED(ctl->lpVtbl->GetState(ctl, &st))) { ctl->lpVtbl->Release(ctl); continue; }

        if (st == AudioSessionStateActive) {
            activeCount++;
            if (SUCCEEDED(ctl->lpVtbl->QueryInterface(ctl, &ST_IID_IAudioMeterInformation,
                                                      (void **)&meter)) && meter != NULL) {
                float peak = 0.0f;
                if (SUCCEEDED(meter->lpVtbl->GetPeakValue(meter, &peak))) {
                    if (peak > maxPeak) maxPeak = peak;
                    if (peak > 0.005f) playing = 1;
                }
                meter->lpVtbl->Release(meter);
            }
        }
        ctl->lpVtbl->Release(ctl);
    }

    StringCchPrintfW(detail, cap, L"音频:%d 活动会话, 峰值%.3f", activeCount, (double)maxPeak);

done:
    if (sess) sess->lpVtbl->Release(sess);
    if (mgr) mgr->lpVtbl->Release(mgr);
    if (dev) dev->lpVtbl->Release(dev);
    if (en) en->lpVtbl->Release(en);
    return playing;
}

/* ---------------- B) GPU 视频解码计数器 ---------------- */

static PDH_HQUERY g_pdhQuery = NULL;
static PDH_HCOUNTER g_pdhVideo = NULL;
static int g_pdhReady = 0;

static void PdhInitOnce(void)
{
    if (g_pdhQuery != NULL) return;
    if (PdhOpenQueryW(NULL, 0, &g_pdhQuery) != ERROR_SUCCESS) {
        g_pdhQuery = NULL;
        return;
    }
    if (PdhAddEnglishCounterW(g_pdhQuery, L"\\GPU Engine(*engtype_VideoDecode)\\Utilization Percentage",
                              0, &g_pdhVideo) != ERROR_SUCCESS) {
        PdhCloseQuery(g_pdhQuery);
        g_pdhQuery = NULL;
        g_pdhVideo = NULL;
        return;
    }
    g_pdhReady = 1;
}

/* 返回 1 = 有进程在硬解视频；detail 里写总占用 */
static int GpuVideoDecoding(WCHAR *detail, int cap)
{
    PDH_STATUS st;
    DWORD size = 0, count = 0, i;
    PDH_FMT_COUNTERVALUE_ITEM_W *items = NULL;
    double total = 0.0;
    int active = 0;

    detail[0] = 0;
    PdhInitOnce();
    if (!g_pdhReady) {
        StringCchCopyW(detail, cap, L"硬解:计数器不可用");
        return 0;
    }

    if (PdhCollectQueryData(g_pdhQuery) != ERROR_SUCCESS) {
        StringCchCopyW(detail, cap, L"硬解:采集失败");
        return 0;
    }

    st = PdhGetFormattedCounterArrayW(g_pdhVideo, PDH_FMT_DOUBLE, &size, &count, NULL);
    if ((DWORD)st != (DWORD)PDH_MORE_DATA || size == 0) {
        StringCchPrintfW(detail, cap, L"硬解:0");
        return 0;
    }
    items = (PDH_FMT_COUNTERVALUE_ITEM_W *)malloc(size);
    if (items == NULL) {
        StringCchCopyW(detail, cap, L"硬解:内存不足");
        return 0;
    }
    if (PdhGetFormattedCounterArrayW(g_pdhVideo, PDH_FMT_DOUBLE, &size, &count, items) == ERROR_SUCCESS) {
        for (i = 0; i < count; i++) {
            if (items[i].FmtValue.CStatus == ERROR_SUCCESS) {
                total += items[i].FmtValue.doubleValue;
            }
        }
        /* 实例名里带 pid，可以直接看出是哪个进程在解码 */
        active = (total > 0.5);
    }
    free(items);

    StringCchPrintfW(detail, cap, L"硬解:%.1f%%", total);
    return active;
}

/* ---------------- C) 全屏判定 ---------------- */

static int IsFullscreenWindow(HWND h)
{
    RECT wr;
    HMONITOR mon;
    MONITORINFO mi;

    if (h == NULL || !IsWindowVisible(h) || IsIconic(h)) return 0;
    if (!GetWindowRect(h, &wr)) return 0;

    mon = MonitorFromWindow(h, MONITOR_DEFAULTTONEAREST);
    if (mon == NULL) return 0;
    mi.cbSize = sizeof(mi);
    if (!GetMonitorInfoW(mon, &mi)) return 0;

    return (wr.left <= mi.rcMonitor.left && wr.top <= mi.rcMonitor.top &&
            wr.right >= mi.rcMonitor.right && wr.bottom >= mi.rcMonitor.bottom);
}

/* 前景窗口是否全屏；另外把 SHQueryUserNotificationState 的结论当作补充信号
   （D3D 独占全屏时窗口矩形不一定等于屏，状态查询能补上） */
static int ForegroundIsFullscreen(void)
{
    HWND fg = GetForegroundWindow();
    QUERY_USER_NOTIFICATION_STATE quns = QUNS_ACCEPTS_NOTIFICATIONS;

    if (IsFullscreenWindow(fg)) return 1;

    if (SUCCEEDED(SHQueryUserNotificationState(&quns))) {
        if (quns == QUNS_RUNNING_D3D_FULL_SCREEN) return 1;
    }
    return 0;
}

/* ---------------- 采集线程 ---------------- */

static HANDLE g_thread = NULL;
static volatile LONG g_stop = 0;
static int g_force = 0;      /* 调试用：0=不干预 1=假全屏视频 2=假窗口化视频 3=强制关闭 */
static WCHAR g_forceFile[MAX_PATH] = L"";  /* 调试用：运行中可以改的文件开关 */

/* 调试开关（仅自动化测试用，正常使用不要设置）：
 *   ST_FAKE_MEDIA=1|fullscreen 假装全屏放视频；=video 只有视频；=0|off 强制判定为没有
 *   ST_FAKE_MEDIA_FILE=<路径>  每轮读这个文件，内容同上；文件删除即恢复真实检测
 *                              （用它可以测"进入观影模式 -> 退出 -> 恢复计时"整条链路） */
static void ReadForceEnv(void)
{
    WCHAR v[32];
    DWORD n = GetEnvironmentVariableW(L"ST_FAKE_MEDIA", v, 32);
    DWORD m;
    g_force = 0;
    if (n > 0) {
        if (!_wcsicmp(v, L"1") || !_wcsicmp(v, L"fullscreen")) g_force = 1;
        else if (!_wcsicmp(v, L"video")) g_force = 2;
        else if (!_wcsicmp(v, L"0") || !_wcsicmp(v, L"off")) g_force = 3;
    }
    m = GetEnvironmentVariableW(L"ST_FAKE_MEDIA_FILE", g_forceFile, MAX_PATH);
    if (m == 0 || m >= MAX_PATH) g_forceFile[0] = 0;
}

static int ParseForceWord(const char *w)
{
    char tmp[32];
    int i = 0;

    if (w == NULL) return 0;
    /* 文件里通常带换行，截到第一个空白 */
    while (w[i] != 0 && i < (int)sizeof(tmp) - 1) {
        if (w[i] == ' ' || w[i] == '\t' || w[i] == '\r' || w[i] == '\n') break;
        tmp[i] = w[i];
        i++;
    }
    tmp[i] = 0;

    if (!_stricmp(tmp, "1") || !_strnicmp(tmp, "full", 4)) return 1;
    if (!_strnicmp(tmp, "video", 5)) return 2;
    if (!_stricmp(tmp, "0") || !_strnicmp(tmp, "off", 3)) return 3;
    return 0;
}

static int ForceFromFile(void)
{
    HANDLE h;
    char buf[32];
    DWORD got = 0;

    if (g_forceFile[0] == 0) return 0;
    h = CreateFileW(g_forceFile, GENERIC_READ, FILE_SHARE_READ | FILE_SHARE_WRITE,
                    NULL, OPEN_EXISTING, 0, NULL);
    if (h == INVALID_HANDLE_VALUE) return 0;
    memset(buf, 0, sizeof(buf));
    ReadFile(h, buf, (DWORD)sizeof(buf) - 1, &got, NULL);
    CloseHandle(h);
    return ParseForceWord(buf);
}

static unsigned int __stdcall MediaThread(void *arg)
{
    int comInited = 0;

    (void)arg;

    if (FAILED(CoInitializeEx(NULL, COINIT_MULTITHREADED))) {
        /* 拿不到 COM 就只靠全屏判定 */
        comInited = 0;
    } else {
        comInited = 1;
    }

    while (!g_stop) {
        WCHAR dAudio[96], dGpu[96], dAll[192];
        int audio, gpu, fullscreen, video;

        dAudio[0] = 0;
        dGpu[0] = 0;
        audio = AudioPlaying(dAudio, 96);
        gpu = GpuVideoDecoding(dGpu, 96);
        fullscreen = ForegroundIsFullscreen();

        video = (audio || gpu) ? 1 : 0;

        /* 模式 2：只看全屏，完全不依赖音频与 GPU（给没有显卡/声卡的虚拟机用）。
           全屏时把 video 也置 1，这样"算作有活动"的逻辑一致，主线程再判观影。 */
        if (MediaMode() == 2) video = fullscreen ? 1 : 0;

        {
            int force = ForceFromFile();
            if (force == 0) force = g_force;
            if (force == 1) { video = 1; fullscreen = 1; }
            else if (force == 2) { video = 1; fullscreen = 0; }
            else if (force == 3) { video = 0; fullscreen = 0; }
        }

        /* 对外只暴露原始事实：全屏与否、有没有视频信号（音频/硬解）。
           是否进入"观影模式"由主线程结合"最近有没有键鼠输入"来判断
           —— 游戏一直在按键，永远不满足安静条件，所以不会被当成观影。 */
        InterlockedExchange(&g_signals,
                            (MediaMode() == 2) ? (fullscreen ? 1 : 0) : ((audio || gpu) ? 1 : 0));
        InterlockedExchange(&g_fullscreen, fullscreen ? 1 : 0);
        InterlockedExchange(&g_videoActive, video);
        InterlockedExchange(&g_fullscreenVideo,
                            (fullscreen && (MediaMode() == 2 ? 1 : (audio || gpu))) ? 1 : 0);

        StringCchPrintfW(dAll, 192, L"%s, %s, 全屏:%d, 信号:%d, 模式:%d%s",
                         dAudio, dGpu, fullscreen ? 1 : 0,
                         MediaVideoSignals(),
                         MediaMode(),
                         (g_force || ForceFromFile()) ? L"（调试强制）" : L"");
        SetDetail(dAll);

        for (int i = 0; i < 20 && !g_stop; i++) Sleep(50);   /* ~1 秒 */
    }

    if (comInited) CoUninitialize();
    return 0;
}

void MediaStart(void)
{
    if (!g_detailCsReady) {
        InitializeCriticalSection(&g_detailCs);
        g_detailCsReady = 1;
    }
    if (g_thread != NULL) return;
    ReadForceEnv();
    InterlockedExchange(&g_stop, 0);
    g_thread = (HANDLE)_beginthreadex(NULL, 0, MediaThread, NULL, 0, NULL);
}

void MediaStop(void)
{
    if (g_thread == NULL) return;
    InterlockedExchange(&g_stop, 1);
    WaitForSingleObject(g_thread, 3000);
    CloseHandle(g_thread);
    g_thread = NULL;
}
