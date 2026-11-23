// 调节任务：每 0.1 秒往目标走一点、发给 BetterDisplay；每 3 秒读一次各台亮度，顺便发现你手动调了。
// 单独一个 FreeRTOS 任务，网络请求慢的时候（Mac 不在、超时）不耽误读传感器和网页。
#pragma once
#include <Arduino.h>

enum class BdState { Unknown, Ok, Fail, NoHost };

void controlBegin();
BdState bdState();   // 最近一轮读亮度的结果，给网页显示
