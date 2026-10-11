/* Password hashing self-test: PBKDF2 derive, hex encode/decode, PwVerify compare */
#include <windows.h>
#include <stdio.h>
#include <string.h>
#include "../src/passwd.h"

int main(int argc, char **argv)
{
    if (argc >= 5) {
        /* pwtest <hash> <salt> <iterations> <password> : 离线校验配置文件里的哈希 */
        WCHAR *hexHash = (WCHAR *)malloc(ST_PW_HASH_HEX * sizeof(WCHAR));
        WCHAR *hexSalt = (WCHAR *)malloc(ST_PW_SALT_HEX * sizeof(WCHAR));
        WCHAR pass[128];
        MultiByteToWideChar(CP_UTF8, 0, argv[1], -1, hexHash, ST_PW_HASH_HEX);
        MultiByteToWideChar(CP_UTF8, 0, argv[2], -1, hexSalt, ST_PW_SALT_HEX);
        MultiByteToWideChar(CP_UTF8, 0, argv[4], -1, pass, 128);
        int iter = atoi(argv[3]);
        int r = PwVerify(pass, hexHash, hexSalt, iter, L"");
        wprintf(L"offline verify -> %d\n", r);
        return r ? 0 : 2;
    }

    unsigned char salt[16], dk[32], dk2[32];
    WCHAR hexHash[ST_PW_HASH_HEX], hexSalt[ST_PW_SALT_HEX];
    WCHAR hexHash2[ST_PW_HASH_HEX];

    if (!PwRandomSalt(salt, (int)sizeof(salt))) { wprintf(L"FAIL: PwRandomSalt\n"); return 1; }
    if (!PwDerive(L"642101", salt, (int)sizeof(salt), ST_PW_DEFAULT_ITER, dk)) { wprintf(L"FAIL: PwDerive\n"); return 1; }
    PwHexEncode(dk, 32, hexHash);
    PwHexEncode(salt, (int)sizeof(salt), hexSalt);
    wprintf(L"hash   = %ls\n", hexHash);
    wprintf(L"salt   = %ls\n", hexSalt);

    unsigned char saltBack[16];
    if (!PwHexDecode(hexSalt, saltBack, (int)sizeof(saltBack))) { wprintf(L"FAIL: PwHexDecode salt\n"); return 1; }
    if (!PwDerive(L"642101", saltBack, (int)sizeof(saltBack), ST_PW_DEFAULT_ITER, dk2)) { wprintf(L"FAIL: PwDerive2\n"); return 1; }
    PwHexEncode(dk2, 32, hexHash2);
    wprintf(L"rehash = %ls  -> %ls\n", hexHash2,
            wcscmp(hexHash, hexHash2) == 0 ? L"SAME" : L"DIFFERENT(FAIL)");

    wprintf(L"verify(642101)    = %d (expect 1)\n",
            PwVerify(L"642101", hexHash, hexSalt, ST_PW_DEFAULT_ITER, L""));
    wprintf(L"verify(123456)    = %d (expect 0)\n",
            PwVerify(L"123456", hexHash, hexSalt, ST_PW_DEFAULT_ITER, L""));
    wprintf(L"verify(6421010)   = %d (expect 0)\n",
            PwVerify(L"6421010", hexHash, hexSalt, ST_PW_DEFAULT_ITER, L""));
    wprintf(L"verify(empty)     = %d (expect 0)\n",
            PwVerify(L"", hexHash, hexSalt, ST_PW_DEFAULT_ITER, L""));

    unsigned char salt3[16];
    WCHAR hexSalt3[ST_PW_SALT_HEX];
    PwRandomSalt(salt3, (int)sizeof(salt3));
    PwHexEncode(salt3, (int)sizeof(salt3), hexSalt3);
    wprintf(L"verify(othersalt) = %d (expect 0)\n",
            PwVerify(L"642101", hexHash, hexSalt3, ST_PW_DEFAULT_ITER, L""));

    wprintf(L"fallback(ok)      = %d (expect 1)\n",
            PwVerify(L"642101", L"", L"", ST_PW_DEFAULT_ITER, L"642101"));
    wprintf(L"fallback(bad)     = %d (expect 0)\n",
            PwVerify(L"999999", L"", L"", ST_PW_DEFAULT_ITER, L"642101"));
    return 0;
}
