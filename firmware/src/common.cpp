#include "common.h"

#include <esp_timer.h>
#include <sys/time.h>
#include <time.h>

SemaphoreHandle_t gLock;

namespace {
constexpr int MSG_KEEP = 40;
String msgs[MSG_KEEP];
uint32_t seq = 0;   // 一共记过几条；第 n 条放在 msgs[(n - 1) % MSG_KEEP]
}  // namespace

void commonBegin() { gLock = xSemaphoreCreateRecursiveMutex(); }

double mono() { return esp_timer_get_time() / 1e6; }

bool timeOk() { return time(nullptr) > 1700000000; }   // 2023 年以后才算对上了

uint32_t epochNow() { return timeOk() ? (uint32_t)time(nullptr) : 0; }

String timeStr(uint32_t epoch) {
  if (!epoch) return "-";
  time_t t = epoch;
  struct tm tm;
  localtime_r(&t, &tm);
  char buf[24];
  strftime(buf, sizeof buf, "%Y-%m-%d %H:%M:%S", &tm);
  return buf;
}

double sampleTime() {
  if (!timeOk()) return mono();
  struct timeval tv;
  gettimeofday(&tv, nullptr);
  return tv.tv_sec + tv.tv_usec / 1e6;
}

void addMsg(const String &s) {
  String line = s.startsWith("#") ? s : "# " + s;
  Serial.println(line);
  Lock l;
  seq++;
  msgs[(seq - 1) % MSG_KEEP] = line;
}

uint32_t msgSeq() {
  Lock l;
  return seq;
}

String msgsJson(uint32_t after) {
  Lock l;
  String out = "[";
  uint32_t first = seq > MSG_KEEP ? seq - MSG_KEEP + 1 : 1;
  if (after + 1 > first) first = after + 1;
  for (uint32_t n = first; n <= seq; n++) {
    if (n > first) out += ',';
    out += jsonStr(msgs[(n - 1) % MSG_KEEP]);
  }
  return out + "]";
}

void printMsgs(Print &p) {
  Lock l;
  uint32_t first = seq > MSG_KEEP ? seq - MSG_KEEP + 1 : 1;
  for (uint32_t n = first; n <= seq; n++) p.println(msgs[(n - 1) % MSG_KEEP]);
}

size_t MsgPrint::write(uint8_t c) {
  if (c == '\n') {
    addMsg(line_);
    line_ = "";
  } else if (c != '\r') {
    line_ += (char)c;
  }
  return 1;
}

String jsonStr(const String &s) {
  String out = "\"";
  for (size_t i = 0; i < s.length(); i++) {
    char c = s[i];
    if (c == '"' || c == '\\') { out += '\\'; out += c; }
    else if ((uint8_t)c < 0x20) { char b[8]; snprintf(b, sizeof b, "\\u%04x", c); out += b; }
    else out += c;
  }
  return out + "\"";
}

String jsonNum(float v, int digits) {
  if (isnan(v) || isinf(v)) return "null";
  char b[24];
  snprintf(b, sizeof b, "%.*g", digits, v);
  return b;
}
