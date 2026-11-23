# Ambient

外接显示器跟着环境光自动调亮度。
External displays that follow the room light.

`Mac` `BetterDisplay` `ESP32-C3-Zero` `OPT3001` `3D 打印 / 3D printing`

3D 模型 / 3D model: https://www.neilpoly.top/ambient/

[中文](#中文) · [English](#english)

---

## 中文

显示器顶上一个 27 mm 的方块，前、上、右三面各一块光线传感器，里面的 ESP32 通过 BetterDisplay 调 Mac 的外接显示器。算法在方块里，Mac 上不装程序。

### 用到的东西

| | 型号 | 数量 |
|---|---|---|
| 主板 | 微雪 ESP32-C3-Zero | 1 |
| 照度传感器 | OPT3001 小板（5 针，19 × 14 mm） | 3 |
| 外壳 | `hardware/stl/` 的 4 个打印件，黑色 + 一点橙色 | 1 套 |
| 其他 | 细导线、无痕纳米胶、USB-C 线 | |

另外要有：Mac 上装 BetterDisplay 并打开 HTTP 接口（设置 → Application → Integration，端口 55777）；2.4 GHz Wi-Fi；PlatformIO。HTTP 接口要不要 BetterDisplay Pro 没核实过。

### 做法

1. **打印**：主体正面朝下、不用支撑；正面那圈橙色是最下面两层（0.2 mm 层高）。`01-body-black` 和 `01-body-orange-ring` 坐标对齐，在 Bambu Studio 里一起导入、作为一个多零件对象。
2. **接线**：见 `hardware/wiring.html`。GND、3V3、SCL（GPIO4）三块共用；SDA 各一根：前 GPIO5、右 GPIO6、上 GPIO7。
3. **烧录**：`cd firmware && pio run -t upload`。以后可以在设置页上传固件。
4. **配网**：手机连热点 `Ambient-xxxx`（第一次不要密码），选 Wi-Fi、输密码。
5. **设置**：打开 http://ambient.local/settings ，填 Mac 的地址和显示器的 tagID（Mac 上打开 http://localhost:55777/get?identifiers 能看到）。多台显示器在 BetterDisplay 里设成一组、打开同步，只勾一台。
6. **装上**：纳米胶贴在显示器顶上，插显示器的 USB 口；显示器设置里让 USB 待机时也供电。

细节（网页、调节规则、配网热点）见 [firmware/README.md](firmware/README.md)。

### 已知的问题

- 局域网里的网页和接口没有密码。
- BetterDisplay 的分组同步传的是变化量，传不到睡着的显示器，所以显示器睡着时不调。判断睡没睡靠读显示器的 DDC 电源状态，别的显示器不一定适用。

### 目录

| 目录 | 内容 |
|---|---|
| `firmware/` | 板子程序（PlatformIO + Arduino）和板子上的网页（`web/`） |
| `enclosure/` | 外壳的参数化模型（three.js + manifold-3d）：`node export-stl.mjs` 导出打印文件，`node build.mjs` 生成可以转着看的预览页 |
| `hardware/` | 打印文件、接线图 |

### 许可

代码（`firmware/`、`enclosure/`）用 [MIT](LICENSE)，打印文件、接线图、文档用 [CC BY 4.0](LICENSE-CC-BY-4.0.txt)。可以商用、可以修改，改过的版本也可以不开源；转发或发布时请注明出处，比如：

> 基于 neilpoly 的 Ambient（https://github.com/NeilPoly/ambient-brightness）

文中提到的产品名称归各自所有者，本项目和它们没有关系。

---

## English

A 27 mm cube on top of the monitor with a light sensor on its front, top and right faces. The ESP32 inside adjusts a Mac's external displays through BetterDisplay. The algorithm runs in the cube; nothing to install on the Mac.

### Parts

| | Model | Qty |
|---|---|---|
| Board | Waveshare ESP32-C3-Zero | 1 |
| Light sensor | OPT3001 breakout (5 pins, 19 × 14 mm) | 3 |
| Enclosure | 4 printed parts in `hardware/stl/`, black with a touch of orange | 1 set |
| Other | Thin wire, reusable nano tape, USB-C cable | |

Also needed: BetterDisplay on the Mac with its HTTP integration on (Settings → Application → Integration, port 55777); 2.4 GHz Wi-Fi; PlatformIO. Not verified whether the HTTP integration needs BetterDisplay Pro.

### Build

1. **Print**: body front face down, no supports; the orange ring is the first two layers at 0.2 mm. `01-body-black` and `01-body-orange-ring` share coordinates; load them together as one multi-part object in Bambu Studio.
2. **Wire**: see `hardware/wiring.html`. GND, 3V3 and SCL (GPIO4) shared; one SDA each: front GPIO5, right GPIO6, top GPIO7.
3. **Flash**: `cd firmware && pio run -t upload`. Later updates can be uploaded from the settings page.
4. **Wi-Fi**: join the `Ambient-xxxx` hotspot with a phone (no password the first time), pick a network, enter its password.
5. **Settings**: open http://ambient.local/settings, enter the Mac's address and the display's tagID (listed at http://localhost:55777/get?identifiers on the Mac). For several displays, group them in BetterDisplay with sync on and tick only one.
6. **Mount**: stick the cube on top of the monitor with nano tape and power it from the monitor's USB port, with USB power kept on in standby.

Details (web pages, control rules, setup hotspot) are in [firmware/README.md](firmware/README.md) (Chinese).

### Known issues

- The web pages and API on the LAN have no password.
- BetterDisplay's group sync passes relative changes and doesn't reach sleeping displays, so nothing is adjusted while the displays sleep. Sleep is detected from the monitor's DDC power state, which may not work on other monitors.

### License

Code (`firmware/`, `enclosure/`): [MIT](LICENSE). Print files, wiring diagram and docs: [CC BY 4.0](LICENSE-CC-BY-4.0.txt). Commercial use and closed-source modifications are fine. When you share or publish it, please credit the source, e.g.

> Based on Ambient by neilpoly (https://github.com/NeilPoly/ambient-brightness)

Product names mentioned here belong to their owners; this project is not affiliated with them.
