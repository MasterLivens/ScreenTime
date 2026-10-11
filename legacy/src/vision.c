#include "vision.h"
#include "util.h"
#include <winhttp.h>
#include <strsafe.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>

/* ---------------- URL 解析 ---------------- */
static int UrlSplit(const WCHAR *url, WCHAR *host, size_t hostN, INTERNET_PORT *port,
                    WCHAR *path, size_t pathN, BOOL *secure)
{
    const WCHAR *p = url;
    *secure = FALSE;
    *port = INTERNET_DEFAULT_HTTP_PORT;

    if (!_wcsnicmp(p, L"https://", 8)) { *secure = TRUE; *port = INTERNET_DEFAULT_HTTPS_PORT; p += 8; }
    else if (!_wcsnicmp(p, L"http://", 7)) { p += 7; }

    const WCHAR *slash = wcschr(p, L'/');
    const WCHAR *hostEnd = slash ? slash : p + wcslen(p);

    const WCHAR *colon = NULL;
    for (const WCHAR *q = p; q < hostEnd; q++) if (*q == L':') { colon = q; break; }

    const WCHAR *hEnd = colon ? colon : hostEnd;
    size_t hlen = (size_t)(hEnd - p);
    if (hlen == 0 || hlen + 1 > hostN) return 0;
    for (size_t i = 0; i < hlen; i++) host[i] = p[i];
    host[hlen] = 0;

    if (colon) {
        int pv = _wtoi(colon + 1);
        if (pv > 0 && pv < 65536) *port = (INTERNET_PORT)pv;
    }

    if (slash) {
        StringCchCopyW(path, pathN, slash);
        size_t l = wcslen(path);
        while (l > 0 && (path[l-1] == L'/')) path[--l] = 0;
    } else {
        path[0] = 0;
    }
    return 1;
}

/* ---------------- JSON 取值 ---------------- */
static const char *FindKey(const char *j, const char *key)
{
    if (!j) return NULL;
    char pat[64];
    sprintf(pat, "\"%s\"", key);
    const char *p = j;
    while ((p = strstr(p, pat)) != NULL) {
        const char *q = p + strlen(pat);
        while (*q && isspace((unsigned char)*q)) q++;
        if (*q == ':') return q + 1;
        p = q;
    }
    return NULL;
}

static void Utf8Put(char *out, size_t cap, size_t *o, unsigned int cp)
{
    if (cp < 0x80) {
        if (*o + 1 < cap) out[(*o)++] = (char)cp;
    } else if (cp < 0x800) {
        if (*o + 2 < cap) {
            out[(*o)++] = (char)(0xC0 | (cp >> 6));
            out[(*o)++] = (char)(0x80 | (cp & 0x3F));
        }
    } else if (cp < 0x10000) {
        if (*o + 3 < cap) {
            out[(*o)++] = (char)(0xE0 | (cp >> 12));
            out[(*o)++] = (char)(0x80 | ((cp >> 6) & 0x3F));
            out[(*o)++] = (char)(0x80 | (cp & 0x3F));
        }
    } else if (*o + 4 < cap) {
        out[(*o)++] = (char)(0xF0 | (cp >> 18));
        out[(*o)++] = (char)(0x80 | ((cp >> 12) & 0x3F));
        out[(*o)++] = (char)(0x80 | ((cp >> 6) & 0x3F));
        out[(*o)++] = (char)(0x80 | (cp & 0x3F));
    }
}

static int Hex4(const char *s, unsigned int *v)
{
    unsigned int r = 0;
    for (int i = 0; i < 4; i++) {
        char c = s[i];
        r <<= 4;
        if (c >= '0' && c <= '9') r |= (unsigned)(c - '0');
        else if (c >= 'a' && c <= 'f') r |= (unsigned)(c - 'a' + 10);
        else if (c >= 'A' && c <= 'F') r |= (unsigned)(c - 'A' + 10);
        else return 0;
    }
    *v = r;
    return 1;
}

