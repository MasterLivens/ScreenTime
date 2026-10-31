#include "config.h"
#include "passwd.h"
#include "crypt.h"
#include "util.h"
#ifdef ST_PWDEBUG
#include "logger.h"
#endif
#include <strsafe.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

void ConfigDefaults(Config *c)
{
    memset(c, 0, sizeof(*c));
    c->session_minutes       = 45;
    c->break_minutes         = 15;
    c->password[0] = 0;      /* 明文密码只在内存里用于设置新密码 */
    c->pw_hash[0] = 0;
    c->pw_salt[0] = 0;
    c->pw_iterations         = ST_PW_DEFAULT_ITER;
    c->screenshot_enabled    = 1;   /* 定时截图总开关 */
    c->screenshot_interval   = 60;
    StringCchCopyW(c->api_url, 256, L"http://192.168.6.220:8177/v1");
    StringCchCopyW(c->model, 128, L"qwen3.8-flash-next-iq3_xxs");
    c->api_key[0] = 0;
    c->vision_enabled        = 1;
    c->idle_pause            = 1;
    c->idle_seconds          = 600;   /* 10 分钟：闲置超过它才算"人不在" */
    c->idle_reset            = 1;     /* 回来时重新开始计时 */
    c->video_detect          = 1;     /* 全屏观影时暂停计时且不锁屏 */
    c->penalty               = 1;
    c->warn_minutes          = 5;
    c->save_screenshots      = 1;
    c->max_image_width       = 1280;
    c->wrong_penalty_seconds = 30;
    c->block_hotkeys         = 1;
    c->keep_days             = 7;
    c->start_hidden          = 1;
    c->show_tray_icon        = 1;
}

static int AtoiDef(const WCHAR *v, int def)
{
    if (!v || !*v) return def;
    return (int)_wtoi(v);
}

static void SetKey(Config *c, const WCHAR *k, const WCHAR *v)
{
    if (!_wcsicmp(k, L"session_minutes"))       c->session_minutes = AtoiDef(v, c->session_minutes);
    else if (!_wcsicmp(k, L"break_minutes"))    c->break_minutes = AtoiDef(v, c->break_minutes);
    else if (!_wcsicmp(k, L"password"))         { if (v && *v) StringCchCopyW(c->password, 64, v); }
    else if (!_wcsicmp(k, L"password_hash"))    { if (v && *v) StringCchCopyW(c->pw_hash, ST_PW_HASH_HEX, v); }
    else if (!_wcsicmp(k, L"password_salt"))    { if (v && *v) StringCchCopyW(c->pw_salt, ST_PW_SALT_HEX, v); }
    else if (!_wcsicmp(k, L"pw_iterations"))    c->pw_iterations = AtoiDef(v, c->pw_iterations);
    else if (!_wcsicmp(k, L"screenshot_enabled")) c->screenshot_enabled = AtoiDef(v, c->screenshot_enabled);
    else if (!_wcsicmp(k, L"screenshot_interval")) c->screenshot_interval = AtoiDef(v, c->screenshot_interval);
    else if (!_wcsicmp(k, L"api_url"))          { if (v && *v) StringCchCopyW(c->api_url, 256, v); }
    else if (!_wcsicmp(k, L"model"))            { if (v && *v) StringCchCopyW(c->model, 128, v); }
    else if (!_wcsicmp(k, L"api_key"))          { StringCchCopyW(c->api_key, 128, v ? v : L""); }
    else if (!_wcsicmp(k, L"vision_enabled"))   c->vision_enabled = AtoiDef(v, c->vision_enabled);
    else if (!_wcsicmp(k, L"idle_pause"))       c->idle_pause = AtoiDef(v, c->idle_pause);
    else if (!_wcsicmp(k, L"idle_seconds"))     c->idle_seconds = AtoiDef(v, c->idle_seconds);
    else if (!_wcsicmp(k, L"idle_reset"))       c->idle_reset = AtoiDef(v, c->idle_reset);
    else if (!_wcsicmp(k, L"video_detect"))     c->video_detect = AtoiDef(v, c->video_detect);
    else if (!_wcsicmp(k, L"penalty"))          c->penalty = AtoiDef(v, c->penalty);
    else if (!_wcsicmp(k, L"warn_minutes"))     c->warn_minutes = AtoiDef(v, c->warn_minutes);
    else if (!_wcsicmp(k, L"save_screenshots")) c->save_screenshots = AtoiDef(v, c->save_screenshots);
    else if (!_wcsicmp(k, L"max_image_width"))  c->max_image_width = AtoiDef(v, c->max_image_width);
    else if (!_wcsicmp(k, L"wrong_penalty_seconds")) c->wrong_penalty_seconds = AtoiDef(v, c->wrong_penalty_seconds);
    else if (!_wcsicmp(k, L"block_hotkeys"))    c->block_hotkeys = AtoiDef(v, c->block_hotkeys);
    else if (!_wcsicmp(k, L"keep_days"))        c->keep_days = AtoiDef(v, c->keep_days);
    else if (!_wcsicmp(k, L"start_hidden"))     c->start_hidden = AtoiDef(v, c->start_hidden);
    else if (!_wcsicmp(k, L"show_tray_icon"))   c->show_tray_icon = AtoiDef(v, c->show_tray_icon);
}

