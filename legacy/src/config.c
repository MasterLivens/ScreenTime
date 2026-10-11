#include "config.h"
#include "passwd.h"
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
    c->password[0] = 0;      /* 明文密码默认为空：只有配置文件里写了才使用，避免改过的密码被重置 */
    c->pw_hash[0] = 0;
    c->pw_salt[0] = 0;
    c->pw_iterations         = ST_PW_DEFAULT_ITER;
    c->screenshot_interval   = 60;
    StringCchCopyW(c->api_url, 256, L"http://192.168.6.220:8177/v1");
    StringCchCopyW(c->model, 128, L"qwen3.8-flash-next-iq3_xxs");
    c->api_key[0] = 0;
    c->vision_enabled        = 1;
    c->idle_pause            = 1;
    c->idle_seconds          = 120;
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
    else if (!_wcsicmp(k, L"screenshot_interval")) c->screenshot_interval = AtoiDef(v, c->screenshot_interval);
    else if (!_wcsicmp(k, L"api_url"))          { if (v && *v) StringCchCopyW(c->api_url, 256, v); }
    else if (!_wcsicmp(k, L"model"))            { if (v && *v) StringCchCopyW(c->model, 128, v); }
    else if (!_wcsicmp(k, L"api_key"))          { StringCchCopyW(c->api_key, 128, v ? v : L""); }
    else if (!_wcsicmp(k, L"vision_enabled"))   c->vision_enabled = AtoiDef(v, c->vision_enabled);
    else if (!_wcsicmp(k, L"idle_pause"))       c->idle_pause = AtoiDef(v, c->idle_pause);
    else if (!_wcsicmp(k, L"idle_seconds"))     c->idle_seconds = AtoiDef(v, c->idle_seconds);
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

int ConfigLoad(Config *c, const WCHAR *path)
{
    ConfigDefaults(c);

    HANDLE h = CreateFileW(path, GENERIC_READ, FILE_SHARE_READ | FILE_SHARE_WRITE, NULL,
                           OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, NULL);
    if (h == INVALID_HANDLE_VALUE) return 0;

    LARGE_INTEGER sz;
    GetFileSizeEx(h, &sz);
    if (sz.QuadPart <= 0 || sz.QuadPart > 1 << 20) { CloseHandle(h); return 0; }

    char *raw = (char *)malloc((size_t)sz.QuadPart + 4);
    if (!raw) { CloseHandle(h); return 0; }
    DWORD got = 0;
    ReadFile(h, raw, (DWORD)sz.QuadPart, &got, NULL);
    CloseHandle(h);
    raw[got] = 0;

    char *p = raw;
    if (got >= 3 && (unsigned char)raw[0] == 0xEF && (unsigned char)raw[1] == 0xBB && (unsigned char)raw[2] == 0xBF) p += 3;

    WCHAR *wide = (WCHAR *)malloc((got + 2) * sizeof(WCHAR));
    if (!wide) { free(raw); return 0; }
    MultiByteToWideChar(CP_UTF8, 0, p, -1, wide, got + 2);

    WCHAR *line = wide;
    while (line && *line) {
        WCHAR *nl = wcschr(line, L'\n');
        if (nl) *nl = 0;
        WCHAR *s = TrimW(line);
        if (*s && *s != L';' && *s != L'#') {
            WCHAR *eq = wcschr(s, L'=');
            if (eq) {
                *eq = 0;
                WCHAR *key = TrimW(s);
                WCHAR *val = TrimW(eq + 1);
                WCHAR *cm = wcschr(val, L'#');       /* strip inline comment */
                if (cm) { *cm = 0; val = TrimW(val); }
                cm = wcschr(val, L';');
                if (cm) { *cm = 0; val = TrimW(val); }
                SetKey(c, key, val);
            }
        }
        line = nl ? nl + 1 : NULL;
    }

    free(wide);
    free(raw);

    if (c->session_minutes < 1)   c->session_minutes = 1;
    if (c->break_minutes < 1)     c->break_minutes = 1;
    if (c->screenshot_interval < 5) c->screenshot_interval = 5;
    if (c->idle_seconds < 10)     c->idle_seconds = 10;
    if (c->warn_minutes < 0)      c->warn_minutes = 0;
    if (c->max_image_width < 320) c->max_image_width = 320;
    /* 注意：这里不填默认密码。已有哈希时保持哈希不变，
       只有在还没有哈希（首次运行）时 ConfigFinalizePassword 才会用默认密码 642101。 */
    return 1;
}

