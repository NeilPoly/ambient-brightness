#include "logbook.h"

#include <LittleFS.h>
#include <WebServer.h>

#include "brain.h"
#include "common.h"
#include "settings.h"

namespace {
constexpr int REC_N = 7 * 24 * 60;    // 7 天，每分钟一行
constexpr int BUF_N = 10;             // 攒 10 行写一次
constexpr const char *MIN_PATH = "/minutes.bin";
constexpr const char *EV_PATH = "/events.csv";
constexpr const char *EV_OLD = "/events.old.csv";
constexpr size_t EV_MAX = 48 * 1024;  // 事件文件超过这么大就换一个新的，旧的留一份

// 每分钟一行，28 字节。亮度按 0.5% 一格存成 0～200，255 是没有
struct Rec {
  uint32_t t;            // 这一分钟开始的 epoch
  float a, b, c, lux;    // 读不到是 -1
  uint8_t target, d[3];
  uint8_t flags;         // 1：自动开着；2：当时先不调（hold）
  uint8_t pad[3];
};
static_assert(sizeof(Rec) == 28, "Rec 大小变了，旧记录会读乱");

bool fsOk = false;
int next = 0;            // 下一行写在第几格
int count = 0;           // 一共有几行（满了就是 REC_N）
Rec buf[BUF_N];
int nBuf = 0;
uint32_t curMin = 0;     // 正在攒的是哪一分钟（epoch / 60）
double sum[3] = {0, 0, 0};
int cnt[3] = {0, 0, 0};

uint8_t pct(float v) { return isnan(v) || v < 0 ? 255 : (uint8_t)lroundf(fminf(v, 1) * 200); }
String unpct(uint8_t v) { return v == 255 ? "" : String(v / 200.0f, 3); }
String num(float v) { return v < 0 || isnan(v) ? "" : String(v, 2); }

void findNext() {
  File f = LittleFS.open(MIN_PATH, "r");
  if (!f) { next = count = 0; return; }
  size_t n = f.size() / sizeof(Rec);
  if (n < (size_t)REC_N) {
    next = count = n;
  } else {
    // 满了：找时间最新的那一行，下一格就是最旧的
    count = REC_N;
    uint32_t best = 0;
    int bestI = 0;
    Rec chunk[32];
    for (int i = 0; i < REC_N; i += 32) {
      int k = f.read((uint8_t *)chunk, sizeof chunk) / sizeof(Rec);
      for (int j = 0; j < k; j++) if (chunk[j].t >= best) { best = chunk[j].t; bestI = i + j; }
      if (k < 32) break;
    }
    next = (bestI + 1) % REC_N;
  }
  f.close();
}

void writeBuf() {
  if (!fsOk || !nBuf) return;
  File f = LittleFS.exists(MIN_PATH) ? LittleFS.open(MIN_PATH, "r+") : LittleFS.open(MIN_PATH, "w");
  if (!f) return;
  for (int i = 0; i < nBuf; i++) {
    f.seek(next * sizeof(Rec));
    f.write((const uint8_t *)&buf[i], sizeof(Rec));
    next = (next + 1) % REC_N;
    if (count < REC_N) count++;
  }
  f.close();
  nBuf = 0;
}

void closeMinute() {
  bool any = cnt[0] || cnt[1] || cnt[2];
  if (any) {
    Rec r{};
    r.t = curMin * 60;
    float *v[3] = {&r.a, &r.b, &r.c};
    for (int i = 0; i < 3; i++) *v[i] = cnt[i] ? sum[i] / cnt[i] : -1;
    {
      Lock l;
      double now = mono();
      float L = B.lux(now);
      r.lux = isnan(L) ? -1 : L;
      r.target = pct(B.target(now));
      for (int i = 0; i < 3; i++) {
        const DispState &d = B.ds[i];
        r.d[i] = (i >= S.nDisp || d.gone || !d.known) ? 255 : pct(d.sent);
      }
      r.flags = (B.autoOn ? 1 : 0) | (B.hold.isEmpty() ? 0 : 2);
    }
    buf[nBuf++] = r;
    if (nBuf >= BUF_N) writeBuf();
  }
  for (int i = 0; i < 3; i++) { sum[i] = 0; cnt[i] = 0; }
}

void streamFile(WebServer &sv, const char *path) {
  File f = LittleFS.open(path, "r");
  if (!f) return;
  char chunk[1024];
  for (;;) {
    int k = f.read((uint8_t *)chunk, sizeof chunk - 1);
    if (k <= 0) break;
    chunk[k] = 0;
    sv.sendContent(chunk, k);
  }
  f.close();
}
}  // namespace