static int ExtractJsonString(const char *start, char *out, size_t cap)
{
    if (!start || cap < 2) return 0;
    while (*start && isspace((unsigned char)*start)) start++;

    if (*start == '[') {                       /* content 为数组时取其中的 text */
        const char *t = FindKey(start, "text");
        if (!t) return 0;
        start = t;
        while (*start && isspace((unsigned char)*start)) start++;
    }
    if (*start != '"') return 0;
    start++;

    size_t o = 0;
    while (*start && *start != '"') {
        if (*start == '\\') {
            start++;
            switch (*start) {
            case 'n': if (o + 1 < cap) out[o++] = '\n'; break;
            case 'r': if (o + 1 < cap) out[o++] = '\r'; break;
            case 't': if (o + 1 < cap) out[o++] = '\t'; break;
            case 'u': {
                unsigned int cp = 0;
                if (Hex4(start + 1, &cp)) {
                    start += 4;
                    if (cp >= 0xD800 && cp <= 0xDBFF && start[1] == '\\' && start[2] == 'u') {
                        unsigned int lo = 0;
                        if (Hex4(start + 3, &lo) && lo >= 0xDC00 && lo <= 0xDFFF) {
                            cp = 0x10000 + ((cp - 0xD800) << 10) + (lo - 0xDC00);
                            start += 6;
                        }
                    }
                    Utf8Put(out, cap, &o, cp);
                }
                break;
            }
            case 0: goto done;
            default: if (o + 1 < cap) out[o++] = *start; break;
            }
            start++;
        } else {
            unsigned char c = (unsigned char)*start++;
            if (o + 4 < cap) out[o++] = (char)c;
            else break;
        }
    }
done:
    out[o] = 0;
    return (int)o;
}

static int ExtractAnswer(const char *json, char *out, size_t cap)
{
    const char *ch = FindKey(json, "choices");
    const char *content = ch ? FindKey(ch, "content") : NULL;
    if (!content) content = FindKey(json, "content");
    if (!content) return 0;
    return ExtractJsonString(content, out, cap);
}

static int ExtractError(const char *json, char *out, size_t cap)
{
    const char *e = FindKey(json, "error");
    if (!e) return 0;
    const char *m = FindKey(e, "message");
    if (!m) return 0;
    return ExtractJsonString(m, out, cap);
}

/* ---------------- HTTP POST ---------------- */
static int HttpPostJson(const Config *cfg, const char *body, char *resp, size_t respCap, int *httpStatus)
{
    WCHAR host[256], path[256];
    INTERNET_PORT port;
    BOOL secure;
    if (!UrlSplit(cfg->api_url, host, sizeof(host) / sizeof(WCHAR), &port, path,
                  sizeof(path) / sizeof(WCHAR), &secure))
        return 0;

    WCHAR full[512];
    StringCchCopyW(full, 512, path);
    StringCchCatW(full, 512, L"/chat/completions");

    int ok = 0;
    HINTERNET s = WinHttpOpen(L"ScreenTime/1.0", WINHTTP_ACCESS_TYPE_DEFAULT_PROXY,
                              WINHTTP_NO_PROXY_NAME, WINHTTP_NO_PROXY_BYPASS, 0);
    if (!s) return 0;
    WinHttpSetTimeouts(s, 10000, 10000, 20000, 180000);

    HINTERNET c = WinHttpConnect(s, host, port, 0);
    if (c) {
        HINTERNET r = WinHttpOpenRequest(c, L"POST", full, NULL, WINHTTP_NO_REFERER,
                                         WINHTTP_DEFAULT_ACCEPT_TYPES, secure ? WINHTTP_FLAG_SECURE : 0);
        if (r) {
            const WCHAR *hdrs[] = {
                L"Content-Type: application/json",
                L"Accept: application/json"
            };
            WinHttpAddRequestHeaders(r, hdrs[0], (DWORD)-1, WINHTTP_ADDREQ_FLAG_ADD);
            WinHttpAddRequestHeaders(r, hdrs[1], (DWORD)-1, WINHTTP_ADDREQ_FLAG_ADD);
            if (cfg->api_key[0]) {
                WCHAR auth[320];
                StringCchCopyW(auth, 320, L"Authorization: Bearer ");
                StringCchCatW(auth, 320, cfg->api_key);
                WinHttpAddRequestHeaders(r, auth, (DWORD)-1, WINHTTP_ADDREQ_FLAG_ADD);
            }

            if (WinHttpSendRequest(r, WINHTTP_NO_ADDITIONAL_HEADERS, 0,
                                   (LPVOID)body, (DWORD)strlen(body), (DWORD)strlen(body), 0)) {
                if (WinHttpReceiveResponse(r, NULL)) {
                    DWORD status = 0, sz = sizeof(status);
                    WinHttpQueryHeaders(r, WINHTTP_QUERY_STATUS_CODE | WINHTTP_QUERY_FLAG_NUMBER,
                                        WINHTTP_HEADER_NAME_BY_INDEX, &status, &sz, WINHTTP_NO_HEADER_INDEX);
                    if (httpStatus) *httpStatus = (int)status;

                    size_t o = 0;
                    for (;;) {
                        DWORD avail = 0;
                        if (!WinHttpQueryDataAvailable(r, &avail) || avail == 0) break;
                        if (o + avail >= respCap) avail = (DWORD)(respCap - o - 1);
                        DWORD read = 0;
                        if (!WinHttpReadData(r, resp + o, avail, &read) || read == 0) break;
                        o += read;
                        if (o + 1 >= respCap) break;
                    }
                    resp[o] = 0;
                    ok = (status >= 200 && status < 300);
                }
            }
            WinHttpCloseHandle(r);
        }
        WinHttpCloseHandle(c);
    }
    WinHttpCloseHandle(s);
    return ok;
}

