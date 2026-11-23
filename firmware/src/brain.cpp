#include "brain.h"

#include <Preferences.h>

#include "common.h"

Brain B;

namespace {
Preferences prefs;

float clampf(float x, float lo, float hi) { return x < lo ? lo : x > hi ? hi : x; }
float lg(float v) { return log10f(v > 0.01f ? v : 0.01f); }

// 三路照度按权重对 log 加权平均（几何平均），合成一个照度；权重为 0 或读不到的那路不算
float combine(const float v[3]) {
  float num = 0, den = 0;
  for (int i = 0; i < 3; i++) {
    if (S.w[i] > 0 && v[i] >= 0) {
      num += S.w[i] * lg(v[i]);
      den += S.w[i];
    }
  }
  return den > 0 ? powf(10, num / den) : NAN;
}
}  // namespace

void Brain::begin() {
  prefs.begin("brain", false);
  nLearned = 0;
  size_t n = prefs.getBytesLength("learned");
  if (n && n % sizeof(Learned) == 0 && n / sizeof(Learned) <= MAX_LEARNED) {
    prefs.getBytes("learned", learned, n);
    nLearned = n / sizeof(Learned);
  }
  autoOn = prefs.getBool("auto", true);
}

void Brain::setAuto(bool on) {
  autoOn = on;
  prefs.putBool("auto", on);
}

// ---------- 照度 ----------
// 合成照度在 log 里做一阶平滑（时间常数 smoothing_s），滤掉闪烁和抖动。
// 两行读数之间把最新一行当作不变，平滑值按指数连续往它靠（lux()），所以目标亮度是连续变的，不是每来一行跳一下。
void Brain::onSample(double t, const float v[3]) {
  float L = combine(v);
  if (isnan(L)) return;
  float x = log10f(L);
  if (isnan(smooth_) || t - smoothT_ > STALE_S) {
    // 第一行，或者板子断过一阵：不从断开前的旧值慢慢平滑过来，直接从这一行开始
    smooth_ = xLast_ = x;
  } else {
    float tau = S.smoothS;
    if (tau > 0) smooth_ += (1 - expf(-(float)(t - smoothT_) / tau)) * (xLast_ - smooth_);
    else smooth_ = xLast_;
    xLast_ = x;
  }
  smoothT_ = t;
}

float Brain::lux(double now) const {
  if (isnan(smooth_) || now - smoothT_ > STALE_S) return NAN;
  float tau = S.smoothS;
  float s = tau > 0 ? smooth_ + (1 - expf(-(float)fmax(0.0, now - smoothT_) / tau)) * (xLast_ - smooth_) : xLast_;
  return powf(10, s);
}

float Brain::target(double now) {
  float L = lux(now);
  return isnan(L) ? NAN : effective(L);
}

// ---------- 曲线 ----------
float Brain::baseAt(float lux) const {
  float x = lg(lux);
  if (!S.nPts) return S.minB;
  if (x <= lg(S.ptLux[0])) return S.ptB[0];
  for (int i = 1; i < S.nPts; i++) {
    float x0 = lg(S.ptLux[i - 1]), x1 = lg(S.ptLux[i]);
    if (x <= x1) return S.ptB[i - 1] + (x - x0) / (x1 - x0) * (S.ptB[i] - S.ptB[i - 1]);
  }
  return S.ptB[S.nPts - 1];
}