void logBegin() {
  fsOk = LittleFS.begin(true);   // 第一次用（或者坏了）就格式化
  if (fsOk) findNext();
  addMsg(fsOk ? "记录：已有 " + String(count) + " 行（每分钟一行，最多 7 天）" : "记录：闪存文件系统打不开，不记了");
}

void logSample(const float v[3]) {
  for (int i = 0; i < 3; i++) if (v[i] >= 0) { sum[i] += v[i]; cnt[i]++; }
}

void logTick() {
  if (!timeOk()) return;
  uint32_t m = epochNow() / 60;
  if (m == curMin) return;
  if (curMin) closeMinute();
  else for (int i = 0; i < 3; i++) { sum[i] = 0; cnt[i] = 0; }   // 刚对上时间：这一分钟不完整，不记
  curMin = m;
}

void logFlush() { writeBuf(); }

void logEvent(const String &ev, float lux, float b) {
  if (!fsOk) return;
  File f = LittleFS.open(EV_PATH, "a");
  if (!f) return;
  if (f.size() == 0) f.print("time,event,lux,b\n");
  f.printf("%s,%s,%s,%s\n", timeStr(epochNow()).c_str(), ev.c_str(), num(lux).c_str(), b < 0 || isnan(b) ? "" : String(b, 3).c_str());
  bool big = f.size() > EV_MAX;
  f.close();
  if (big) {
    LittleFS.remove(EV_OLD);
    LittleFS.rename(EV_PATH, EV_OLD);
  }
}

void logCsv(WebServer &sv, int hours) {
  logFlush();
  String names[3];
  {
    Lock l;
    for (int i = 0; i < 3; i++) names[i] = i < S.nDisp ? "b" + String(S.disp[i].tag) : "b" + String(i);
  }
  sv.setContentLength(CONTENT_LENGTH_UNKNOWN);
  sv.sendHeader("Content-Disposition", "attachment; filename=\"ambient-minutes.csv\"");
  sv.send(200, "text/csv; charset=utf-8", "");
  sv.sendContent("time,A,B,C,lux,target," + names[0] + "," + names[1] + "," + names[2] + ",auto,hold\n");
  File f = LittleFS.open(MIN_PATH, "r");
  if (f) {
    uint32_t since = hours > 0 && timeOk() ? epochNow() - hours * 3600 : 0;
    int start = count < REC_N ? 0 : next;
    String out;
    for (int k = 0; k < count; k++) {
      Rec r;
      f.seek(((start + k) % REC_N) * sizeof(Rec));
      if (f.read((uint8_t *)&r, sizeof r) != sizeof r) break;
      if (r.t < since) continue;
      out += timeStr(r.t) + "," + num(r.a) + "," + num(r.b) + "," + num(r.c) + "," + num(r.lux) + "," + unpct(r.target) + ","
           + unpct(r.d[0]) + "," + unpct(r.d[1]) + "," + unpct(r.d[2]) + "," + (r.flags & 1) + "," + ((r.flags & 2) ? 1 : 0) + "\n";
      if (out.length() > 1500) { sv.sendContent(out); out = ""; }
    }
    if (out.length()) sv.sendContent(out);
    f.close();
  }
  sv.sendContent("");
}

void eventsCsv(WebServer &sv) {
  sv.setContentLength(CONTENT_LENGTH_UNKNOWN);
  sv.sendHeader("Content-Disposition", "attachment; filename=\"ambient-events.csv\"");
  sv.send(200, "text/csv; charset=utf-8", "");
  if (LittleFS.exists(EV_OLD)) streamFile(sv, EV_OLD);
  streamFile(sv, EV_PATH);
  sv.sendContent("");
}

String manualJson() {
  String out = "[";
  bool first = true;
  for (const char *path : {EV_OLD, EV_PATH}) {
    File f = LittleFS.open(path, "r");
    if (!f) continue;
    while (f.available()) {
      String line = f.readStringUntil('\n');
      // time,manual:4,46.70,0.750
      int c1 = line.indexOf(','), c2 = line.indexOf(',', c1 + 1), c3 = line.indexOf(',', c2 + 1);
      if (c1 < 0 || c2 < 0 || c3 < 0 || !line.substring(c1 + 1, c2).startsWith("manual:")) continue;
      String lux = line.substring(c2 + 1, c3), b = line.substring(c3 + 1);
      b.trim();
      if (lux.isEmpty() || b.isEmpty()) continue;
      out += String(first ? "" : ",") + "{\"lux\":" + lux + ",\"b\":" + b + ",\"tag\":" + jsonStr(line.substring(c1 + 8, c2))
           + ",\"time\":" + jsonStr(line.substring(0, c1)) + "}";
      first = false;
    }
    f.close();
  }
  return out + "]";
}

String logStats() {
  return String(count) + " 行（最多 " + String(REC_N) + "），另有 " + String(nBuf) + " 行还没写进闪存";
}
