// 几个模块都要用的东西：全局锁、时间、说明消息。
#pragma once
#include <Arduino.h>
#include <freertos/FreeRTOS.h>
#include <freertos/semphr.h>

// 全局锁：调节算法（brain）、设置、显示器状态都在它下面读写。
// 两个任务会碰这些数据：Arduino 的 loop（读传感器、网页）和调节任务（control.cpp，发 BetterDisplay 请求）。
// 拿着锁的时候不发网络请求，免得另一边干等。
extern SemaphoreHandle_t gLock;
struct Lock {
  Lock() { xSemaphoreTakeRecursive(gLock, portMAX_DELAY); }
  ~Lock() { xSemaphoreGiveRecursive(gLock); }
  Lock(const Lock &) = delete;
  Lock &operator=(const Lock &) = delete;
};
void commonBegin();

// 时间：mono() 是开机以来的秒数，只往前走，内部算平滑、间隔都用它；
// 墙上时间（epoch，NTP 对上以后才有）只用来给记录、学到的点标时间
double mono();
bool timeOk();
uint32_t epochNow();                       // 没对上时间是 0
String timeStr(uint32_t epoch);            // "2026-11-21 17:38:00"；0 写成 "-"
double sampleTime();                       // 网页曲线用的时间：对上了是 epoch（带小数），没对上是 mono()

// 说明消息：串口打一行，网页的日志框里也看得到（留最近 40 条）
void addMsg(const String &s);
uint32_t msgSeq();                         // 最新一条的编号
String msgsJson(uint32_t after);           // 编号比 after 大的那些，JSON 字符串数组
void printMsgs(Print &p);                  // 最近 40 条原样打出来（串口 m 命令：开机时的说明错过了也能看）

// 把串口命令（i / s）的输出接到 addMsg：一行一条
class MsgPrint : public Print {
 public:
  size_t write(uint8_t c) override;
 private:
  String line_;
};

// JSON 小工具
String jsonStr(const String &s);           // 带引号、转义
String jsonNum(float v, int digits = 4);   // NAN 写成 null