/* ---------------- 主入口 ---------------- */
int VisionAnalyze(const Config *cfg, const WCHAR *pngPath, VisionResult *res)
{
    memset(res, 0, sizeof(*res));
    LONGLONG t0 = GetTickCount64();

    HANDLE h = CreateFileW(pngPath, GENERIC_READ, FILE_SHARE_READ, NULL,
                           OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, NULL);
    if (h == INVALID_HANDLE_VALUE) {
        StringCchCopyW(res->answer, 2048, L"读取截图失败");
        res->ms = GetTickCount64() - t0;
        return 0;
    }
    LARGE_INTEGER sz;
    GetFileSizeEx(h, &sz);
    if (sz.QuadPart <= 0 || sz.QuadPart > 40 * 1024 * 1024) {
        CloseHandle(h);
        StringCchCopyW(res->answer, 2048, L"截图文件过大或为空");
        res->ms = GetTickCount64() - t0;
        return 0;
    }
    unsigned char *png = (unsigned char *)malloc((size_t)sz.QuadPart);
    if (!png) { CloseHandle(h); return 0; }
    DWORD got = 0;
    ReadFile(h, png, (DWORD)sz.QuadPart, &got, NULL);
    CloseHandle(h);

    char *b64 = Base64Encode(png, got);
    free(png);
    if (!b64) {
        StringCchCopyW(res->answer, 2048, L"base64 编码失败");
        res->ms = GetTickCount64() - t0;
        return 0;
    }

    WCHAR promptW[] = L"这是用户电脑屏幕的截图。请用简体中文回答，共三行：\n"
                      L"1) 正在做什么（一句话）\n"
                      L"2) 类别：工作/学习/娱乐/社交/浏览/休息/其他\n"
                      L"3) 专注程度：高/中/低\n"
                      L"只输出这三行，不要多余内容。";
    char promptA[1024];
    WToUtf8(promptW, promptA, (int)sizeof(promptA));
    char promptEsc[1400];
    JsonEscapeA(promptA, promptEsc, sizeof(promptEsc));

    WCHAR sysW[] = L"你是屏幕使用行为分析助手，回答简洁。";
    char sysA[512];
    WToUtf8(sysW, sysA, (int)sizeof(sysA));
    char sysEsc[700];
    JsonEscapeA(sysA, sysEsc, sizeof(sysEsc));

    char modelA[256];
    WToUtf8(cfg->model, modelA, (int)sizeof(modelA));

    size_t need = strlen(b64) + strlen(promptEsc) + strlen(sysEsc) + strlen(modelA) + 512;
    char *body = (char *)malloc(need);
    if (!body) { free(b64); return 0; }
    sprintf(body,
            "{\"model\":\"%s\",\"messages\":[{\"role\":\"system\",\"content\":\"%s\"},"
            "{\"role\":\"user\",\"content\":[{\"type\":\"text\",\"text\":\"%s\"},"
            "{\"type\":\"image_url\",\"image_url\":{\"url\":\"data:image/png;base64,%s\"}}]}],"
            "\"temperature\":0.2,\"max_tokens\":400,\"stream\":false}",
            modelA, sysEsc, promptEsc, b64);
    free(b64);

    char *resp = (char *)malloc(256 * 1024);
    if (!resp) { free(body); return 0; }
    resp[0] = 0;

    int status = 0;
    int ok = HttpPostJson(cfg, body, resp, 256 * 1024, &status);
    free(body);

    res->status = status;
    res->ok = ok;

    char ansA[4000];
    ansA[0] = 0;
    if (ok) {
        if (ExtractAnswer(resp, ansA, sizeof(ansA)) <= 0)
            StringCchCopyA(ansA, sizeof(ansA), "(模型未返回文本)");
    } else {
        if (ExtractError(resp, ansA, sizeof(ansA)) <= 0) {
            if (status) sprintf(ansA, "HTTP %d", status);
            else StringCchCopyA(ansA, sizeof(ansA), "请求失败（无法连接或超时）");
        }
    }
    free(resp);

    Utf8ToW(ansA, res->answer, 2048);
    res->ms = GetTickCount64() - t0;
    return ok;
}
