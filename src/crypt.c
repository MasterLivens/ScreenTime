#include "crypt.h"
#include "passwd.h"
#include <bcrypt.h>
#include <stdlib.h>
#include <string.h>

#define ST_CFG_MAGIC  "STCF"
#define ST_CFG_VER    1
#define ST_CFG_ITER   12000
#define ST_CFG_HDR    44          /* 4 magic + 4 ver + 4 iter + 16 salt + 16 iv */

/* 内置口令：和 exe 一起发出去，只用于"不让人直接翻配置文件" */
static const WCHAR kSecret[] = L"ScreenTime/cfg/v1/9f2c7a41b8e35d60";

static void Put32(unsigned char *p, unsigned int v)
{
    p[0] = (unsigned char)(v & 0xFF);
    p[1] = (unsigned char)((v >> 8) & 0xFF);
    p[2] = (unsigned char)((v >> 16) & 0xFF);
    p[3] = (unsigned char)((v >> 24) & 0xFF);
}

static unsigned int Get32(const unsigned char *p)
{
    return (unsigned int)p[0] | ((unsigned int)p[1] << 8) |
           ((unsigned int)p[2] << 16) | ((unsigned int)p[3] << 24);
}

int CryptIsSealed(const void *blob, int blobLen)
{
    return blob != NULL && blobLen > ST_CFG_HDR &&
           memcmp(blob, ST_CFG_MAGIC, 4) == 0;
}

static int DeriveKey(const unsigned char *salt, int iter, unsigned char key[32])
{
    return PwDerive(kSecret, salt, 16, iter, key);
}

static int AesRun(int encrypt, const unsigned char key[32], const unsigned char iv[16],
                  const unsigned char *in, int inLen, unsigned char *out, int outCap,
                  int *outLen)
{
    BCRYPT_ALG_HANDLE  alg = NULL;
    BCRYPT_KEY_HANDLE  kh = NULL;
    unsigned char     *obj = NULL;
    DWORD objLen = 0, got = 0, need = 0;
    int ok = 0;

    if (BCryptOpenAlgorithmProvider(&alg, BCRYPT_AES_ALGORITHM, NULL, 0) != 0) goto done;
    if (BCryptSetProperty(alg, BCRYPT_CHAINING_MODE, (PUCHAR)BCRYPT_CHAIN_MODE_CBC,
                          sizeof(BCRYPT_CHAIN_MODE_CBC), 0) != 0) goto done;
    if (BCryptGetProperty(alg, BCRYPT_OBJECT_LENGTH, (PUCHAR)&objLen, sizeof(objLen),
                          &got, 0) != 0) goto done;
    obj = (unsigned char *)malloc(objLen ? objLen : 1);
    if (!obj) goto done;
    if (BCryptGenerateSymmetricKey(alg, &kh, obj, objLen, (PUCHAR)key, 32, 0) != 0) goto done;

    if (encrypt) {
        if (BCryptEncrypt(kh, (PUCHAR)in, (ULONG)inLen, NULL, (PUCHAR)iv, 16,
                          out, (ULONG)outCap, &need, BCRYPT_BLOCK_PADDING) != 0) goto done;
    } else {
        if (BCryptDecrypt(kh, (PUCHAR)in, (ULONG)inLen, NULL, (PUCHAR)iv, 16,
                          out, (ULONG)outCap, &need, BCRYPT_BLOCK_PADDING) != 0) goto done;
    }
    *outLen = (int)need;
    ok = 1;

done:
    if (kh)  BCryptDestroyKey(kh);
    if (alg) BCryptCloseAlgorithmProvider(alg, 0);
    if (obj) { SecureZeroMemory(obj, objLen); free(obj); }
    return ok;
}

int CryptSeal(const void *plain, int plainLen, unsigned char **out, int *outLen)
{
    unsigned char salt[16], iv[16], key[32];
    unsigned char *buf = NULL;
    int cap, n = 0;

    if (!plain || plainLen <= 0 || !out || !outLen) return 0;
    *out = NULL;
    *outLen = 0;

    if (!PwRandomSalt(salt, 16)) return 0;
    if (!PwRandomSalt(iv, 16)) return 0;
    if (!DeriveKey(salt, ST_CFG_ITER, key)) return 0;

    cap = plainLen + 32;
    buf = (unsigned char *)calloc(1, (size_t)ST_CFG_HDR + (size_t)cap);
    if (!buf) { SecureZeroMemory(key, sizeof(key)); return 0; }

    memcpy(buf, ST_CFG_MAGIC, 4);
    Put32(buf + 4, ST_CFG_VER);
    Put32(buf + 8, ST_CFG_ITER);
    memcpy(buf + 12, salt, 16);
    memcpy(buf + 28, iv, 16);

    if (!AesRun(1, key, iv, (const unsigned char *)plain, plainLen,
                buf + ST_CFG_HDR, cap, &n)) {
        free(buf);
        SecureZeroMemory(key, sizeof(key));
        return 0;
    }
    SecureZeroMemory(key, sizeof(key));

    *out = buf;
    *outLen = ST_CFG_HDR + n;
    return 1;
}

int CryptOpen(const void *blob, int blobLen, unsigned char **out, int *outLen)
{
    const unsigned char *b = (const unsigned char *)blob;
    unsigned char salt[16], iv[16], key[32];
    unsigned char *buf = NULL;
    unsigned int iter;
    int clen, n = 0;

    if (!out || !outLen) return 0;
    *out = NULL;
    *outLen = 0;
    if (!CryptIsSealed(blob, blobLen)) return 0;
    if (Get32(b + 4) != ST_CFG_VER) return 0;

    iter = Get32(b + 8);
    if (iter < 1000 || iter > 1000000) return 0;
    memcpy(salt, b + 12, 16);
    memcpy(iv, b + 28, 16);
    clen = blobLen - ST_CFG_HDR;
    if (clen <= 0 || (clen & 15) != 0) return 0;
    if (!DeriveKey(salt, (int)iter, key)) return 0;

    buf = (unsigned char *)malloc((size_t)clen + 1);
    if (!buf) { SecureZeroMemory(key, sizeof(key)); return 0; }
    if (!AesRun(0, key, iv, b + ST_CFG_HDR, clen, buf, clen, &n)) {
        free(buf);
        SecureZeroMemory(key, sizeof(key));
        return 0;
    }
    SecureZeroMemory(key, sizeof(key));

    buf[n] = 0;
    *out = buf;
    *outLen = n;
    return 1;
}
