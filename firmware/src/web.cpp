#include "web.h"

#include <Update.h>
#include <WebServer.h>
#include <WiFi.h>

#include "bd.h"
#include "brain.h"
#include "common.h"
#include "control.h"
#include "logbook.h"
#include "net.h"
#include "sensors.h"
#include "settings.h"
#include "web_assets.h"   // embed_web.py 编译前生成

namespace {
WebServer sv(80);

// 读数曲线：最近 600 行（慢档 10 分钟、快档 1 分钟）。只在 loop 里读写，不用锁
constexpr int KEEP = 600;
struct Sample { double t; float v[3]; };
Sample ring[KEEP];
int ringN = 0, ringHead = 0;   // ringHead 是下一行写的位置

void sendGz(const uint8_t *data, size_t len) {
  sv.sendHeader("Content-Encoding", "gzip");
  sv.sendHeader("Cache-Control", "no-cache");
  sv.send_P(200, "text/html; charset=utf-8", (const char *)data, len);
}

void sendJson(const String &s) {
  sv.sendHeader("Cache-Control", "no-store");
  sv.send(200, "application/json", s);
}

// 这个请求是不是从配网热点那边进来的（看它连的是板子的哪个地址）。
// 热点要么没密码（第一次配网），要么密码就贴在设置页上，所以从热点进来的设备只给配 Wi-Fi：
// 不然附近的人连上热点就能改设置、传固件
bool viaAp() { return apActive() && sv.client().localIP() == WiFi.softAPIP(); }

// 热点上不给用的地址：回 403 并返回 true，调用的地方直接 return
bool apDenied() {
  if (!viaAp()) return false;
  sv.send(403, "text/plain; charset=utf-8", "热点上只能配 Wi-Fi");
  return true;
}

// 改东西的请求（POST）只接受从板子自己的网页发来的：别的网站在你的浏览器里偷偷发过来的不理
bool sameOrigin() {
  String o = sv.header("Origin");
  if (o.isEmpty()) return true;
  return o == "http://" + sv.hostHeader();
}

String sampleJson(const Sample &s) {
  const char *k[3] = {"A", "B", "C"};
  String o = "{\"t\":" + String(s.t, 3);
  for (int i = 0; i < 3; i++) o += String(",\"") + k[i] + "\":" + (s.v[i] < 0 ? String("null") : jsonNum(s.v[i], 5));
  return o + "}";
}

String samplesJson(double since) {
  String o = "[";
  bool first = true;
  for (int k = 0; k < ringN; k++) {
    const Sample &s = ring[(ringHead - ringN + k + KEEP) % KEEP];
    // 发给网页的时间只到毫秒（四舍五入），网页拿它当 since 再要：差 1 毫秒以内的算同一行，不然会重复
    if (s.t <= since + 0.001) continue;
    if (!first) o += ',';
    o += sampleJson(s);
    first = false;
  }
  return o + "]";
}

// 网页要的全部状态：连没连上 BetterDisplay、照度、各台亮度、曲线和学到的点、最新的读数和说明
String stateJson(double since, uint32_t mseq) {
  String o;
  o.reserve(6000);
  BdState st = bdState();
  String where = bdWhere();
  bool sensorsDead = ringN && ring[(ringHead - 1 + KEEP) % KEEP].v[0] < 0 && ring[(ringHead - 1 + KEEP) % KEEP].v[1] < 0 && ring[(ringHead - 1 + KEEP) % KEEP].v[2] < 0;
  String status;
  bool ok = false;
  if (st == BdState::NoHost || !bdConfigured()) status = "还没填 Mac 的地址：在设置里填";
  else if (st == BdState::Ok) { status = "已连上 Mac 上的 BetterDisplay：" + where; ok = true; }
  else if (st == BdState::Fail) status = "连不上 Mac 上的 BetterDisplay：" + where;
  else status = "正在连 Mac：" + where;
  if (sensorsDead) { status += "；三块传感器都读不到"; ok = false; }
  o += "{\"status_text\":" + jsonStr(status) + ",\"status_ok\":" + (ok ? "true" : "false");
  {
    Lock l;
    double now = mono();
    float L = B.lux(now), T = B.target(now);
    o += ",\"lux\":" + jsonNum(L, 5) + ",\"target\":" + jsonNum(T);
    o += ",\"auto\":" + String(B.autoOn ? "true" : "false");
    String hold = !B.hold.isEmpty() ? B.hold : isnan(L) ? String("没有照度读数") : String("");
    o += ",\"hold\":" + (hold.isEmpty() ? String("null") : jsonStr(hold));
    o += ",\"displays\":[";
    for (int i = 0; i < S.nDisp; i++) {
      const DisplayCfg &c = S.disp[i];
      const DispState &d = B.ds[i];
      float b = (d.gone || !d.known) ? NAN : d.sent;
      o += String(i ? "," : "") + "{\"tag\":\"" + c.tag + "\",\"name\":" + jsonStr(c.name) + ",\"enabled\":" + (c.enabled ? "true" : "false")
         + ",\"offset\":" + jsonNum(c.offset) + ",\"brightness\":" + jsonNum(b) + "}";
    }
    o += "],\"curve\":[";
    for (int i = 0; i < S.nPts; i++) o += String(i ? "," : "") + "[" + jsonNum(S.ptLux[i], 6) + "," + jsonNum(S.ptB[i]) + "]";
    o += "],\"learned\":[";
    for (int i = 0; i < B.nLearned; i++)
      o += String(i ? "," : "") + "{\"lux\":" + jsonNum(B.learned[i].lux, 6) + ",\"b\":" + jsonNum(B.learned[i].b) + ",\"time\":" + jsonStr(timeStr(B.learned[i].t)) + "}";
    // 实际曲线给网页画：每 0.05 个十倍取一个点（0.01～10 万 lux）
    o += "],\"effective\":[";
    for (int k = 0; k <= 140; k++) {
      float x = powf(10, -2 + 0.05f * k);
      o += String(k ? "," : "") + "[" + jsonNum(x, 4) + "," + jsonNum(B.effective(x)) + "]";
    }
    o += "],\"weights\":{\"A\":" + jsonNum(S.w[0]) + ",\"B\":" + jsonNum(S.w[1]) + ",\"C\":" + jsonNum(S.w[2]) + "}";
    o += ",\"orientation\":" + jsonStr(S.orientation);
    o += ",\"control\":{\"min\":" + jsonNum(S.minB) + ",\"max\":" + jsonNum(S.maxB) + ",\"smoothing_s\":" + jsonNum(S.smoothS)
       + ",\"deadband\":" + jsonNum(S.deadband) + ",\"rate_per_s\":" + jsonNum(S.rate) + "}";
  }
  o += ",\"fast\":" + String(sensors::fast() ? "true" : "false");
  o += ",\"samples\":" + samplesJson(since);
  o += ",\"msgs\":" + msgsJson(mseq) + ",\"mseq\":" + String(msgSeq());
  o += ",\"wifi\":" + wifiJson();
  o += ",\"now\":" + String(sampleTime(), 3) + ",\"uptime\":" + String((uint32_t)mono()) + ",\"heap\":" + String(ESP.getFreeHeap());
  return o + "}";
}

// 开着热点的时候把每个网页请求记进说明里：配网出问题时看得出手机到底有没有请求到板子
void noteRequest() {
  if (apActive()) addMsg("网页请求 " + sv.hostHeader() + sv.uri() + "（来自 " + sv.client().remoteIP().toString() + "）");
}

void handleRoot() {
  noteRequest();
  // 从配网热点连进来的：看到的就是配网页
  if (viaAp()) return sendGz(WIFI_HTML_GZ, WIFI_HTML_GZ_LEN);
  sendGz(MONITOR_HTML_GZ, MONITOR_HTML_GZ_LEN);
}

void handleNotFound() {
  noteRequest();
  // 热点模式下手机系统会访问各种"检查有没有网"的地址：一律转到配网页，手机就会弹出它
  if (viaAp() && sv.hostHeader() != WiFi.softAPIP().toString()) {
    sv.sendHeader("Location", "http://" + WiFi.softAPIP().toString() + "/", true);
    sv.send(302, "text/plain", "");
    return;
  }
  sv.send(404, "text/plain", "没有这个地址");
}

void handleCmd() {
  if (apDenied()) return;
  if (!sameOrigin()) return sv.send(403, "text/plain", "");
  String c = sv.arg("c");
  MsgPrint mp;
  if (c == "i") sensors::printInfo(mp);
  else if (c == "s") sensors::scan(mp);
  else if (c == "f" || c == "n") { sensors::setFast(c == "f"); sensors::printMode(mp); }
  sendJson("{\"ok\":true}");
}

void handleAuto() {
  if (apDenied()) return;
  if (!sameOrigin()) return sv.send(403, "text/plain", "");
  bool on = sv.arg("on") == "1";
  {
    Lock l;
    B.setAuto(on);
  }
  addMsg(on ? "开了自动调节" : "关了自动调节");
  logEvent(on ? "auto:on" : "auto:off", NAN, NAN);
  sendJson("{\"ok\":true}");
}

void handleClearLearned() {
  if (apDenied()) return;
  if (!sameOrigin()) return sv.send(403, "text/plain", "");
  {
    Lock l;
    B.clearLearned();
  }
  addMsg("清掉了学到的点");
  logEvent("learned:clear", NAN, NAN);
  sendJson("{\"ok\":true}");
}

void handleSettingsPost() {
  if (apDenied()) return;
  if (!sameOrigin()) return sv.send(403, "text/plain", "");
  bool fastBefore;
  {
    Lock l;
    fastBefore = S.fastDefault;
  }
  String err = settingsFromForm(sv);
  if (!err.isEmpty()) return sv.send(400, "application/json", "{\"error\":" + jsonStr(err) + "}");
  {
    Lock l;
    B.invalidate();
    if (S.fastDefault != fastBefore) sensors::setFast(S.fastDefault);
  }
  applyTz();
  addMsg("改了设置");
  sendJson("{\"ok\":true}");
}

void handleWifiPost() {
  if (!sameOrigin()) return sv.send(403, "text/plain", "");
  String ssid = sv.arg("ssid");
  if (ssid.isEmpty()) return sv.send(400, "application/json", "{\"error\":\"没选 Wi-Fi\"}");
  netSave(ssid, sv.arg("pass"));
  sendJson("{\"ok\":true}");
  addMsg("存了 Wi-Fi " + ssid + "，重启去连");
  logFlush();
  delay(800);
  ESP.restart();
}

void handleReboot() {
  if (apDenied()) return;
  if (!sameOrigin()) return sv.send(403, "text/plain", "");
  if (sv.arg("forget") == "1") netForget();
  sendJson("{\"ok\":true}");
  logFlush();
  delay(500);
  ESP.restart();
}

// 升级固件：设置页上传 .pio/build/c3zero/firmware.bin，写进另一半闪存，成功了重启换过去；失败就还用原来的
void handleUpdateDone() {
  if (apDenied()) return;
  bool ok = Update.isFinished() && !Update.hasError();
  sv.send(ok ? 200 : 500, "application/json", ok ? "{\"ok\":true}" : "{\"error\":" + jsonStr(Update.errorString()) + "}");
  if (ok) {
    logFlush();
    delay(800);
    ESP.restart();
  }
}

void handleUpdateUpload() {
  HTTPUpload &up = sv.upload();
  if (up.status == UPLOAD_FILE_START) {
    if (viaAp() || !sameOrigin()) return;   // 热点上不收固件（收尾时回 403）
    addMsg("开始升级固件：" + up.filename);
    logFlush();
    if (!Update.begin(UPDATE_SIZE_UNKNOWN)) addMsg("升级开不了头：" + String(Update.errorString()));
  } else if (up.status == UPLOAD_FILE_WRITE) {
    if (Update.isRunning() && Update.write(up.buf, up.currentSize) != up.currentSize) addMsg("升级写入失败：" + String(Update.errorString()));
  } else if (up.status == UPLOAD_FILE_END) {
    if (Update.end(true)) addMsg("固件收完了（" + String(up.totalSize) + " 字节），重启");
    else addMsg("升级没成功：" + String(Update.errorString()));
  } else if (up.status == UPLOAD_FILE_ABORTED) {
    Update.abort();
    addMsg("升级中断了");
  }
}
}  // namespace

