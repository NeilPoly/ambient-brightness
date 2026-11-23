// 调节算法：照度合成和平滑、照度 → 亮度的曲线、学到的点、各台显示器的状态。
// 规则见 ../README.md 的"调节规则"。
// 所有成员都在 gLock 下读写。
#pragma once
#include <Arduino.h>

#include "settings.h"

constexpr int MAX_LEARNED = 32;
constexpr double STALE_S = 10;   // 这么久没有新读数，就当作没有照度（慢档每秒一行）

struct Learned {
  float lux, b;
  uint32_t t;   // epoch，没对上时间是 0
};

struct DispState {
  float level = 0;      // 程序眼里这台现在的亮度：自动调节时每 0.1 秒往目标走一点（不取整）
  float sent = -1;      // 最后发给这台的亮度（1% 一格）；不调的时候跟着读数走
  double sentT = 0;     // 最后一次发命令的时间（mono）
  bool moving = false;  // 正在往目标调
  bool known = false;   // 读到过
  bool gone = false;    // 最近一轮读不到（拔了、没开）
  float bright = -1;    // 最近读到的亮度
};

class Brain {
 public:
  void begin();         // 读学到的点、自动开关

  // ---------- 照度 ----------
  void onSample(double t, const float v[3]);   // 一行新读数（t 是 mono）
  float lux(double now) const;                 // 平滑后的合成照度；STALE_S 秒没新读数就是 NAN
  float target(double now);                    // 实际曲线给的目标亮度；没照度是 NAN

  // ---------- 曲线 ----------
  float baseAt(float lux) const;               // 基础曲线（设置里的点）
  float effective(float lux);                  // 实际用的曲线 = 基础曲线 + 学到的点的修正
  void invalidate() { gridOk_ = false; }       // 设置或学到的点变了，下次重算

  // ---------- 学到的点 ----------
  void learn(float lux, float b, uint32_t t);
  void clearLearned();
  int nLearned = 0;
  Learned learned[MAX_LEARNED];

  // ---------- 显示器和开关 ----------
  DispState ds[MAX_DISP];
  String hold;          // 不是空的时候先不调，值是原因（显示器睡着、刚恢复、有一台读不到、连不上 Mac）
  bool autoOn = true;
  void setAuto(bool on);

 private:
  void saveLearned();
  void buildGrid();
  float smooth_ = NAN, xLast_ = NAN;   // 平滑后的合成照度（log10），是 smoothT_ 那一刻的值；最新一行的合成照度
  double smoothT_ = -1e9;
  static constexpr int GRID_N = 701;   // log10(照度) 从 -2 到 5，每 0.01 一格
  float grid_[GRID_N];
  bool gridOk_ = false;
};

extern Brain B;
