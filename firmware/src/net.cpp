#include "net.h"

#include <DNSServer.h>
#include <ESPmDNS.h>
#include <Preferences.h>
#include <WiFi.h>
#include <esp_wifi.h>
#include <time.h>

#include "common.h"
#include "settings.h"

namespace {
constexpr const char *HOSTNAME = "ambient";
constexpr double AP_AFTER_S = 600;     // 存过 Wi-Fi 的：开机（或断开）这么久还没连上才开热点。
                                       // 路由器重启、短时间断网都等得过去，不会动不动就开热点
constexpr double AP_OFF_AFTER_S = 120; // 连上家里的 Wi-Fi 这么久以后关掉热点

Preferences prefs;
DNSServer dns;
String ssid, pass;
String apPass;                         // 配网热点的密码（8 位数字，第一次开机生成）
bool ap = false;
bool wasConnected = false;
double downSince = 0;                  // 从什么时候开始没连上
double upSince = 0;                    // 从什么时候开始连上了

// 发射功率调低到 8.5 dBm：ESP32-C3 这类小板（C3-Zero、SuperMini）开满功率时天线那块会出问题，
// 常见的样子是热点看得到、手机也连得上，但网页打不开、连家里 Wi-Fi 也不稳（2026-11-21 第一次配网就是这样）。
// 屋里几米的距离 8.5 dBm 够用。另外频宽用 20 MHz，老手机、信道挤的时候更稳
void radioTweaks() {
  WiFi.setTxPower(WIFI_POWER_8_5dBm);
  esp_wifi_set_bandwidth(WIFI_IF_AP, WIFI_BW_HT20);
  esp_wifi_set_bandwidth(WIFI_IF_STA, WIFI_BW_HT20);
}

void startAp() {
  if (ap) return;
  WiFi.mode(ssid.isEmpty() ? WIFI_AP : WIFI_AP_STA);
  radioTweaks();
  // 没存过 Wi-Fi（刚刷完固件、或者在设置里忘掉了）：热点不要密码，不然第一次没法配；存过的要密码
  bool open = ssid.isEmpty();
  if (open) WiFi.softAP(apName().c_str());
  else WiFi.softAP(apName().c_str(), apPass.c_str());
  radioTweaks();
  dns.start(53, "*", WiFi.softAPIP());   // 手机连上热点后随便访问什么地址都指到配网页
  ap = true;
  addMsg("开了热点 " + apName() + (open ? "（不要密码）" : "（密码 " + apPass + "）")
         + "：手机连上它来配 Wi-Fi（或者打开 http://192.168.4.1）。热点上只能配 Wi-Fi");
}

void stopAp() {
  if (!ap) return;
  dns.stop();
  WiFi.softAPdisconnect(true);
  WiFi.mode(WIFI_STA);
  ap = false;
  addMsg("关了配网热点");
}

void onConnected() {
  addMsg("连上 Wi-Fi " + WiFi.SSID() + "，地址 " + WiFi.localIP().toString() + "，信号 " + String(WiFi.RSSI()) + " dBm");
  MDNS.end();
  if (MDNS.begin(HOSTNAME)) {
    MDNS.addService("http", "tcp", 80);
    addMsg("网页：http://ambient.local 或 http://" + WiFi.localIP().toString());
  }
  String tz;
  {
    Lock l;
    tz = S.tz;
  }
  configTzTime(tz.c_str(), "ntp.aliyun.com", "time.apple.com", "pool.ntp.org");
}
}  // namespace

String apName() {
  uint8_t mac[6];
  WiFi.macAddress(mac);
  char b[20];
  snprintf(b, sizeof b, "Ambient-%02X%02X", mac[4], mac[5]);
  return b;
}

