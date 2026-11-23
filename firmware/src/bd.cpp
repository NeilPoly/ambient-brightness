#include "bd.h"

#include <ESPmDNS.h>
#include <HTTPClient.h>
#include <WiFi.h>

#include "common.h"
#include "settings.h"

namespace {
String host;               // 上次解析的是哪个名字（设置里改了就重新解析）
IPAddress ip;              // 解析出来的地址
bool resolved = false;
double lastTry = -1e9;     // 上次解析的时间：解析不出来时 10 秒才再试一次（mDNS 查一次要等 1.5 秒）

// 设置里的地址：IP 直接用；xxx.local 用 mDNS 查（ESP32 普通的域名解析不查 .local）；别的走路由器的 DNS
bool resolve(const String &h) {
  if (h != host) { host = h; resolved = false; lastTry = -1e9; }
  if (resolved) return true;
  if (h.isEmpty() || !WiFi.isConnected()) return false;
  if (mono() - lastTry < 10) return false;
  lastTry = mono();
  IPAddress a;
  if (a.fromString(h)) {
    ip = a;
  } else if (h.endsWith(".local")) {
    a = MDNS.queryHost(h.substring(0, h.length() - 6), 1500);
    if (a == IPAddress(0, 0, 0, 0)) return false;
    ip = a;
  } else {
    if (WiFi.hostByName(h.c_str(), a) != 1) return false;
    ip = a;
  }
  resolved = true;
  return true;
}

BdReply request(const char *op, const String &query) {
  String h, token;
  uint16_t port;
  {
    Lock l;
    h = S.host;
    token = S.token;
    port = S.port;
  }
  if (!resolve(h)) return {Bd::Fail, ""};
  String url = "http://" + ip.toString() + ":" + port + "/" + op + "?" + query;
  if (!token.isEmpty()) url += "&token=" + token;
  WiFiClient client;
  HTTPClient http;
  http.setConnectTimeout(800);
  http.setTimeout(1500);
  if (!http.begin(client, url)) return {Bd::Fail, ""};
  int code = http.GET();
  BdReply rep{Bd::Fail, ""};
  if (code == 200) {
    rep = {Bd::Ok, http.getString()};
    rep.body.trim();
  } else if (code == 404) {
    rep = {Bd::NotFound, ""};
  } else if (code < 0) {
    resolved = false;   // 连不上：可能 Mac 换了地址，下次重新解析
  }
  http.end();
  return rep;
}
}  // namespace

BdReply bdGet(const String &query) { return request("get", query); }
BdReply bdSet(const String &query) { return request("set", query); }

bool bdConfigured() {
  Lock l;
  return S.host[0] != 0;
}

String bdWhere() {
  String h;
  {
    Lock l;
    h = S.host;
  }
  if (h.isEmpty()) return "";
  IPAddress a;
  if (resolved && !a.fromString(h)) return h + "（" + ip.toString() + "）";
  return h;
}