static WCHAR *TrimW(WCHAR *s)
{
    while (*s == L' ' || *s == L'\t' || *s == L'\r' || *s == L'\n') s++;
    size_t n = wcslen(s);
    while (n > 0 && (s[n-1] == L' ' || s[n-1] == L'\t' || s[n-1] == L'\r' || s[n-1] == L'\n')) s[--n] = 0;
    return s;
}

/* 把内联注释（前面有空白的 # 或 ;）去掉，避免 "a#b" 这种值被误伤 */
static WCHAR *StripInlineComment(WCHAR *v)
{
    WCHAR *p = v;
    while (*p) {
        if ((*p == L'#' || *p == L';') && p > v && (p[-1] == L' ' || p[-1] == L'\t')) {
            *p = 0;
            break;
        }
        p++;
    }
    return TrimW(v);
}

static void ClampConfig(Config *c)
{
    if (c->session_minutes < 1)      c->session_minutes = 1;
    if (c->session_minutes > 1440)   c->session_minutes = 1440;
    if (c->break_minutes < 1)        c->break_minutes = 1;
    if (c->break_minutes > 1440)     c->break_minutes = 1440;
    if (c->screenshot_interval < 5)  c->screenshot_interval = 5;
    if (c->screenshot_interval > 3600) c->screenshot_interval = 3600;
    if (c->idle_seconds < 10)        c->idle_seconds = 10;
    if (c->idle_seconds > 7200)      c->idle_seconds = 7200;
    if (c->idle_reset)               c->idle_reset = 1; else c->idle_reset = 0;
    if (c->video_detect)             c->video_detect = 1; else c->video_detect = 0;
    if (c->warn_minutes < 0)         c->warn_minutes = 0;
    if (c->warn_minutes > 120)       c->warn_minutes = 120;
    if (c->max_image_width < 320)    c->max_image_width = 320;
    if (c->max_image_width > 3840)   c->max_image_width = 3840;
    if (c->wrong_penalty_seconds < 0) c->wrong_penalty_seconds = 0;
    if (c->wrong_penalty_seconds > 3600) c->wrong_penalty_seconds = 3600;
    if (c->keep_days < 0)            c->keep_days = 0;
    if (c->keep_days > 3650)         c->keep_days = 3650;
    if (c->pw_iterations < 1000)     c->pw_iterations = 1000;
    if (c->pw_iterations > 500000)   c->pw_iterations = 500000;
}

/* 解析 UTF-8 文本形式的配置（加密文件解密后的内容 / 老版 config.ini 都一样） */
static void ParseIniText(Config *c, const char *utf8, int len)
{
    WCHAR *wide = (WCHAR *)malloc(((size_t)len + 2) * sizeof(WCHAR));
    WCHAR *line;
    if (!wide) return;
    MultiByteToWideChar(CP_UTF8, 0, utf8, -1, wide, len + 2);

    line = wide;
    while (line && *line) {
        WCHAR *nl = wcschr(line, L'\n');
        WCHAR *s;
        if (nl) *nl = 0;
        s = TrimW(line);
        if (*s && *s != L';' && *s != L'#') {
            WCHAR *eq = wcschr(s, L'=');
            if (eq) {
                *eq = 0;
                SetKey(c, TrimW(s), StripInlineComment(eq + 1));
            }
        }
        line = nl ? nl + 1 : NULL;
    }
    free(wide);
}

/* 把路径的扩展名换成 .ini / .ini.bak（用于兼容与退休老版明文配置） */
static int SiblingPath(const WCHAR *path, const WCHAR *ext, WCHAR *out, int cap)
{
    WCHAR *dot;
    if (!path || !ext || !out || cap <= 0) return 0;
    StringCchCopyW(out, (size_t)cap, path);
    dot = wcsrchr(out, L'.');
    if (dot && !wcschr(dot, L'\\')) *dot = 0;
    StringCchCatW(out, (size_t)cap, ext);
    return 1;
}

/* ---------------- 读 ---------------- */

