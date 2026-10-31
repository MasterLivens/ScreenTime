#ifndef ST_MEDIA_H
#define ST_MEDIA_H

#include <windows.h>

/* 媒体/视频检测：完全不截图，只用系统状态判断"是否正在播放视频"。
 *
 * 组合三个信号：
 *   1) WASAPI 音频会话：某个会话处于 Active 且峰值 > 0  -> 有声音在播
 *   2) PDH GPU 计数器：\GPU Engine(*engtype_VideoDecode)\Utilization Percentage > 0
 *      -> 有进程在用硬件解码视频（静音也算）
 *   3) 前景窗口是否铺满整个显示器（无边框全屏 / 独占全屏 / F11 全屏）
 *
 * 对外只暴露两个状态：
 *   MediaVideoActive()     有视频/媒体在播（窗口化或全屏）
 *   MediaFullscreenVideo() 全屏 + 在播 -> "观影模式"
 *
 * 采集在独立线程里每秒一次，主线程读全局变量，不做任何阻塞调用。 */

void MediaStart(void);
void MediaStop(void);

/* 检测模式（对应配置 video_detect）：
 *   0 = 关闭（不启动采集线程）
 *   1 = 自动：音频/硬解 + 全屏（默认；有 GPU、有声卡时最准）
 *   2 = 只看全屏：只要前景窗口铺满屏幕就算"在播"，完全不依赖音频与 GPU
 *       —— 给没有显卡/声卡的虚拟机用，判据最稳，代价是全屏游戏/PPT 也会被当成观影
 */
void MediaSetMode(int mode);
int  MediaMode(void);

/* 1 = 有视频/媒体在播；0 = 没有。每秒刷新。 */
int MediaVideoActive(void);

/* 1 = 全屏且正在播（观影模式）；0 = 否。连续约 2 秒满足才置 1，退出有约 3 秒迟滞。 */
int MediaFullscreenVideo(void);

/* 最近一次采集的摘要（写日志用），例如 "音频=1 硬解=0 全屏=1"。 */
const WCHAR *MediaLastDetail(void);

#endif
