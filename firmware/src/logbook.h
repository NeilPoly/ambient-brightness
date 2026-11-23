// 记录（存在板子闪存的 LittleFS 里，网页上能下载 CSV）：
//   每分钟一行：三路照度（这一分钟的平均）、合成照度、目标亮度、各台亮度、自动开没开，最多留 7 天，转圈覆盖；
//   事件另记一个文件：手动调节（manual:编号）、自动开关（auto:on/off）、清掉学到的点（learned:clear）。
// 时间要等 NTP 对上以后才记（没对上时间的那几分钟不记）。
// 每分钟那份先攒在内存里，10 分钟写一次闪存（少写几次）；断电最多丢最近 10 分钟。
#pragma once
#include <Arduino.h>

class WebServer;

void logBegin();
void logSample(const float v[3]);            // 每行读数调一次（loop 里）
void logTick();                              // loop 里常调：到了新的一分钟就记一行
void logFlush();                             // 重启前调：把攒着的写进闪存
void logEvent(const String &ev, float lux, float b);
void logCsv(WebServer &sv, int hours);       // 每分钟那份，最近 hours 小时（0 = 全部）
void eventsCsv(WebServer &sv);
String manualJson();                         // 所有手动调节：[{lux, b, tag, time}]，网页画在曲线旁边
String logStats();                           // 记了多少行、从什么时候开始
