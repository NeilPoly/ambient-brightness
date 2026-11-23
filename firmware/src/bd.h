// BetterDisplay 的 HTTP 接口（Mac 上 BetterDisplay 设置 → Application → Integration 打开，默认端口 55777）。
// 一次请求：http://<Mac>:55777/get?tagID=4&brightness → "0.75"；set 成功回 200、空内容；显示器不在回 404 "Failed."。
// 2026-11-21 实测：BetterDisplay 在所有网卡上听（*:55777），局域网能访问；set 约 15 ms，get 约 3 ms，DDC 读约 0.1 秒。
// 只在调节任务里调用（会等网络），调用时不要拿着 gLock。
#pragma once
#include <Arduino.h>

enum class Bd { Ok, NotFound, Fail };   // Fail：连不上（Mac 不在、睡了、地址不对、BetterDisplay 没开接口）

struct BdReply {
  Bd r;
  String body;
};

BdReply bdGet(const String &query);   // query 例："tagID=4&brightness"
BdReply bdSet(const String &query);   // query 例："tagID=4&brightness=0.75"
String bdWhere();                     // 现在连的是哪：给网页显示，比如 "MacBook-Pro.local（192.168.1.20）"
bool bdConfigured();                  // 设置里填了 Mac 的地址没有
