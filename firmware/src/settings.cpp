#include "settings.h"

#include <Preferences.h>
#include <WebServer.h>

#include "common.h"

Settings S;

namespace {
constexpr uint16_t VERSION = 1;   // 结构变了就加 1，旧的存档作废、回到默认值
Preferences prefs;

void copyStr(char *dst, size_t n, const String &src) {
  strncpy(dst, src.c_str(), n - 1);
  dst[n - 1] = 0;
}
}  // namespace

void settingsDefaults(Settings &s) {
  memset(&s, 0, sizeof s);
  s.version = VERSION;
  // 默认曲线是作者自己定的三个点（2026-11-21）：晚上开灯工作 1.84 lux → 50%、傍晚 46.7 lux → 75%、白天晴天 1150 lux → 100%。
  // 每个人的环境和喜好不一样，在设置页改，或者直接手动调几次让它学
  const float lux[] = {1.84f, 46.7f, 1150.0f}, b[] = {0.50f, 0.75f, 1.00f};
  s.nPts = 3;
  for (int i = 0; i < 3; i++) { s.ptLux[i] = lux[i]; s.ptB[i] = b[i]; }
  // BetterDisplay 的 50% 是显示器自己的亮度 0，往下是软件压暗（画面发灰），所以自动调节最低 50%
  s.minB = 0.50f;
  s.maxB = 1.00f;
  s.smoothS = 2;
  s.deadband = 0.02f;
  s.rate = 0.10f;
  s.w[0] = 1; s.w[1] = 0; s.w[2] = 0;   // 先只用正面 A，B、C 只记录
  s.sigma = 0.5f;
  s.replace = 0.3f;
  // 要调哪几台在设置页里填（编号是 BetterDisplay 里的 tagID）。
  // 作者的用法：BetterDisplay 里三台是一组、开着分组同步，只勾一台 U3223QE，两台 U2518D 跟着走
  // （同步传的是变化量：调一台，另外两台加减同样多）
  s.nDisp = 0;
  s.host[0] = 0;   // Mac 的地址在设置页里填
  s.port = 55777;
  s.token[0] = 0;
  s.sleepDdc = true;
  s.fastDefault = false;
  copyStr(s.tz, sizeof s.tz, "CST-8");
  copyStr(s.orientation, sizeof s.orientation, "前上右");
}

void settingsBegin() {
  prefs.begin("ambient", false);
  Settings s;
  if (prefs.getBytesLength("settings") == sizeof s && prefs.getBytes("settings", &s, sizeof s) == sizeof s && s.version == VERSION) {
    S = s;
  } else {
    settingsDefaults(S);
  }
}

void settingsSave() {
  Lock l;
  prefs.putBytes("settings", &S, sizeof S);
}

String settingsJson() {
  Lock l;
  String o = "{\"points\":[";
  for (int i = 0; i < S.nPts; i++) o += String(i ? "," : "") + "[" + jsonNum(S.ptLux[i], 6) + "," + jsonNum(S.ptB[i]) + "]";
  o += "],\"min\":" + jsonNum(S.minB) + ",\"max\":" + jsonNum(S.maxB);
  o += ",\"smoothing_s\":" + jsonNum(S.smoothS) + ",\"deadband\":" + jsonNum(S.deadband) + ",\"rate_per_s\":" + jsonNum(S.rate);
  o += ",\"weights\":{\"A\":" + jsonNum(S.w[0]) + ",\"B\":" + jsonNum(S.w[1]) + ",\"C\":" + jsonNum(S.w[2]) + "}";
  o += ",\"sigma\":" + jsonNum(S.sigma) + ",\"replace\":" + jsonNum(S.replace);
  o += ",\"displays\":[";
  for (int i = 0; i < S.nDisp; i++) {
    const DisplayCfg &d = S.disp[i];
    o += String(i ? "," : "") + "{\"tag\":" + d.tag + ",\"name\":" + jsonStr(d.name) + ",\"offset\":" + jsonNum(d.offset)
       + ",\"enabled\":" + (d.enabled ? "true" : "false") + "}";
  }
  o += "],\"host\":" + jsonStr(S.host) + ",\"port\":" + String(S.port) + ",\"token\":" + jsonStr(S.token);
  o += ",\"sleep_ddc\":" + String(S.sleepDdc ? "true" : "false") + ",\"fast\":" + String(S.fastDefault ? "true" : "false");
  o += ",\"tz\":" + jsonStr(S.tz) + ",\"orientation\":" + jsonStr(S.orientation) + "}";
  return o;
}

