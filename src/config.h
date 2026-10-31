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
    int   screenshot_enabled;     /* 1=定时截图（总开关；0 则完全不截、不识别、不落盘） */
    int   screenshot_interval;    /* 截图间隔秒（默认 60） */
    WCHAR api_url[256];           /* OpenAI 兼容地址，含 /v1 */
    WCHAR model[128];
    WCHAR api_key[128];
    int   vision_enabled;         /* 1=调用模型识别 */
    int   idle_pause;             /* 1=无人操作时暂停计时 */
    int   idle_seconds;           /* 无输入多少秒后视为离开（默认 600 = 10 分钟） */
    int   idle_reset;             /* 1=长时间空闲后回来"重新开始计时"；0=接着之前的进度 */
    int   video_detect;           /* 1=用系统状态检测视频（全屏观影时暂停计时且不锁屏） */
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
/* 返回 0=没有配置文件（默认值） 1=读到 2=读到老版明文配置（需要重新加密保存） */
int  ConfigLoad(Config *c, const WCHAR *path);
/* 加密写入（AES-256-CBC）；成功后会清掉内存里的明文密码并把同目录的老 config.ini 改名为 .ini.bak */
int  ConfigSave(Config *c, const WCHAR *path);
int  ConfigSaveDefault(const Config *c, const WCHAR *path);
/* 明文密码 -> PBKDF2 哈希并加密保存。返回 1 表示写盘了 */
int  ConfigFinalizePassword(Config *c, const WCHAR *path);

#endif
