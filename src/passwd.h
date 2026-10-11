#ifndef ST_PASSWD_H
#define ST_PASSWD_H

#include <windows.h>

#define ST_PW_HASH_HEX    80    /* 32 字节 -> 64 个十六进制字符 */
#define ST_PW_SALT_HEX    48    /* 16 字节 -> 32 个十六进制字符 */
#define ST_PW_DEFAULT_ITER 20000

int  PwRandomSalt(unsigned char *salt, int len);
int  PwDerive(const WCHAR *pass, const unsigned char *salt, int saltLen,
              int iterations, unsigned char out[32]);
void PwHexEncode(const unsigned char *in, int n, WCHAR *out);
int  PwHexDecode(const WCHAR *in, unsigned char *out, int maxOut);

/* 校验密码：有哈希走 PBKDF2-HMAC-SHA256；没有哈希时回退明文比较 */
int  PwVerify(const WCHAR *pass, const WCHAR *hexHash, const WCHAR *hexSalt,
              int iterations, const WCHAR *plainFallback);

#endif