// 表单字段（都是字符串）：points = "1.84 50\n46.7 75\n1150 100"（照度 lux、亮度 %，一行一个点），
// min、max、deadband、rate 用 %；smoothing_s；wA wB wC；sigma replace；
// d0_tag d0_name d0_offset(%) d0_en ... d3_*（tag 空着就是没有这台）；host port token；sleep_ddc fast（"1" 为开）；tz orientation
String settingsFromForm(WebServer &sv) {
  Settings s;
  {
    Lock l;
    s = S;
  }
  auto num = [&](const char *k, float &out, float lo, float hi, float scale) -> String {
    if (!sv.hasArg(k)) return "";
    String v = sv.arg(k);
    v.trim();
    char *end;
    float f = strtof(v.c_str(), &end);
    if (v.isEmpty() || *end) return String(k) + " 不是数字";
    f *= scale;
    if (f < lo || f > hi) return String(k) + " 超出范围";
    out = f;
    return "";
  };
  String err;
  if (sv.hasArg("points")) {
    String t = sv.arg("points");
    t.replace("\r", "");
    t.replace(",", " ");
    int n = 0;
    float lx[MAX_PTS], bb[MAX_PTS];
    int start = 0;
    while (start <= (int)t.length()) {
      int nl = t.indexOf('\n', start);
      if (nl < 0) nl = t.length();
      String line = t.substring(start, nl);
      line.trim();
      start = nl + 1;
      if (line.isEmpty()) continue;
      float a, b;
      if (sscanf(line.c_str(), "%f %f", &a, &b) != 2) return "曲线这一行看不懂：" + line;
      if (n >= MAX_PTS) return "曲线最多 " + String(MAX_PTS) + " 个点";
      if (a <= 0 || b < 0 || b > 100) return "曲线的点超出范围：" + line;
      if (n && a <= lx[n - 1]) return "曲线的点要按照度从低到高写";
      if (n && b / 100 < bb[n - 1]) return "照度越高亮度不能越低：" + line;
      lx[n] = a;
      bb[n] = b / 100;
      n++;
    }
    if (!n) return "曲线至少要一个点";
    s.nPts = n;
    for (int i = 0; i < n; i++) { s.ptLux[i] = lx[i]; s.ptB[i] = bb[i]; }
  }
  if (!(err = num("min", s.minB, 0, 1, 0.01f)).isEmpty()) return err;
  if (!(err = num("max", s.maxB, 0, 1, 0.01f)).isEmpty()) return err;
  if (s.minB > s.maxB) return "最低亮度不能比最高还高";
  if (!(err = num("smoothing_s", s.smoothS, 0, 120, 1)).isEmpty()) return err;
  if (!(err = num("deadband", s.deadband, 0, 0.5f, 0.01f)).isEmpty()) return err;
  if (!(err = num("rate", s.rate, 0.005f, 1, 0.01f)).isEmpty()) return err;
  const char *wk[3] = {"wA", "wB", "wC"};
  for (int i = 0; i < 3; i++)
    if (!(err = num(wk[i], s.w[i], 0, 100, 1)).isEmpty()) return err;
  if (s.w[0] + s.w[1] + s.w[2] <= 0) return "三路权重不能全是 0";
  if (!(err = num("sigma", s.sigma, 0.05f, 5, 1)).isEmpty()) return err;
  if (!(err = num("replace", s.replace, 0, 2, 1)).isEmpty()) return err;
  if (sv.hasArg("d0_tag")) {
    int n = 0;
    for (int i = 0; i < MAX_DISP; i++) {
      String p = "d" + String(i) + "_";
      String tag = sv.arg(p + "tag");
      tag.trim();
      if (tag.isEmpty()) continue;
      DisplayCfg &d = s.disp[n];
      d.tag = tag.toInt();
      String name = sv.arg(p + "name");
      name.trim();
      copyStr(d.name, sizeof d.name, name.isEmpty() ? tag : name);
      float off = 0;
      if (!(err = num((p + "offset").c_str(), off, -0.5f, 0.5f, 0.01f)).isEmpty()) return err;
      d.offset = off;
      d.enabled = sv.arg(p + "en") == "1";
      n++;
    }
    s.nDisp = n;
  }
  if (sv.hasArg("host")) { String h = sv.arg("host"); h.trim(); copyStr(s.host, sizeof s.host, h); }
  if (sv.hasArg("port")) {
    long p = sv.arg("port").toInt();
    if (p < 1 || p > 65535) return "端口不对";
    s.port = p;
  }
  if (sv.hasArg("token")) { String t = sv.arg("token"); t.trim(); copyStr(s.token, sizeof s.token, t); }
  if (sv.hasArg("sleep_ddc")) s.sleepDdc = sv.arg("sleep_ddc") == "1";
  if (sv.hasArg("fast")) s.fastDefault = sv.arg("fast") == "1";
  if (sv.hasArg("tz")) { String t = sv.arg("tz"); t.trim(); if (!t.isEmpty()) copyStr(s.tz, sizeof s.tz, t); }
  if (sv.hasArg("orientation")) copyStr(s.orientation, sizeof s.orientation, sv.arg("orientation"));
  {
    Lock l;
    S = s;
  }
  settingsSave();
  return "";
}
