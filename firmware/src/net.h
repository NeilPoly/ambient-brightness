// Wi-Fi：
//   存了家里的 Wi-Fi 就连（ESP32-C3 只能连 2.4 GHz）；连上以后用 mDNS 起名 ambient（http://ambient.local），再用 NTP 对时间。
//   没存过 Wi-Fi：马上开一个热点 "Ambient-xxxx"，不用密码（第一次配网没法先知道密码）。
//   存过、但连不上超过 10 分钟：开同名热点，要密码。密码是第一次开机时随机生成的 8 位数字，存在板子里，
//   在家里网络上打开设置页能看到，USB 串口开机时也会打出来。
//   手机连上热点会弹出配网页（也可以开 http://192.168.4.1），选 Wi-Fi、输密码，存进板子，重启以后去连。
//   热点开着的时候也一直在试着连家里的 Wi-Fi，连上 2 分钟后热点自己关掉。
//   从热点连进来的设备只能配 Wi-Fi：改设置、传固件、下载记录都不行（见 web.cpp 的 viaAp）。
// Wi-Fi 密码只存在板子的闪存里，不写在代码里。
#pragma once
#include <Arduino.h>

void netBegin();
void netLoop();
bool apActive();
String apName();
String apPassword();                                 // 配网热点的密码（存过 Wi-Fi 以后开的热点才要）
String wifiJson();                                   // 现在连的哪个 Wi-Fi、信号、地址
String scanJson();                                   // 扫一遍附近的 Wi-Fi（要 2～3 秒）
void netSave(const String &ssid, const String &pass);   // 存起来（调用方接着重启）
void netForget();                                    // 忘掉存的 Wi-Fi（调用方接着重启，开机就开热点）
void applyTz();                                      // 设置里的时区改了以后调
