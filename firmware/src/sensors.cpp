// 三块 OPT3001 轮流读。
//
// 接线（见 hardware/wiring.html）：三块共用 GND、3V3、SCL=GPIO4，
// SDA 各接一个脚：A（正面）GPIO5、B（右面）GPIO6、C（顶面）GPIO7，INT 不接。
// 三块板地址一样，所以每块板单独走自己的 SDA。没在读的两块 SDA 一直被板上的上拉电阻拉高，
// 看得到 SCL 在跳，但收不到起始信号（SCL 高时 SDA 往下跳），不会应答。
//
// I2C 是程序自己翻转引脚做的（软件 I2C），没用芯片的硬件 I2C：每块板一根独立的 SDA，
// 不用来回切换硬件 I2C 的引脚，逻辑简单；传感器每秒只读几个字节，速度够用（约 100 kHz）。
// （2026-11-20 第一版用硬件 I2C，每读一块就 Wire.end() 再换 SDA 脚 Wire.begin()，B、C 经常读失败，
//  当时以为是切换引脚的问题，改成软件 I2C 后照样失败。用 s 命令扫地址才查出来：B 一会儿在 0x44、一会儿在 0x45 应答，
//  是板上 ADDR 脚悬空（地址跳线没焊通）。硬件 I2C 那版未必有问题，只是没再回去试。）
//
// 地址不固定：这批板子的 ADDR 脚没接死，芯片每次通信都重新看这个脚，地址会在 0x44～0x47 之间跳
// （B 换了新板以后 B 稳了，原来稳的 A 又开始跳，所以不是哪一块坏，是都没接）。
// 好在每根 SDA 上只有一块板，哪个地址应答都是它：每次读写先用上次成功的地址，不应答就把四个地址轮着试。
// 板上的地址跳线焊到 44（ADDR 接地）才是根治，焊了以后这段重试就基本用不上了。
//
// 两档速度：慢档每次积分 800 ms、每秒一行（默认，弱光下读数更细、噪声更小，调亮度每秒一次足够）；
// 快档每次积分 100 ms、每 100 ms 发一行（看曲线、做遮挡测试时临时切过去）。
// 传感器连续测量每块只用几微安，采样快慢对功耗影响很小。

#include "sensors.h"

#include <driver/gpio.h>

namespace sensors {
namespace {

constexpr gpio_num_t PIN_SCL = GPIO_NUM_4;
constexpr gpio_num_t PIN_SDA[3] = {GPIO_NUM_5, GPIO_NUM_6, GPIO_NUM_7};
constexpr char NAME[3] = {'A', 'B', 'C'};

// OPT3001 的地址由 ADDR 脚决定（接 GND 0x44、VDD 0x45、SDA 0x46、SCL 0x47）。
// 板子上是焊盘跳线，这批没焊通，ADDR 悬空，所以四个地址都可能应答
constexpr uint8_t ADDRS[] = {0x44, 0x45, 0x46, 0x47};
constexpr int ROUNDS = 6;   // 一次读写最多把四个地址轮几遍

constexpr uint8_t REG_RESULT = 0x00;
constexpr uint8_t REG_CONFIG = 0x01;
constexpr uint8_t REG_MANUFACTURER = 0x7E;   // 应该是 0x5449（"TI"）
constexpr uint8_t REG_DEVICE = 0x7F;         // 应该是 0x3001

// 配置：自动量程（RN=1100）、连续测量（M=11）、中断锁存（L=1，不接 INT 无所谓），
// CT 位选每次积分 100 ms（快档）还是 800 ms（慢档，噪声小）
constexpr uint16_t CONFIG_FAST = 0xC610;
constexpr uint16_t CONFIG_SLOW = 0xCE10;
constexpr uint16_t CONFIG_CHECK = 0x0E00;   // 读回来的配置要核对的位：CT 和 M

bool fastMode = false;   // 现在是不是快档（2026-11-21 起默认慢档）
uint16_t config() { return fastMode ? CONFIG_FAST : CONFIG_SLOW; }

uint8_t last[3] = {0x44, 0x44, 0x44};   // 每块板上次应答的地址，下次先试它
bool ready[3] = {false, false, false};  // 找到了、配置也写进去了
uint32_t retries[3] = {0, 0, 0};        // 换地址重试的次数（i 命令里看，判断 ADDR 跳得厉不厉害）

// ---------- 软件 I2C ----------
// 引脚设成开漏：写 1 是放开（靠上拉变高），写 0 是拉低；开漏模式下照样能读回线上的电平。
// 半个时钟周期 5 微秒，约 100 kHz
class SoftI2C {
 public:
  explicit SoftI2C(gpio_num_t sda) : sda_(sda) {}