// 实际用的曲线，在 log10(照度) 从 -2 到 5、每 0.01 一格上算好：
//   1. 基础曲线（设置里的点）；
//   2. 加上学到的点的修正。每个点有一个偏差 r（你要的 - 基础曲线），影响按钟形 k(d) = exp(-(d/sigma)²/2) 往外衰减，
//      d 按"差几个十倍照度"算，sigma 默认 0.5：差 3 倍照度时剩 61%，差 10 倍剩 14%。
//      最外侧两个点以外：r·k(d)；两个相邻的点之间：两边各自衰减后按位置线性过渡 (1-t)·r0·k(x-x0) + t·r1·k(x-x1)。
//      这样曲线正好经过每个学到的点，点挨得近时是直接连过去，离得远时中间回到基础曲线附近；
//   3. 照度越高亮度不会越低：学到的点把横轴分成几段，每段的值夹在两头点的亮度之间，再从左往右取累计最大；
//   4. 限制在 min～max。
void Brain::buildGrid() {
  const float lo = S.minB, hi = S.maxB, sigma = S.sigma;
  float px[MAX_LEARNED], pb[MAX_LEARNED], res[MAX_LEARNED];
  int n = nLearned;
  for (int i = 0; i < n; i++) {
    px[i] = lg(learned[i].lux);
    pb[i] = clampf(learned[i].b, lo, hi);
    res[i] = pb[i] - baseAt(powf(10, px[i]));
  }
  auto k = [&](float d) { return expf(-(d / sigma) * (d / sigma) / 2); };
  auto correction = [&](float x) -> float {
    if (!n) return 0;
    if (x <= px[0]) return res[0] * k(x - px[0]);
    if (x >= px[n - 1]) return res[n - 1] * k(x - px[n - 1]);
    for (int i = 1; i < n; i++) {
      if (x <= px[i]) {
        float f = px[i] > px[i - 1] ? (x - px[i - 1]) / (px[i] - px[i - 1]) : 0;
        return (1 - f) * res[i - 1] * k(x - px[i - 1]) + f * res[i] * k(x - px[i]);
      }
    }
    return 0;
  };
  for (int g = 0; g < GRID_N; g++) {
    float x = -2 + 0.01f * g;
    float v = baseAt(powf(10, x)) + correction(x);
    // 夹在两头学到的点之间：左边最近的点是下限，右边最近的点是上限
    for (int i = n - 1; i >= 0; i--) if (px[i] <= x) { v = fmaxf(v, pb[i]); break; }
    for (int i = 0; i < n; i++) if (px[i] >= x) { v = fminf(v, pb[i]); break; }
    grid_[g] = v;
  }
  for (int g = 1; g < GRID_N; g++) grid_[g] = fmaxf(grid_[g], grid_[g - 1]);
  for (int g = 0; g < GRID_N; g++) grid_[g] = clampf(grid_[g], lo, hi);
  gridOk_ = true;
}

float Brain::effective(float lux) {
  if (!gridOk_) buildGrid();
  float x = lg(lux);
  if (x <= -2) return grid_[0];
  if (x >= 5) return grid_[GRID_N - 1];
  int i = (int)((x + 2) / 0.01f);
  if (i >= GRID_N - 1) return grid_[GRID_N - 1];
  float f = (x - (-2 + 0.01f * i)) / 0.01f;
  return grid_[i] + f * (grid_[i + 1] - grid_[i]);
}

// ---------- 学到的点 ----------
// 记一个学到的点。附近（replace 个十倍以内，默认 0.3 ≈ 2 倍照度）的旧点换掉；
// 和它矛盾的旧点也去掉：照度更低却更亮的、照度更高却更暗的（照度越高亮度不能越低，听新的）。
// 存满了（MAX_LEARNED 个）就去掉最早的那个。
void Brain::learn(float lux, float b, uint32_t t) {
  float x = lg(lux), r = S.replace;
  Learned keep[MAX_LEARNED + 1];
  int n = 0;
  for (int i = 0; i < nLearned; i++) {
    const Learned &p = learned[i];
    float px = lg(p.lux);
    if (fabsf(px - x) < r || (px < x && p.b > b) || (px > x && p.b < b)) continue;
    keep[n++] = p;
  }
  if (n >= MAX_LEARNED) {
    int oldest = 0;
    for (int i = 1; i < n; i++) if (keep[i].t < keep[oldest].t) oldest = i;
    for (int i = oldest; i < n - 1; i++) keep[i] = keep[i + 1];
    n--;
  }
  // 按照度从低到高插进去
  int pos = 0;
  while (pos < n && keep[pos].lux < lux) pos++;
  for (int i = n; i > pos; i--) keep[i] = keep[i - 1];
  keep[pos] = {roundf(lux * 1000) / 1000, roundf(b * 1000) / 1000, t};
  n++;
  memcpy(learned, keep, n * sizeof(Learned));
  nLearned = n;
  saveLearned();
}

void Brain::clearLearned() {
  nLearned = 0;
  saveLearned();
}

void Brain::saveLearned() {
  if (nLearned) prefs.putBytes("learned", learned, nLearned * sizeof(Learned));
  else prefs.remove("learned");
  gridOk_ = false;
}
