#include "passwd.h"
#include "util.h"
#ifdef ST_PWDEBUG
#include "logger.h"
#endif
#include <bcrypt.h>
#include <strsafe.h>
#include <stdlib.h>
#include <string.h>
#include <stdio.h>

/* 随机盐；BCryptGenRandom 失败时用时间/计数器兜底 */
int PwRandomSalt(unsigned char *salt, int len)
{
    if (salt == NULL || len <= 0) return 0;
    if (BCryptGenRandom(NULL, salt, (ULONG)len, BCRYPT_USE_SYSTEM_PREFERRED_RNG) == 0)
        return 1;

    LARGE_INTEGER t;
    QueryPerformanceCounter(&t);
    DWORD tick = GetTickCount();
    srand((unsigned)(t.QuadPart ^ tick));
    for (int i = 0; i < len; i++)
        salt[i] = (unsigned char)((t.QuadPart >> ((i % 8) * 5)) ^ (tick >> (i % 16)) ^ (rand() & 0xFF));
    return 0;
}

/* PBKDF2-HMAC-SHA256(password, salt, iterations) -> 32 字节 */
int PwDerive(const WCHAR *pass, const unsigned char *salt, int saltLen,
             int iterations, unsigned char out[32])
{
    if (!pass || !*pass || !salt || saltLen <= 0 || !out) return 0;

    char pwbuf[512];
    int n = WToUtf8(pass, pwbuf, (int)sizeof(pwbuf));
    if (n <= 0) return 0;

    BCRYPT_ALG_HANDLE hAlg = NULL;
    if (BCryptOpenAlgorithmProvider(&hAlg, BCRYPT_SHA256_ALGORITHM, NULL,
                                    BCRYPT_ALG_HANDLE_HMAC_FLAG) != 0 || hAlg == NULL) {
        SecureZeroMemory(pwbuf, sizeof(pwbuf));
        return 0;
    }

    NTSTATUS st = BCryptDeriveKeyPBKDF2(hAlg,
                                        (PUCHAR)pwbuf, (ULONG)n,
                                        (PUCHAR)salt,  (ULONG)saltLen,
                                        (ULONGLONG)(iterations > 0 ? iterations : ST_PW_DEFAULT_ITER),
                                        out, 32, 0);

    BCryptCloseAlgorithmProvider(hAlg, 0);
    SecureZeroMemory(pwbuf, sizeof(pwbuf));
    return st == 0 ? 1 : 0;
}

void PwHexEncode(const unsigned char *in, int n, WCHAR *out)
{
    static const WCHAR hex[] = L"0123456789abcdef";
    for (int i = 0; i < n; i++) {
        out[i * 2]     = hex[(in[i] >> 4) & 0xF];
        out[i * 2 + 1] = hex[in[i] & 0xF];
    }
    out[n * 2] = 0;
}

static int HexNibble(WCHAR c)
{
    if (c >= L'0' && c <= L'9') return c - L'0';
    if (c >= L'a' && c <= L'f') return c - L'a' + 10;
    if (c >= L'A' && c <= L'F') return c - L'A' + 10;
    return -1;
}

int PwHexDecode(const WCHAR *in, unsigned char *out, int maxOut)
{
    if (!in || !out) return 0;
    int n = 0;
    while (in[n * 2] && in[n * 2 + 1] && n < maxOut) {
        int hi = HexNibble(in[n * 2]);
        int lo = HexNibble(in[n * 2 + 1]);
        if (hi < 0 || lo < 0) break;
        out[n++] = (unsigned char)((hi << 4) | lo);
    }
    return n;
}

int PwVerify(const WCHAR *pass, const WCHAR *hexHash, const WCHAR *hexSalt,
             int iterations, const WCHAR *plainFallback)
{
    if (!pass || !*pass) return 0;

    if (hexHash && hexHash[0] && hexSalt && hexSalt[0]) {
        unsigned char salt[64], want[64], got[64];
        int saltLen = PwHexDecode(hexSalt, salt, (int)sizeof(salt));
        int wantLen = PwHexDecode(hexHash, want, (int)sizeof(want));
#ifdef ST_PWDEBUG
        LogEventW(L"[pwdbg] passlen=%d iter=%d saltLen=%d wantLen=%d hashlen=%d saltstrlen=%d",
                  (int)wcslen(pass), iterations, saltLen, wantLen,
                  (int)wcslen(hexHash), (int)wcslen(hexSalt));
#endif
        if (saltLen <= 0 || wantLen != 32) return 0;
        if (!PwDerive(pass, salt, saltLen, iterations, got)) return 0;

        unsigned char diff = 0;                 /* 常量时间比较 */
        for (int i = 0; i < 32; i++) diff |= (unsigned char)(got[i] ^ want[i]);
#ifdef ST_PWDEBUG
        {
            WCHAR gotHex[80], wantHex[80];
            PwHexEncode(got, 32, gotHex);
            PwHexEncode(want, 32, wantHex);
            LogEventW(L"[pwdbg] got=%ls want=%ls match=%d", gotHex, wantHex, (int)diff);
        }
#endif
        return diff == 0 ? 1 : 0;
    }

    if (plainFallback && plainFallback[0])
        return wcscmp(pass, plainFallback) == 0 ? 1 : 0;

    return 0;
}