/* 返回：0=没有配置文件（用默认值） 1=读到了（加密） 2=读到了明文（需要重新加密保存） */
int ConfigLoad(Config *c, const WCHAR *path)
{
    HANDLE h;
    LARGE_INTEGER sz;
    char *raw = NULL;
    DWORD got = 0;
    int ret = 0;

    ConfigDefaults(c);

    h = CreateFileW(path, GENERIC_READ, FILE_SHARE_READ | FILE_SHARE_WRITE, NULL,
                    OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, NULL);

    if (h == INVALID_HANDLE_VALUE) {
        /* 兼容老版本：同目录同名的 config.ini（读完由调用方重新加密保存） */
        WCHAR ini[MAX_PATH];
        if (!SiblingPath(path, L".ini", ini, MAX_PATH)) return 0;
        if (GetFileAttributesW(ini) == INVALID_FILE_ATTRIBUTES) return 0;
        h = CreateFileW(ini, GENERIC_READ, FILE_SHARE_READ | FILE_SHARE_WRITE, NULL,
                        OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, NULL);
        if (h == INVALID_HANDLE_VALUE) return 0;
    }

    GetFileSizeEx(h, &sz);
    if (sz.QuadPart <= 0 || sz.QuadPart > (1 << 20)) { CloseHandle(h); return 0; }

    raw = (char *)malloc((size_t)sz.QuadPart + 4);
    if (!raw) { CloseHandle(h); return 0; }
    ReadFile(h, raw, (DWORD)sz.QuadPart, &got, NULL);
    CloseHandle(h);
    raw[got] = 0;

    if (CryptIsSealed(raw, (int)got)) {
        unsigned char *plain = NULL;
        int plainLen = 0;
        if (CryptOpen(raw, (int)got, &plain, &plainLen) && plainLen > 0) {
            ParseIniText(c, (const char *)plain, plainLen);
            ret = 1;
            SecureZeroMemory(plain, plainLen);
            free(plain);
        } else {
            ret = 0;                       /* 解不开：当没有配置，用默认值 */
        }
    } else {
        char *p = raw;
        if (got >= 3 && (unsigned char)raw[0] == 0xEF && (unsigned char)raw[1] == 0xBB &&
            (unsigned char)raw[2] == 0xBF) p += 3;
        ParseIniText(c, p, (int)strlen(p));
        ret = 2;                           /* 老版明文配置：让调用方重新加密保存 */
    }

    free(raw);
    ClampConfig(c);
    return ret;
}

/* ---------------- 写 ---------------- */

#define CFG_TEXT_CAP 8192

static int BuildIniText(const Config *c, char *out, size_t cap)
{
    char url[512], model[256], key[256], hash[128], salt[96];
    WToUtf8(c->api_url, url, (int)sizeof(url));
    WToUtf8(c->model, model, (int)sizeof(model));
    WToUtf8(c->api_key, key, (int)sizeof(key));
    WToUtf8(c->pw_hash, hash, (int)sizeof(hash));
    WToUtf8(c->pw_salt, salt, (int)sizeof(salt));

    return _snprintf_s(out, cap, _TRUNCATE,
        "; ScreenTime 配置（加密存储，请用程序里的「家长设置」界面修改）\n"
        "\n"
        "; ---- 时间 ----\n"
        "session_minutes = %d\n"
        "break_minutes = %d\n"
        "warn_minutes = %d\n"
        "idle_pause = %d\n"
        "idle_seconds = %d\n"
        "idle_reset = %d\n"
        "video_detect = %d\n"
        "penalty = %d\n"
        "\n"
        "; ---- 截图 ----\n"
        "screenshot_enabled = %d\n"
        "screenshot_interval = %d\n"
        "save_screenshots = %d\n"
        "vision_enabled = %d\n"
        "keep_days = %d\n"
        "max_image_width = %d\n"
        "api_url = %s\n"
        "model = %s\n"
        "api_key = %s\n"
        "\n"
        "; ---- 安全 / 界面 ----\n"
        "wrong_penalty_seconds = %d\n"
        "block_hotkeys = %d\n"
        "start_hidden = %d\n"
        "show_tray_icon = %d\n"
        "\n"
        "; ---- 密码（PBKDF2 哈希，由界面里的「修改密码」生成） ----\n"
        "pw_iterations = %d\n"
        "password_hash = %s\n"
        "password_salt = %s\n",
        c->session_minutes, c->break_minutes, c->warn_minutes,
        c->idle_pause, c->idle_seconds, c->idle_reset, c->video_detect, c->penalty,
        c->screenshot_enabled, c->screenshot_interval, c->save_screenshots,
        c->vision_enabled, c->keep_days, c->max_image_width,
        url, model, key,
        c->wrong_penalty_seconds, c->block_hotkeys, c->start_hidden, c->show_tray_icon,
        c->pw_iterations, hash, salt);
}