int ConfigSaveDefault(const Config *c, const WCHAR *path)
{
    static const char *txt =
        "; ScreenTime 屏幕使用时间管理 配置文件\n"
        "; 修改后保存，重启程序生效（或托盘右键 -> 重载配置）\n"
        "\n"
        "; 每次限时使用多少分钟（触发后锁屏强制休息）\n"
        "session_minutes = 45\n"
        "\n"
        "; 触发后强制休息多少分钟（休息结束自动解锁）\n"
        "break_minutes = 15\n"
        "\n"
        "; 提前解锁密码。程序读到这一行后会自动把它变成不可逆的哈希（password_hash / password_salt），\n"
        "; 并把这一行从文件里删掉，配置文件同时被设为隐藏属性，所以看不到明文密码。\n"
        "; 想换密码：在本行写上 password = 新密码，保存后重启程序（或点“重载配置”）即可。\n"
        "password =\n"
        "\n"
        "; 下面三行由程序自动生成，用于校验密码（不要手工编辑）\n"
        "password_hash =\n"
        "password_salt =\n"
        "pw_iterations = 20000\n"
        "\n"
        "; 1=启动时不显示任何窗口，只在后台计时并锁屏\n"
        "start_hidden = 1\n"
        "; 1=隐藏时在右下角显示托盘图标（双击可恢复窗口）；0=完全不可见（再运行一次程序可唤出窗口）\n"
        "show_tray_icon = 1\n"
        "\n"
        "; 每隔多少秒截一次屏并送模型识别\n"
        "screenshot_interval = 60\n"
        "api_url = http://192.168.6.220:8177/v1\n"
        "model = qwen3.8-flash-next-iq3_xxs\n"
        "api_key =\n"
        "vision_enabled = 1\n"
        "idle_pause = 1\n"
        "idle_seconds = 120\n"
        "penalty = 1\n"
        "warn_minutes = 5\n"
        "save_screenshots = 1\n"
        "max_image_width = 1280\n"
        "wrong_penalty_seconds = 30\n"
        "block_hotkeys = 1\n"
        "keep_days = 7\n";
    (void)c;
    DWORD attrs = GetFileAttributesW(path);
    HANDLE h = CreateFileW(path, GENERIC_WRITE, 0, NULL, CREATE_ALWAYS,
                           attrs != INVALID_FILE_ATTRIBUTES ? attrs : FILE_ATTRIBUTE_NORMAL, NULL);
    if (h == INVALID_HANDLE_VALUE) {
        if (attrs != INVALID_FILE_ATTRIBUTES) SetFileAttributesW(path, FILE_ATTRIBUTE_NORMAL);
        h = CreateFileW(path, GENERIC_WRITE, 0, NULL, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, NULL);
        if (h == INVALID_HANDLE_VALUE) return 0;
    }
    DWORD written = 0;
    WriteFile(h, txt, (DWORD)strlen(txt), &written, NULL);
    CloseHandle(h);
    return 1;
}

/* ---------- 把明文密码换成 PBKDF2 哈希，并就地改写配置文件（保留注释） ---------- */

static void WToAscii(const WCHAR *w, char *out, int cap)
{
    int i = 0;
    for (; w[i] && i < cap - 1; i++) out[i] = (char)w[i];
    out[i] = 0;
}

static int IsPwKey(const char *t, const char *key)
{
    size_t n = strlen(key);
    if (_strnicmp(t, key, n) != 0) return 0;
    char c = t[n];
    return (c == '=' || c == ' ' || c == '\t');
}

