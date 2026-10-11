#ifndef ST_CRYPT_H
#define ST_CRYPT_H

#include <windows.h>

/* 配置文件加密存储：AES-256-CBC（CNG/bcrypt），
   密钥 = PBKDF2-HMAC-SHA256(内置口令, 每次保存新生成的随机盐)。
   密文自带文件头：magic "STCF" + 版本 + 迭代次数 + 盐 + IV。
   注意：这是"防翻看/防随意篡改"级别的保护，不是密码学意义上的安全边界——
   密钥口令就在 exe 里，会逆向的人仍然能解开。 */

int CryptIsSealed(const void *blob, int blobLen);
int CryptSeal(const void *plain, int plainLen, unsigned char **out, int *outLen);
int CryptOpen(const void *blob, int blobLen, unsigned char **out, int *outLen);

#endif