  bool readReg(uint8_t a, uint8_t reg, uint16_t &v) {
    start();
    bool ok = write(a << 1) && write(reg);
    if (ok) {
      start();   // 重复起始，换成读
      ok = write((a << 1) | 1);
    }
    if (ok) {
      v = (uint16_t)read(true) << 8;
      v |= read(false);
    }
    stop();
    return ok;
  }

  bool writeReg(uint8_t a, uint8_t reg, uint16_t v) {
    start();
    bool ok = write(a << 1) && write(reg) && write(v >> 8) && write(v & 0xFF);
    stop();
    return ok;
  }

  // SDA 被卡在低电平时（比如上次读到一半断了），打 9 个时钟让传感器把字节吐完、放开 SDA
  void recover() {
    sda(1);
    for (int i = 0; i < 9 && !gpio_get_level(sda_); i++) {
      scl(0);
      scl(1);
    }
    stop();
  }

 private:
  static void wait() { delayMicroseconds(5); }
  void sda(int v) { gpio_set_level(sda_, v); wait(); }
  static void scl(int v) {
    gpio_set_level(PIN_SCL, v);
    // 放开 SCL 时等它真的变高（传感器可以拉住时钟让主机等，OPT3001 不会，但等一下不吃亏），最多等 1 毫秒
    if (v) for (int i = 0; i < 200 && !gpio_get_level(PIN_SCL); i++) delayMicroseconds(5);
    wait();
  }

  // 起始：SCL 高的时候 SDA 往下跳（也用作重复起始）
  void start() {
    sda(1);
    scl(1);
    sda(0);
    scl(0);
  }
  // 停止：SCL 高的时候 SDA 往上跳
  void stop() {
    sda(0);
    scl(1);
    sda(1);
  }
  // 发一个字节，高位先发，返回对方有没有应答（第 9 个时钟 SDA 被拉低）
  bool write(uint8_t b) {
    for (int i = 7; i >= 0; i--) {
      sda((b >> i) & 1);
      scl(1);
      scl(0);
    }
    sda(1);
    scl(1);
    bool ack = !gpio_get_level(sda_);
    scl(0);
    return ack;
  }
  // 收一个字节，高位先收；ack 为 true 时回应答（还要接着收），最后一个字节回不应答
  uint8_t read(bool ack) {
    uint8_t b = 0;
    sda(1);
    for (int i = 0; i < 8; i++) {
      scl(1);
      b = (b << 1) | gpio_get_level(sda_);
      scl(0);
    }
    sda(ack ? 0 : 1);
    scl(1);
    scl(0);
    sda(1);
    return b;
  }