static int RewritePasswordLines(const WCHAR *path, const WCHAR *hexHash,
                                const WCHAR *hexSalt, int iterations)
{
    HANDLE h = CreateFileW(path, GENERIC_READ, FILE_SHARE_READ | FILE_SHARE_WRITE, NULL,
                           OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, NULL);
    if (h == INVALID_HANDLE_VALUE) return 0;

    LARGE_INTEGER sz;
    GetFileSizeEx(h, &sz);
    if (sz.QuadPart <= 0 || sz.QuadPart > 1 << 20) { CloseHandle(h); return 0; }

    char *raw = (char *)malloc((size_t)sz.QuadPart + 1);
    if (!raw) { CloseHandle(h); return 0; }
    DWORD got = 0;
    ReadFile(h, raw, (DWORD)sz.QuadPart, &got, NULL);
    CloseHandle(h);
    raw[got] = 0;

    char *body = raw;
    int hasBom = (got >= 3 && (unsigned char)raw[0] == 0xEF &&
                  (unsigned char)raw[1] == 0xBB && (unsigned char)raw[2] == 0xBF);
    if (hasBom) body += 3;

    char hashLine[160], saltLine[120], iterLine[80];
    char hashA[128], saltA[96];
    WToAscii(hexHash, hashA, (int)sizeof(hashA));
    WToAscii(hexSalt, saltA, (int)sizeof(saltA));
    snprintf(hashLine, sizeof(hashLine), "password_hash = %s\n", hashA);
    snprintf(saltLine, sizeof(saltLine), "password_salt = %s\n", saltA);
    snprintf(iterLine,  sizeof(iterLine),  "pw_iterations = %d\n", iterations);

    char *outbuf = (char *)malloc((size_t)got + 1024);
    if (!outbuf) { free(raw); return 0; }

    size_t o = 0;
    if (hasBom) { memcpy(outbuf + o, "\xEF\xBB\xBF", 3); o += 3; }

    char *p = body;
    int replaced = 0;
    while (*p) {
        char *nl = strchr(p, '\n');
        size_t lineLen = nl ? (size_t)(nl - p + 1) : strlen(p);

        char *t = p;
        while (*t == ' ' || *t == '\t') t++;
        if (IsPwKey(t, "password") || IsPwKey(t, "password_hash") ||
            IsPwKey(t, "password_salt") || IsPwKey(t, "pw_iterations")) {
            if (!replaced) {           /* 新的三行放在第一个密码相关行的位置 */
                memcpy(outbuf + o, hashLine, strlen(hashLine)); o += strlen(hashLine);
                memcpy(outbuf + o, saltLine, strlen(saltLine)); o += strlen(saltLine);
                memcpy(outbuf + o, iterLine,  strlen(iterLine));  o += strlen(iterLine);
                replaced = 1;
            }
            p = nl ? nl + 1 : p + lineLen;   /* 旧的密码相关行一律丢弃 */
            continue;
        }
        memcpy(outbuf + o, p, lineLen);
        o += lineLen;
        p = nl ? nl + 1 : p + lineLen;
    }

    if (!replaced) {
        memcpy(outbuf + o, hashLine, strlen(hashLine)); o += strlen(hashLine);
        memcpy(outbuf + o, saltLine, strlen(saltLine)); o += strlen(saltLine);
        memcpy(outbuf + o, iterLine,  strlen(iterLine));  o += strlen(iterLine);
    }

    /* 覆写时保留原有属性：隐藏文件用 FILE_ATTRIBUTE_NORMAL 会被拒绝（ERROR_ACCESS_DENIED） */
    DWORD attrs = GetFileAttributesW(path);
    HANDLE w = CreateFileW(path, GENERIC_WRITE, 0, NULL, CREATE_ALWAYS,
                           attrs != INVALID_FILE_ATTRIBUTES ? attrs : FILE_ATTRIBUTE_NORMAL, NULL);
    if (w == INVALID_HANDLE_VALUE) {
        if (attrs != INVALID_FILE_ATTRIBUTES) SetFileAttributesW(path, FILE_ATTRIBUTE_NORMAL);
        w = CreateFileW(path, GENERIC_WRITE, 0, NULL, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, NULL);
        if (w == INVALID_HANDLE_VALUE) { free(raw); free(outbuf); return 0; }
    }
    DWORD written = 0;
    WriteFile(w, outbuf, (DWORD)o, &written, NULL);
    CloseHandle(w);

    free(raw);
    free(outbuf);

    /* 配置文件设为隐藏，避免被随手翻到 */
    SetFileAttributesW(path, FILE_ATTRIBUTE_HIDDEN);
    return 1;
}

int ConfigFinalizePassword(Config *c, const WCHAR *path)
{
    if (!c) return 0;

    if (c->pw_hash[0] && c->pw_salt[0] && !c->password[0]) {
        c->password[0] = 0;   /* 已经是哈希，内存里也不留明文 */
        return 0;
    }

    if (c->pw_iterations < 1000)   c->pw_iterations = 1000;
    if (c->pw_iterations > 500000) c->pw_iterations = 500000;
    if (!c->password[0]) StringCchCopyW(c->password, 64, L"642101");

    unsigned char salt[16], out[32];
    if (!PwRandomSalt(salt, 16)) return 0;              /* 极端情况：保留明文比较 */
    if (!PwDerive(c->password, salt, 16, c->pw_iterations, out)) return 0;

    WCHAR hexHash[ST_PW_HASH_HEX], hexSalt[ST_PW_SALT_HEX];
    PwHexEncode(out, 32, hexHash);
    PwHexEncode(salt, 16, hexSalt);
    StringCchCopyW(c->pw_hash, ST_PW_HASH_HEX, hexHash);
    StringCchCopyW(c->pw_salt, ST_PW_SALT_HEX, hexSalt);
#ifdef ST_PWDEBUG
    LogEventW(L"[pwdbg] finalize: hash=%ls salt=%ls iter=%d", c->pw_hash, c->pw_salt, c->pw_iterations);
#endif
    SecureZeroMemory(c->password, sizeof(c->password)); /* 明文只存在这几毫秒里 */

    RewritePasswordLines(path, hexHash, hexSalt, c->pw_iterations);
    return 1;
}