/* 写入文件并设隐藏属性（隐藏文件用 CREATE_ALWAYS + NORMAL 会被拒绝，要带上原属性） */
static int WriteAllHidden(const WCHAR *path, const void *data, DWORD len)
{
    DWORD attrs = GetFileAttributesW(path);
    HANDLE h = CreateFileW(path, GENERIC_WRITE, 0, NULL, CREATE_ALWAYS,
                           attrs != INVALID_FILE_ATTRIBUTES ? attrs : FILE_ATTRIBUTE_HIDDEN,
                           NULL);
    DWORD written = 0;
    if (h == INVALID_HANDLE_VALUE) {
        if (attrs != INVALID_FILE_ATTRIBUTES) SetFileAttributesW(path, FILE_ATTRIBUTE_NORMAL);
        h = CreateFileW(path, GENERIC_WRITE, 0, NULL, CREATE_ALWAYS,
                        FILE_ATTRIBUTE_HIDDEN, NULL);
        if (h == INVALID_HANDLE_VALUE) return 0;
    }
    if (!WriteFile(h, data, len, &written, NULL) || written != len) {
        CloseHandle(h);
        return 0;
    }
    CloseHandle(h);
    SetFileAttributesW(path, FILE_ATTRIBUTE_HIDDEN);
    return 1;
}

/* 把 dat 路径同目录下的老 config.ini 改名为 .ini.bak（保留一份但不再是生效配置） */
static void RetireLegacyIni(const WCHAR *datPath)
{
    WCHAR ini[MAX_PATH], bak[MAX_PATH];
    if (!datPath) return;
    if (!SiblingPath(datPath, L".ini", ini, MAX_PATH)) return;
    if (!SiblingPath(datPath, L".ini.bak", bak, MAX_PATH)) return;
    if (GetFileAttributesW(ini) == INVALID_FILE_ATTRIBUTES) return;
    SetFileAttributesW(ini, FILE_ATTRIBUTE_NORMAL);
    DeleteFileW(bak);
    MoveFileW(ini, bak);
}

int ConfigSave(Config *c, const WCHAR *path)
{
    char text[CFG_TEXT_CAP];
    unsigned char *blob = NULL;
    int blobLen = 0;
    int n, ok;

    if (!c || !path) return 0;
    n = BuildIniText(c, text, sizeof(text));
    if (n <= 0) return 0;

    if (!CryptSeal(text, n, &blob, &blobLen)) return 0;
    ok = WriteAllHidden(path, blob, (DWORD)blobLen);
    SecureZeroMemory(blob, blobLen);
    free(blob);
    SecureZeroMemory(text, sizeof(text));

    if (ok) {
        SecureZeroMemory(c->password, sizeof(c->password));  /* 明文密码不留在内存/文件里 */
        RetireLegacyIni(path);
    }
    return ok;
}

int ConfigSaveDefault(const Config *c, const WCHAR *path)
{
    Config tmp;
    if (c) tmp = *c;
    else ConfigDefaults(&tmp);
    return ConfigSave(&tmp, path);
}

int ConfigFinalizePassword(Config *c, const WCHAR *path)
{
    int needSave = 0;

    if (!c) return 0;

    /* 有新明文密码 -> 生成哈希 */
    if (c->password[0]) {
        unsigned char salt[16], out[32];
        WCHAR hexHash[ST_PW_HASH_HEX], hexSalt[ST_PW_SALT_HEX];
        if (!PwRandomSalt(salt, 16)) return 0;
        if (!PwDerive(c->password, salt, 16, c->pw_iterations, out)) return 0;
        PwHexEncode(out, 32, hexHash);
        PwHexEncode(salt, 16, hexSalt);
        StringCchCopyW(c->pw_hash, ST_PW_HASH_HEX, hexHash);
        StringCchCopyW(c->pw_salt, ST_PW_SALT_HEX, hexSalt);
        SecureZeroMemory(c->password, sizeof(c->password));
#ifdef ST_PWDEBUG
        LogEventW(L"[pwdbg] finalize: hash=%ls salt=%ls iter=%d",
                  c->pw_hash, c->pw_salt, c->pw_iterations);
#endif
        needSave = 1;
    } else if (!c->pw_hash[0] || !c->pw_salt[0]) {
        /* 首次运行还没有哈希：用默认密码 12345678 */
        StringCchCopyW(c->password, 64, L"12345678");
        return ConfigFinalizePassword(c, path);
    }

    if (needSave) {
        if (!ConfigSave(c, path)) return 0;
    } else {
        c->password[0] = 0;
    }
    return needSave;
}