void webAddSample(double t, const float v[3]) {
  // 刚对上时间：之前的读数是按开机秒数记的，和之后的对不上，清掉
  if (ringN && t > 1e9 && ring[(ringHead - 1 + KEEP) % KEEP].t < 1e9) ringN = 0;
  Sample &s = ring[ringHead];
  s.t = t;
  memcpy(s.v, v, sizeof s.v);
  ringHead = (ringHead + 1) % KEEP;
  if (ringN < KEEP) ringN++;
}

void webBegin() {
  const char *keys[] = {"Origin"};
  sv.collectHeaders(keys, 1);
  sv.on("/", HTTP_GET, handleRoot);
  // 从热点进来只能用：/、/wifi、/api/wifi、/api/scan（和把别的地址转到配网页）。其他地址都先过 apDenied
  sv.on("/settings", HTTP_GET, [] { if (!apDenied()) sendGz(SETTINGS_HTML_GZ, SETTINGS_HTML_GZ_LEN); });
  sv.on("/wifi", HTTP_GET, [] { sendGz(WIFI_HTML_GZ, WIFI_HTML_GZ_LEN); });
  sv.on("/api/state", HTTP_GET, [] { if (!apDenied()) sendJson(stateJson(sv.hasArg("since") ? sv.arg("since").toDouble() : 0, sv.arg("m").toInt())); });
  sv.on("/api/history", HTTP_GET, [] {
    if (apDenied()) return;
    sv.setContentLength(CONTENT_LENGTH_UNKNOWN);
    sv.send(200, "application/json", "");
    String out = "[";
    for (int k = 0; k < ringN; k++) {
      out += (k ? "," : "") + sampleJson(ring[(ringHead - ringN + k + KEEP) % KEEP]);
      if (out.length() > 1500) { sv.sendContent(out); out = ""; }
    }
    sv.sendContent(out + "]");
    sv.sendContent("");
  });
  sv.on("/api/manual", HTTP_GET, [] { if (!apDenied()) sendJson(manualJson()); });
  sv.on("/api/cmd", HTTP_POST, handleCmd);
  sv.on("/api/auto", HTTP_POST, handleAuto);
  sv.on("/api/learned/clear", HTTP_POST, handleClearLearned);
  sv.on("/api/settings", HTTP_GET, [] { if (!apDenied()) sendJson(settingsJson()); });
  sv.on("/api/settings", HTTP_POST, handleSettingsPost);
  sv.on("/api/wifi", HTTP_GET, [] { sendJson(wifiJson()); });
  sv.on("/api/wifi", HTTP_POST, handleWifiPost);
  sv.on("/api/scan", HTTP_GET, [] { sendJson(scanJson()); });
  sv.on("/api/reboot", HTTP_POST, handleReboot);
  sv.on("/api/info", HTTP_GET, [] {
    if (apDenied()) return;
    // 配网热点的密码只在这里给（家里网络上的设置页），/api/wifi 从热点也能读，那里不放
    sendJson("{\"version\":" + jsonStr(FW_VERSION) + ",\"log\":" + jsonStr(logStats()) + ",\"heap\":" + String(ESP.getFreeHeap())
             + ",\"uptime\":" + String((uint32_t)mono()) + ",\"wifi\":" + wifiJson() + ",\"ap_pass\":" + jsonStr(apPassword()) + "}");
  });
  sv.on("/log.csv", HTTP_GET, [] { if (!apDenied()) logCsv(sv, sv.arg("hours").toInt()); });
  sv.on("/events.csv", HTTP_GET, [] { if (!apDenied()) eventsCsv(sv); });
  sv.on("/update", HTTP_POST, handleUpdateDone, handleUpdateUpload);
  sv.onNotFound(handleNotFound);
  sv.begin();
}

void webLoop() { sv.handleClient(); }