  gpio_num_t sda_;
};

SoftI2C bus[3] = {SoftI2C(PIN_SDA[0]), SoftI2C(PIN_SDA[1]), SoftI2C(PIN_SDA[2])};

void setupPin(gpio_num_t p) {
  gpio_reset_pin(p);
  gpio_set_level(p, 1);
  gpio_set_direction(p, GPIO_MODE_INPUT_OUTPUT_OD);
  gpio_set_pull_mode(p, GPIO_PULLUP_ONLY);   // 板子上有上拉，这个只是保险（哪块板没接时脚不悬空）
}

// 在第 i 块板上读写一个寄存器：先用上次应答的地址，不应答就把四个地址轮着试，最多 ROUNDS 遍
bool readAny(int i, uint8_t reg, uint16_t &v) {
  for (int k = 0; k < ROUNDS; k++) {
    if (bus[i].readReg(last[i], reg, v)) return true;
    for (uint8_t a : ADDRS) {
      if (a == last[i]) continue;
      retries[i]++;
      if (bus[i].readReg(a, reg, v)) { last[i] = a; return true; }
    }
  }
  bus[i].recover();   // 一直没应答：可能 SDA 被卡住了，放开一下，下次再试
  return false;
}

bool writeAny(int i, uint8_t reg, uint16_t v) {
  for (int k = 0; k < ROUNDS; k++) {
    if (bus[i].writeReg(last[i], reg, v)) return true;
    for (uint8_t a : ADDRS) {
      if (a == last[i]) continue;
      retries[i]++;
      if (bus[i].writeReg(a, reg, v)) { last[i] = a; return true; }
    }
  }
  bus[i].recover();
  return false;
}

// 确认第 i 块板是 OPT3001（厂商号、型号都对上），然后写配置开始连续测量
bool probe(int i) {
  uint16_t man, dev;
  return readAny(i, REG_MANUFACTURER, man) && man == 0x5449
      && readAny(i, REG_DEVICE, dev) && dev == 0x3001
      && writeAny(i, REG_CONFIG, config());
}

// 读第 i 块板的照度（lux），读不到返回负数
float readLux(int i) {
  if (!ready[i]) {
    ready[i] = probe(i);
    return -1;   // 刚写完配置，第一次测量还没出来，下一行再读
  }
  // 顺手看一眼配置：传感器要是掉过电被复位，会回到关机状态（M=00），结果寄存器停在旧值不动，得重写配置；
  // 刚换过快慢档时 CT 也对不上，一样重写（这一行的结果还是旧档的，不要）
  uint16_t cfg, raw;
  if (!readAny(i, REG_CONFIG, cfg) || !readAny(i, REG_RESULT, raw)) {
    ready[i] = false;
    return -1;
  }
  if ((cfg & CONFIG_CHECK) != (config() & CONFIG_CHECK)) {
    if (!writeAny(i, REG_CONFIG, config())) ready[i] = false;
    return -1;
  }
  // 结果 = 0.01 × 2^指数 × 尾数（高 4 位是指数，低 12 位是尾数）
  return 0.01f * (float)(1u << (raw >> 12)) * (float)(raw & 0x0FFF);
}

}  // namespace

void begin() {
  setupPin(PIN_SCL);
  for (gpio_num_t p : PIN_SDA) setupPin(p);
  for (int i = 0; i < 3; i++) ready[i] = probe(i);
}

bool fast() { return fastMode; }
void setFast(bool on) { fastMode = on; }   // 配置在下次读的时候发现对不上就重写
uint32_t periodMs() { return fastMode ? 100 : 1000; }

void readAll(float out[3]) {
  for (int i = 0; i < 3; i++) out[i] = readLux(i);
}

void printMode(Print &p) {
  p.println(fastMode ? "# 模式 快：每次积分 100 ms，每 100 ms 一行" : "# 模式 慢：每次积分 800 ms，每秒一行");
}

void printInfo(Print &p) {
  printMode(p);
  for (int i = 0; i < 3; i++) {
    p.printf("# %c: SDA=GPIO%d ", NAME[i], (int)PIN_SDA[i]);
    if (!ready[i]) ready[i] = probe(i);
    if (!ready[i]) {
      p.println("没找到");
      continue;
    }
    uint16_t man = 0, dev = 0, cfg = 0;
    readAny(i, REG_MANUFACTURER, man);
    readAny(i, REG_DEVICE, dev);
    readAny(i, REG_CONFIG, cfg);
    p.printf("上次地址 0x%02X 厂商 0x%04X 型号 0x%04X 配置 0x%04X 换地址重试 %lu 次\n",
             last[i], man, dev, cfg, (unsigned long)retries[i]);
  }
}

// ADDR 脚接死的板子只会在一个地址上应答；脚悬空的会东一下西一下（2026-11-20 B、后来 A 都扫出来是这样）
void scan(Print &p) {
  for (int i = 0; i < 3; i++) {
    int hit[4] = {0, 0, 0, 0};
    for (int k = 0; k < 50; k++) {
      for (int j = 0; j < 4; j++) {
        uint16_t man;
        if (bus[i].readReg(ADDRS[j], REG_MANUFACTURER, man) && man == 0x5449) hit[j]++;
      }
    }
    p.printf("# 扫描 %c: 0x44=%d 0x45=%d 0x46=%d 0x47=%d（各 50 次）\n", NAME[i], hit[0], hit[1], hit[2], hit[3]);
  }
}

}  // namespace sensors
