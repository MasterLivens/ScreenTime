#ifndef ST_CONFIG_H
#define ST_CONFIG_H

#include <windows.h>
#include "passwd.h"

typedef struct {
    int   session_minutes;        /* 每次限时使用分钟数（默认 45） */
    int   break_minutes;          /* 触发后强制休息分钟数（默认 15） */
    WCHAR password[64];           /* 明文密码：只用于设置/更换，会被自动从文件里清除 */
    WCHAR pw_hash[ST_PW_HASH_HEX];/* 解锁密码的 PBKDF2-HMAC-SHA256（十六进制） */
    WCHAR pw_salt[ST_PW_SALT_HEX];/* 盐（十六进制） */
    int   pw_iterations;          /* PBKDF2 迭代次数 */
    int   screenshot_interval;    /* 截图间隔秒（默认 60） */
    WCHAR api_url[256];           /* OpenAI 兼容地址，含 /v1 */
    WCHAR model[128];
    WCHAR api_key[128];
    int   vision_enabled;         /* 1=调用模型识别 */
    int   idle_pause;             /* 1=无人操作时暂停计时 */
    int   idle_seconds;           /* 无输入多少秒后视为离开 */
    int   penalty;                /* 1=密码提前解锁会缩短下一次使用时长 */
    int   warn_minutes;           /* 锁屏前多少分钟开始提醒 */
    int   save_screenshots;       /* 1=本地留存截图 */
    int   max_image_width;        /* 送模型的截图最大宽度 */
    int   wrong_penalty_seconds;  /* 密码错误追加的锁屏秒数 */
    int   block_hotkeys;          /* 1=锁屏期间屏蔽 Alt+Tab / Win / Alt+F4 / Ctrl+Esc */
    int   keep_days;              /* 截图保留天数，0=永久 */
    int   start_hidden;           /* 1=启动时不显示窗口（只在系统图标区域） */
    int   show_tray_icon;         /* 1=显示托盘图标 */
} Config;

void ConfigDefaults(Config *c);
int  ConfigLoad(Config *c, const WCHAR *path);
int  ConfigSaveDefault(const Config *c, const WCHAR *path);
/* 明文密码 -> PBKDF2 哈希，并就地改写配置文件（清除明文）。返回 1 表示文件已改写 */
int  ConfigFinalizePassword(Config *c, const WCHAR *path);

#endif
