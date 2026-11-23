// 板子上的网页（http://ambient.local）。页面在 ../web/ 里写，编译时压缩进固件（embed_web.py 生成 web_assets.h）：
//   /          monitor.html：三路读数、三台亮度、曲线和学到的点、自动开关
//   /settings  settings.html：改设置、看 Wi-Fi、下载记录、升级固件
//   /wifi      wifi.html：配 Wi-Fi（从配网热点连进来时 / 也是它）
// 网页每秒拉一次 /api/state?since= 。从配网热点连进来的设备只能用配 Wi-Fi 的那几个地址，别的一律 403
#pragma once
#include <Arduino.h>

void webBegin();
void webLoop();
void webAddSample(double t, const float v[3]);   // 给网页的读数曲线：最近 10 分钟（慢档）
