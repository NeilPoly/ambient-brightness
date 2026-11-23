// 环境光自动调亮度（Ambient）：
//   三块 OPT3001 读照度（sensors.cpp）→ 按曲线算目标亮度、学你手动调的点（brain.cpp）
//   → 通过 Wi-Fi 调 Mac 上 BetterDisplay 的 HTTP 接口（bd.cpp、control.cpp）。
//   管理和设置在板子的网页上：http://ambient.local（web.cpp）。记录存在板子闪存里，网页上能下载（logbook.cpp）。
//
// USB 串口每行输出 "lux A=.. B=.. C=.."（双击打开 web/monitor.html 能直接读串口看曲线，排查问题用），
// 说明消息以 # 开头；往串口发 i（每块板的情况）、s（扫地址）、f / n（快档 / 慢档）、m（再打一遍最近 40 条说明）。
//
// 省电：蓝牙没启动（Arduino 不用就不开）；主频 80 MHz（Wi-Fi 最低要 80 MHz）；Wi-Fi 用默认的省电模式（空闲时关射频）。
// 两次读数之间 delay，让 CPU 歇着，不空转。

#include <Arduino.h>

#include "brain.h"
#include "common.h"
#include "control.h"
#include "logbook.h"
#include "net.h"
#include "sensors.h"
#include "settings.h"
#include "web.h"

void setup() {
  Serial.begin(115200);
  Serial.setTxTimeoutMs(0);   // Mac 没在读的时候不要卡住
  setCpuFrequencyMhz(80);     // 80 MHz 足够；再低 Wi-Fi 和 USB 串口都不行
  commonBegin();
  settingsBegin();
  applyTz();
  sensors::begin();
  sensors::setFast(S.fastDefault);
  B.begin();
  addMsg("开机：固件 " + String(FW_VERSION) + "，自动调节" + (B.autoOn ? "开着" : "关着") + "，学到的点 " + B.nLearned + " 个");
  logBegin();
  netBegin();
  webBegin();
  controlBegin();
}

void loop() {
  static uint32_t next = 0;
  while (Serial.available()) {
    int c = Serial.read();
    if (c == 'i') sensors::printInfo(Serial);
    if (c == 's') sensors::scan(Serial);
    if (c == 'm') printMsgs(Serial);
    if (c == 'f' || c == 'n') {
      sensors::setFast(c == 'f');
      sensors::printMode(Serial);
    }
  }
  netLoop();
  webLoop();
  logTick();

  int32_t wait = (int32_t)(next - millis());
  if (wait > 0) {
    delay(wait < 5 ? wait : 5);   // 歇着等下一次；最多歇 5 ms 就回来看一眼网页和串口
    return;
  }
  next = millis() + sensors::periodMs();

  float v[3];
  sensors::readAll(v);
  char line[64];
  int n = snprintf(line, sizeof line, "lux");
  const char names[3] = {'A', 'B', 'C'};
  for (int i = 0; i < 3; i++) {
    if (v[i] < 0) n += snprintf(line + n, sizeof line - n, " %c=-", names[i]);
    else n += snprintf(line + n, sizeof line - n, " %c=%.2f", names[i], v[i]);
  }
  Serial.println(line);
  {
    Lock l;
    B.onSample(mono(), v);
  }
  webAddSample(sampleTime(), v);
  logSample(v);
}
