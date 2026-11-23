// 设置：曲线、上下限、平滑和调节速度、三路权重、调哪几台、Mac 的地址。
// 存在闪存（NVS）里，网页的设置页改了保存就生效。
#pragma once
#include <Arduino.h>

constexpr int MAX_PTS = 8;    // 基础曲线最多几个点
constexpr int MAX_DISP = 4;   // 最多几台显示器

struct DisplayCfg {
  int16_t tag;          // BetterDisplay 里的编号（tagID）
  char name[24];
  float offset;         // 这台在目标亮度上再加减多少
  bool enabled;         // false：不归程序调（靠 BetterDisplay 的分组同步跟着走），也不拿来判断手动调节
};

struct Settings {
  uint16_t version;
  // 基础曲线：照度 → 亮度的几个点，照度从低到高；点和点之间按 log(照度) 连直线，两头以外保持不变
  uint8_t nPts;
  float ptLux[MAX_PTS], ptB[MAX_PTS];
  float minB, maxB;       // 亮度最低、最高
  float smoothS;          // 照度平滑的时间常数（秒）
  float deadband;         // 差到这么多才开始调，调起来就一直跟到目标
  float rate;             // 每秒最多变化多少
  float w[3];             // A、B、C 三路的权重（对 log 照度加权平均）
  float sigma, replace;   // 学到的点：影响范围、多近算替换（都以"差几个十倍照度"计）
  uint8_t nDisp;
  DisplayCfg disp[MAX_DISP];
  char host[64];          // Mac 的地址：IP，或者 xxx.local（mDNS 名字）
  uint16_t port;          // BetterDisplay 的 HTTP 端口，默认 55777
  char token[48];         // BetterDisplay 设置里的 token，没设就空着
  bool sleepDdc;          // 用第一台要调的显示器的 DDC 电源状态（VCP 0xD6）判断显示器睡没睡
  bool fastDefault;       // 开机用快档采样
  char tz[40];            // 时区（POSIX 写法），中国 "CST-8"、日本 "JST-9"
  char orientation[16];   // 方块的摆法："前上右" 或 "前上左"，只用来在网页上标名字
};

extern Settings S;   // 当前设置；读写都要拿 gLock

void settingsBegin();                 // 从闪存读，没有就用默认值
void settingsSave();
void settingsDefaults(Settings &s);
String settingsJson();
// 网页设置页提交的表单：检查、改进 S、存起来；返回空字符串表示成功，否则是哪里不对
class WebServer;
String settingsFromForm(WebServer &server);
