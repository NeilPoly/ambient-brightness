// 三块 OPT3001 照度传感器（软件 I2C），见 sensors.cpp 开头的说明。
#pragma once
#include <Arduino.h>

namespace sensors {

void begin();
bool fast();                 // 现在是不是快档（每次积分 100 ms）
void setFast(bool on);       // 换档：配置在下次读的时候发现对不上就重写
uint32_t periodMs();         // 慢档 1000、快档 100
void readAll(float out[3]);  // 读三块板的照度（lux），读不到是 -1
void printMode(Print &p);
void printInfo(Print &p);    // 每块板的情况：上次应答的地址、厂商号、型号、配置、换地址重试的次数
void scan(Print &p);         // 排查用：每块板把四个地址各读 50 次，数各地址应答了几次

}  // namespace sensors