void netBegin() {
  prefs.begin("wifi", false);
  ssid = prefs.getString("ssid", "");
  pass = prefs.getString("pass", "");
  // 配网热点的密码：第一次开机随机生成 8 位数字存起来，以后一直用这个
  apPass = prefs.getString("appass", "");
  if (apPass.length() != 8) {
    char b[9];
    snprintf(b, sizeof b, "%08lu", (unsigned long)(esp_random() % 100000000UL));
    apPass = b;
    prefs.putString("appass", apPass);
  }
  WiFi.persistent(false);   // 密码自己存在 Preferences 里，不让 Wi-Fi 库再存一份
  WiFi.setHostname(HOSTNAME);
  WiFi.setAutoReconnect(true);
  // 热点这边的情况记进说明里（串口、网页都看得到），配网出问题时好查是卡在哪一步
  WiFi.onEvent([](WiFiEvent_t, WiFiEventInfo_t) { addMsg("热点：有设备连上了"); }, ARDUINO_EVENT_WIFI_AP_STACONNECTED);
  WiFi.onEvent([](WiFiEvent_t, WiFiEventInfo_t info) {
    addMsg("热点：给设备分了地址 " + IPAddress(info.wifi_ap_staipassigned.ip.addr).toString());
  }, ARDUINO_EVENT_WIFI_AP_STAIPASSIGNED);
  WiFi.onEvent([](WiFiEvent_t, WiFiEventInfo_t) { addMsg("热点：设备断开了"); }, ARDUINO_EVENT_WIFI_AP_STADISCONNECTED);
  WiFi.onEvent([](WiFiEvent_t, WiFiEventInfo_t info) {
    addMsg("连家里 Wi-Fi 没成功（原因代码 " + String(info.wifi_sta_disconnected.reason) + "）");
  }, ARDUINO_EVENT_WIFI_STA_DISCONNECTED);
  // 开 IPv6（只要本地链路地址）：Mac 上 Chrome 查 ambient.local 时 IPv4、IPv6 两个地址都要，
  // 板子没有 IPv6 就不回，Chrome 每次干等 5 秒超时（2026-11-21 实测：按名字 8 秒，按 IP 0.6 秒；Safari 不受影响）。
  // 有了 IPv6 地址，mDNS 两个都回答，就不等了。网页服务只听 IPv4，浏览器连 IPv6 被拒后马上改用 IPv4
  WiFi.onEvent([](WiFiEvent_t, WiFiEventInfo_t) { WiFi.enableIpV6(); }, ARDUINO_EVENT_WIFI_STA_CONNECTED);
  WiFi.onEvent([](WiFiEvent_t, WiFiEventInfo_t) { addMsg("有了 IPv6 地址 " + WiFi.localIPv6().toString()); }, ARDUINO_EVENT_WIFI_STA_GOT_IP6);
  downSince = mono();
  if (ssid.isEmpty()) {
    addMsg("还没配 Wi-Fi");
    startAp();
  } else {
    addMsg("配网热点的密码是 " + apPass + "（连不上 Wi-Fi 超过 10 分钟才会开热点）");
    WiFi.mode(WIFI_STA);
    radioTweaks();
    WiFi.begin(ssid.c_str(), pass.c_str());
    radioTweaks();
    addMsg("正在连 Wi-Fi " + ssid);
  }
}

void netLoop() {
  if (ap) dns.processNextRequest();
  bool up = WiFi.isConnected();
  double now = mono();
  if (up && !wasConnected) { onConnected(); upSince = now; }
  if (!up && wasConnected) { addMsg("Wi-Fi 断开了，在重连"); downSince = now; }
  wasConnected = up;
  if (!up && !ap && now - downSince > AP_AFTER_S) startAp();
  if (up && ap && now - upSince > AP_OFF_AFTER_S) stopAp();
}

bool apActive() { return ap; }

String apPassword() { return apPass; }

String wifiJson() {
  bool up = WiFi.isConnected();
  return "{\"ssid\":" + jsonStr(up ? WiFi.SSID() : ssid) + ",\"connected\":" + (up ? "true" : "false")
       + ",\"rssi\":" + (up ? String(WiFi.RSSI()) : "null") + ",\"ip\":" + jsonStr(up ? WiFi.localIP().toString() : "")
       + ",\"ap\":" + (ap ? jsonStr(apName()) : "null") + "}";
}

String scanJson() {
  int n = WiFi.scanNetworks();
  String out = "[";
  for (int i = 0; i < n; i++) {
    // 同名的（一个路由器好几个信号）只留最强的那个
    bool dup = false;
    for (int j = 0; j < i; j++) if (WiFi.SSID(j) == WiFi.SSID(i) && WiFi.RSSI(j) >= WiFi.RSSI(i)) dup = true;
    if (dup || WiFi.SSID(i).isEmpty()) continue;
    if (out.length() > 1) out += ',';
    out += "{\"ssid\":" + jsonStr(WiFi.SSID(i)) + ",\"rssi\":" + String(WiFi.RSSI(i)) + ",\"open\":" + (WiFi.encryptionType(i) == WIFI_AUTH_OPEN ? "true" : "false") + "}";
  }
  WiFi.scanDelete();
  return out + "]";
}

void netSave(const String &s, const String &p) {
  prefs.putString("ssid", s);
  prefs.putString("pass", p);
}

void netForget() {
  prefs.remove("ssid");
  prefs.remove("pass");
}

void applyTz() {
  Lock l;
  setenv("TZ", S.tz, 1);
  tzset();
}
