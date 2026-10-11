#include "logger.h"
#include "util.h"
#include <strsafe.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static WCHAR   g_logDir[MAX_PATH];
static CRITICAL_SECTION g_cs;
static int     g_inited = 0;

void LogInit(const WCHAR *logDir)
{
    InitializeCriticalSection(&g_cs);
    StringCchCopyW(g_logDir, MAX_PATH, logDir);
    EnsureDirW(logDir);
    g_inited = 1;

    HANDLE h = CreateFileW(logDir, FILE_READ_ATTRIBUTES, FILE_SHARE_READ, NULL,
                           OPEN_EXISTING, FILE_FLAG_BACKUP_SEMANTICS, NULL);
    if (h != INVALID_HANDLE_VALUE) CloseHandle(h);
}

void LogClose(void)
{
    if (g_inited) { DeleteCriticalSection(&g_cs); g_inited = 0; }
}

static void AppendUtf8(const WCHAR *path, const char *utf8, size_t len)
{
    HANDLE h = CreateFileW(path, FILE_APPEND_DATA, FILE_SHARE_READ, NULL,
                           OPEN_ALWAYS, FILE_ATTRIBUTE_NORMAL, NULL);
    if (h == INVALID_HANDLE_VALUE) return;
    SetFilePointer(h, 0, NULL, FILE_END);
    DWORD written = 0;
    WriteFile(h, utf8, (DWORD)len, &written, NULL);
    CloseHandle(h);
}

void LogEventW(const WCHAR *fmt, ...)
{
    if (!g_inited) return;

    WCHAR msg[1024];
    va_list ap;
    va_start(ap, fmt);
    StringCchVPrintfW(msg, 1024, fmt, ap);
    va_end(ap);

    WCHAR stamp[64];
    NowStampW(stamp, 64);

    WCHAR line[1200];
    StringCchPrintfW(line, 1200, L"[%s] %s\r\n", stamp, msg);

    char u8[3600];
    WToUtf8(line, u8, (int)sizeof(u8));

    WCHAR path[MAX_PATH];
    StringCchCopyW(path, MAX_PATH, g_logDir);
    StringCchCatW(path, MAX_PATH, L"\\activity.log");

    EnterCriticalSection(&g_cs);
    AppendUtf8(path, u8, strlen(u8));

    /* JSONL 版本 */
    char stampA[192];
    WToUtf8(stamp, stampA, (int)sizeof(stampA));
    char esc[3600];
    JsonEscapeA(u8, esc, sizeof(esc));
    char json[4200];
    sprintf(json, "{\"time\":\"%s\",\"event\":\"log\",\"text\":\"%s\"}\n", stampA, esc);
    StringCchCopyW(path, MAX_PATH, g_logDir);
    StringCchCatW(path, MAX_PATH, L"\\activity.jsonl");
    AppendUtf8(path, json, strlen(json));
    LeaveCriticalSection(&g_cs);
}

void LogVisionW(const WCHAR *file, const WCHAR *answer, LONGLONG ms, int ok)
{
    if (!g_inited) return;

    WCHAR stamp[64];
    NowStampW(stamp, 64);

    WCHAR line[1400];
    StringCchPrintfW(line, 1400, L"[%s] 识别 %s -> %s (%lld ms)\r\n",
                     stamp, file ? file : L"?", answer && *answer ? answer : L"(无结果)", ms);

    char u8[4000];
    WToUtf8(line, u8, (int)sizeof(u8));

    WCHAR path[MAX_PATH];
    StringCchCopyW(path, MAX_PATH, g_logDir);
    StringCchCatW(path, MAX_PATH, L"\\activity.log");

    EnterCriticalSection(&g_cs);
    AppendUtf8(path, u8, strlen(u8));

    char escFile[2000], escAns[4000];
    char fileU8[2000], ansU8[4000];
    WToUtf8(file ? file : L"", fileU8, (int)sizeof(fileU8));
    WToUtf8(answer ? answer : L"", ansU8, (int)sizeof(ansU8));
    JsonEscapeA(fileU8, escFile, sizeof(escFile));
    JsonEscapeA(ansU8, escAns, sizeof(escAns));

    char stampA[192];
    WToUtf8(stamp, stampA, (int)sizeof(stampA));

    char json[7000];
    sprintf(json, "{\"time\":\"%s\",\"event\":\"vision\",\"ok\":%d,\"ms\":%lld,\"file\":\"%s\",\"answer\":\"%s\"}\n",
            stampA, ok ? 1 : 0, ms, escFile, escAns);

    StringCchCopyW(path, MAX_PATH, g_logDir);
    StringCchCatW(path, MAX_PATH, L"\\activity.jsonl");
    AppendUtf8(path, json, strlen(json));
    LeaveCriticalSection(&g_cs);
}
