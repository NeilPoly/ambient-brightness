#include "control.h"

#include <WiFi.h>

#include <algorithm>

#include "bd.h"
#include "brain.h"
#include "common.h"
#include "logbook.h"
#include "settings.h"

namespace {
constexpr double TICK = 0.1;          // 每 0.1 秒算一次、往目标走一点
constexpr double READ_EVERY = 3;      // 每 3 秒读一次各台亮度
constexpr double GRACE_S = 10;        // 先不调的原因没了以后再等这么久（显示器刚醒时可能还没连好）
volatile BdState state = BdState::Unknown;

float clampf(float x, float lo, float hi) { return x < lo ? lo : x > hi ? hi : x; }

bool isNumber(const String &s) { return s.length() && (isDigit(s[0]) || s[0] == '-' || s[0] == '.'); }

// 读回来的亮度和我们发的（或上一次读到的）对不上：是你手动调的。
// 自动调节不暂停：把（当时的照度，你调到的亮度）记成学到的点，目标马上就是你调的值，不会和你抢。
// 调到 min～max 以外的按 min / max 记（自动最低 50%），这时自动会把它拉回边界。
// 调用时拿着 gLock。
void onManual(int i, float b, double now) {
  DispState &d = B.ds[i];
  const DisplayCfg &c = S.disp[i];
  d.level = d.sent = b;
  d.moving = false;
  float L = B.lux(now);
  float want = clampf(b - c.offset, S.minB, S.maxB);
  if (!isnan(L)) B.learn(L, want, epochNow());
  String note = fabsf(want - (b - c.offset)) < 0.005f ? "" : "，超出 " + String(S.minB, 2) + "～" + String(S.maxB, 2) + "，按 " + String(want, 2) + " 记";
  addMsg("手动调了 " + String(c.name) + " → " + String(b, 2) + "（当时照度 " + String(isnan(L) ? 0 : L, 2) + " lux），记成学到的点" + note);
  logEvent("manual:" + String(c.tag), L, b);
}

// 每 3 秒：读各台亮度（再看一眼显示器睡没睡），算出现在要不要先不调（hold），没在调的时候发现手动调节。
// 先不调的这段时间（和刚恢复的这一轮）：读到多少就当作起点，不算手动调节。
void readStep(double now) {
  static double okSince = -1e9;
  static bool wasOk = false;
  int n;
  DisplayCfg disp[MAX_DISP];
  bool sleepDdc;
  {
    Lock l;
    n = S.nDisp;
    memcpy(disp, S.disp, sizeof disp);
    sleepDdc = S.sleepDdc;
  }
  String hold;
  BdReply reads[MAX_DISP];
  bool asked = false;
  if (!WiFi.isConnected()) {
    hold = "Wi-Fi 没连上";
  } else if (!bdConfigured()) {
    hold = "还没填 Mac 的地址";
    state = BdState::NoHost;
  } else if (std::none_of(disp, disp + n, [](const DisplayCfg &d) { return d.enabled; })) {
    hold = "还没选要调哪台显示器";
  } else {
    asked = true;
    bool fail = false;
    for (int i = 0; i < n; i++) {
      reads[i] = bdGet("tagID=" + String(disp[i].tag) + "&brightness");
      if (reads[i].r == Bd::Fail) { fail = true; break; }
    }
    state = fail ? BdState::Fail : BdState::Ok;
    if (fail) {
      hold = "连不上 Mac 上的 BetterDisplay";
      asked = false;
    } else {
      // 显示器睡着时 BetterDisplay 的分组同步传不过去（2026-11-21 早上就因为这个三台差开了），所以睡着时不调。
      // 判断办法：第一台要调的显示器的 DDC 电源状态（VCP 0xD6），1 是开着，别的数（待机、关）算睡着。
      // 读不到（BetterDisplay 回 404）也算睡着，但要连着两次：2026-11-21 晚上实测，睡着的 3 个多小时里每次都是 404、
      // 一次也没读到过数；醒着时也偶尔 404（约 2%），只一次就停会时不时卡 13 秒。
      // 当初写成"读不到不算"，结果 22:13 关灯时显示器睡着，板子照样把 U3223QE 从 60% 调到 50%，两台 U2518D 收不到同步，留在 60%
      static int d6Miss = 0;
      int first = -1;
      for (int i = 0; i < n; i++) if (disp[i].enabled) { first = i; break; }
      if (sleepDdc && first >= 0 && reads[first].r == Bd::Ok) {
        BdReply p = bdGet("tagID=" + String(disp[first].tag) + "&ddc&vcp=0xD6");
        if (p.r == Bd::Ok && isNumber(p.body)) {
          d6Miss = 0;
          if (p.body.toInt() != 1) hold = "显示器睡着";
        } else if (p.r == Bd::Fail) {
          hold = "连不上 Mac 上的 BetterDisplay";
        } else if (++d6Miss >= 2) {
          hold = "显示器睡着";
        }
      }
      if (hold.isEmpty()) {
        String missing;
        for (int i = 0; i < n; i++) if (reads[i].r == Bd::NotFound) missing += (missing.isEmpty() ? "" : "、") + String(disp[i].name);
        if (!missing.isEmpty()) hold = "读不到 " + missing;
      }
    }
  }
  static String lastReason = "-";
  if (hold != lastReason && !hold.isEmpty()) addMsg("先不调：" + hold);
  lastReason = hold;
  if (hold.isEmpty()) {
    if (!wasOk) { okSince = now; wasOk = true; }
    if (now - okSince < GRACE_S) hold = "刚恢复，等一下";
  } else {
    wasOk = false;
  }

  Lock l;
  bool prevHold = !B.hold.isEmpty();
  if (hold.isEmpty() && prevHold) addMsg("显示器都在，接着调");
  for (int i = 0; i < n && asked; i++) {
    DispState &d = B.ds[i];
    if (reads[i].r == Bd::NotFound) { d.gone = true; continue; }
    if (reads[i].r != Bd::Ok || !isNumber(reads[i].body)) continue;
    float b = reads[i].body.toFloat();
    d.gone = false;
    d.bright = b;
    if (!hold.isEmpty() || prevHold || !d.known || !disp[i].enabled) {
      // 先不调的时候、刚恢复的这一轮、第一次读到、不归程序调的那几台（分组同步会带着它们变）：只记读数
      d.level = d.sent = b;
      d.known = true;
      d.moving = false;
    } else if (d.moving || now - d.sentT < 2) {
      // 正在调、或者刚发过命令 2 秒内的那台不比：读回来的可能还是旧值
    } else if (fabsf(b - d.sent) > 0.015f) {
      onManual(i, b, now);
    }
  }
  B.hold = hold;
}

// 每 0.1 秒：目标亮度连续地算；各台从现在的亮度往目标走，每秒最多 rate，按 1% 一格发给显示器。
// 差到 deadband 才开始走，走起来就一直跟到离目标不到 0.2%（中途目标还在变也一直跟着，不走走停停）。
void controlStep(double now, double dt) {
  static double failUntil = 0;   // 发命令连不上以后歇 3 秒（等下一轮读亮度把 hold 设上），不要每 0.1 秒都等一次超时
  struct Cmd { int i, tag; float v; } cmds[MAX_DISP];
  int nc = 0;
  {
    Lock l;
    float target = B.target(now);
    bool go = B.autoOn && !isnan(target) && B.hold.isEmpty();
    for (int i = 0; i < S.nDisp; i++) {
      const DisplayCfg &c = S.disp[i];
      DispState &d = B.ds[i];
      if (!d.known || d.gone || !go || !c.enabled) { d.moving = false; continue; }
      float want = clampf(target + c.offset, S.minB, S.maxB);
      float diff = want - d.level;
      if (!d.moving && fabsf(diff) >= S.deadband) d.moving = true;
      if (d.moving) {
        float step = S.rate * dt;
        d.level += clampf(diff, -step, step);
        if (fabsf(want - d.level) < 0.002f) d.moving = false;
      }
      float v = roundf(d.level * 100) / 100;
      if (fabsf(v - d.sent) > 0.001f) cmds[nc++] = {i, c.tag, v};
    }
  }
  if (now < failUntil) return;
  for (int k = 0; k < nc; k++) {
    BdReply r = bdSet("tagID=" + String(cmds[k].tag) + "&brightness=" + String(cmds[k].v, 2));
    Lock l;
    if (r.r == Bd::Ok) {
      B.ds[cmds[k].i].sent = cmds[k].v;
      B.ds[cmds[k].i].sentT = mono();
    } else if (r.r == Bd::Fail) {
      failUntil = mono() + 3;
      break;
    }
  }
}

void task(void *) {
  double lastRead = -1e9, last = mono();
  for (;;) {
    vTaskDelay(pdMS_TO_TICKS((uint32_t)(TICK * 1000)));
    double now = mono();
    if (now - lastRead >= READ_EVERY) {
      lastRead = now;
      readStep(now);
      now = mono();
    }
    double dt = now - last;
    last = now;
    controlStep(now, dt > 0.5 ? 0.5 : dt);
  }
}
}  // namespace

void controlBegin() { xTaskCreate(task, "control", 12288, nullptr, 1, nullptr); }

BdState bdState() { return state; }
