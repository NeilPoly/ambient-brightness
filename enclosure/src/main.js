// 环境光传感器外壳的三维预览。
// 所有尺寸单位是毫米。坐标：x 朝右（人面对显示器时的右手边），y 朝上，z 朝人。
// 显示器屏幕面在 z = 0，上沿在 y = 0。
//
// 结构：三块传感器板和主板都装在方块里，USB 线从方块背面插入。
// 没有底座：方块用纳米胶直接贴在显示器顶部的窄面上，正面和屏幕前沿齐平，多出来的部分往后探。
// 胶直接贴在底面靠前的一条，不开槽；换摆法就把方块撕下来，绕前后方向转 90 度，用左侧面重贴。
// 后盖可以拆，USB 口开在后盖上。后面绕前后方向转 90 度还是朝后，所以两种摆法都不用动后盖。
// 水平方向贴的时候对齐边沿，俯仰跟着显示器走。
// USB 线从背后垂下去，在显示器背面用一个理线扣固定，线的重量不挂在方块上。
//
// 板子怎么固定：方块主体是一整块实心，像 CNC 铣一样只挖出板子的槽、元件让位和走线的地方（见 parts.js 的 innerVoids），
// 正面朝下打印，挖的地方都朝上开口，不用支撑：
// 右面、顶面两块从后面推进各自的插槽，芯片那头顶到槽底，背后是实心托住，后盖内侧的小凸起顶住尾巴；
// 正面那块横放（芯片朝右），从后面推进通道、贴到前壁，一根柱子穿过安装孔，柱头用烙铁压平；
// 主板从后面推进槽里，前面两个角压在没挖的地方下面，后面靠后盖上的 USB 口压住。
// 后盖四个角各一根打印的短柱子，插进主体角上的盲孔，靠摩擦力夹住，不用磁铁也不用胶；接缝外一圈细槽，指甲伸进去就能撬开。

import * as THREE from 'three';
import { OrbitControls } from 'three/addons/controls/OrbitControls.js';
import { RoundedBoxGeometry } from 'three/addons/geometries/RoundedBoxGeometry.js';
import { RoomEnvironment } from 'three/addons/environments/RoomEnvironment.js';
// 尺寸、板子摆法和要打印的实体都在 parts.js（导出 STL 也用它）
import {
  D, H, PORT_Y, V, AX, AY, AZ, CLIP_Z, onBoard, SENSORS, C3_M,
  quatTo, shellSolids, fixtureGeos, clipSolid, roundRect, initKernel, toGeometry,
} from './parts.js';
// 实体运算库的 WebAssembly，打包时直接嵌进页面，不用联网也不用另外的文件
import wasmBinary from 'manifold-3d/manifold.wasm';

const errorBox = document.getElementById('error');
window.addEventListener('error', (e) => { errorBox.textContent = `页面出错：${e.message}`; });
window.addEventListener('unhandledrejection', (e) => { errorBox.textContent = `页面出错：${e.reason}`; });

await initKernel({ wasmBinary });

// 方块中心在房间里的位置：方块底面垫着一层胶，比上沿高出胶厚；正面和屏幕前沿（z = 0）齐平
const CUBE_POS = V(0, D.tape.t + H, -H);

// 主板焊盘。side = 1 是 USB 口朝上看正面时的左边一排，-1 是右边一排；i 从 USB 口那头数起。
const c3Pad = (side, i) => V(side * (D.c3.W / 2 - 0.65), -10.16 + i * 2.54, D.c3.t / 2).applyMatrix4(C3_M);

// ---------- 材质 ----------
// 配色按个人打印件设计语言（~/persional/git/print-design-language/design-language.md）的黑色版：
// 方块和后盖都是黑的，只有正面进光口外面一圈是橙色。两卷料：黑、橙。
const BLACK = 0x1e1e1e;
const ORANGE = 0xf26b00;
const M = {
  shell: new THREE.MeshStandardMaterial({ color: BLACK, roughness: 0.85, metalness: 0 }),
  accent: new THREE.MeshStandardMaterial({ color: ORANGE, roughness: 0.75, metalness: 0, side: THREE.DoubleSide }),
  lidInner: new THREE.MeshStandardMaterial({ color: BLACK, roughness: 0.85, metalness: 0 }),   // 后盖内侧的凸起，透视时也不透明
  mount: new THREE.MeshStandardMaterial({ color: BLACK, roughness: 0.8, metalness: 0 }),   // 理线扣
  inner: new THREE.MeshStandardMaterial({ color: BLACK, roughness: 0.85, metalness: 0 }),   // 方块里面的导轨、框、柱子，透视时也不透明
  pcbGreen: new THREE.MeshStandardMaterial({ color: 0x23955a, roughness: 0.55 }),
  silk: new THREE.MeshStandardMaterial({ color: 0xf2f2ee, roughness: 0.7 }),
  black: new THREE.MeshStandardMaterial({ color: 0x232325, roughness: 0.45 }),
  pcbBlack: new THREE.MeshStandardMaterial({ color: 0x14161a, roughness: 0.5 }),
  chip: new THREE.MeshStandardMaterial({ color: 0x1a1a1c, roughness: 0.4 }),
  glass: new THREE.MeshStandardMaterial({ color: 0x9fb4c8, roughness: 0.1, metalness: 0.2 }),
  gold: new THREE.MeshStandardMaterial({ color: 0xd9b44a, roughness: 0.35, metalness: 1 }),
  steel: new THREE.MeshStandardMaterial({ color: 0xc8ccd0, roughness: 0.3, metalness: 1 }),
  smd: new THREE.MeshStandardMaterial({ color: 0xb9a58a, roughness: 0.6 }),
  ceramic: new THREE.MeshStandardMaterial({ color: 0xe9e6dc, roughness: 0.5 }),
  rubber: new THREE.MeshStandardMaterial({ color: 0x232426, roughness: 0.7 }),
  tape: new THREE.MeshStandardMaterial({ color: 0xd9d9d2, roughness: 0.95 }),
  arrow: new THREE.MeshStandardMaterial({ color: 0xff8a00, roughness: 0.5, emissive: 0xff8a00, emissiveIntensity: 0.35 }),
  monBody: new THREE.MeshStandardMaterial({ color: 0xb6babf, roughness: 0.45, metalness: 0.35 }),
  monFront: new THREE.MeshStandardMaterial({ color: 0x0d0e10, roughness: 0.35 }),
  desk: new THREE.MeshStandardMaterial({ color: 0xcbc5bb, roughness: 0.9 }),
  wall: new THREE.MeshStandardMaterial({ color: 0xe6e9ec, roughness: 1 }),
};

function mesh(geo, mat, shadow = true) {
  const m = new THREE.Mesh(geo, mat);
  m.castShadow = shadow;
  m.receiveShadow = shadow;
  return m;
}

// 运算结果当成一个打印件：转成网格、上材质、开阴影
function part(solid, mat) {
  return mesh(toGeometry(solid), mat);
}
const QX90 = new THREE.Quaternion().setFromAxisAngle(AX, Math.PI / 2);

// ---------- 传感器方块 ----------
function buildCube() {
  const cube = new THREE.Group();

  // 外壳的实体在 parts.js 里算：主体（整块实心挖出槽）、后盖、正面嵌进去的橙圈
  const solids = shellSolids();
  const body = part(solids.body, M.shell);
  const lid = part(solids.lid, M.shell);
  cube.add(body, lid);

  // 正面那块板的柱子（和主体一体打印）；后盖上的小凸起和插销跟着后盖走。
  // 预览里单独画、透视时不透明；导出 STL 时才并进主体和后盖
  const fx = fixtureGeos({ staked: true });
  for (const g of fx.body) cube.add(mesh(g, M.inner));
  for (const g of fx.lid) lid.add(mesh(g, M.lidInner));

  // 全身唯一的橙色：正面进光口外面一圈，嵌进正面两层（正面朝下打印，就是头两层换色）
  cube.add(part(solids.ring, M.accent));

  // 里面的东西：三块传感器板、主板、连线
  const inside = new THREE.Group();
  const wires = new THREE.Group();
  const boards = SENSORS.map((sn, k) => {
    const board = makeSensorBoard();
    board.applyMatrix4(sn.m);
    board.userData.home = board.position.clone();
    inside.add(board);
    wires.add(makeWires(sn, k));
    return board;
  });
  const c3 = makeC3();
  c3.applyMatrix4(C3_M);
  c3.userData.home = c3.position.clone();
  inside.add(c3, wires);
  cube.add(inside);

  // USB 插头：插在方块背面，跟着方块一起转
  const plug = new THREE.Group();
  const tongue = mesh(new THREE.BoxGeometry(8.3, 2.5, 6.5), M.steel);
  tongue.position.set(0, PORT_Y, -H + 2.6);
  const shell = mesh(new RoundedBoxGeometry(D.plug.w, D.plug.h, D.plug.len, 3, 1.6), M.rubber);
  shell.position.set(0, PORT_Y, -H - 0.4 - D.plug.len / 2);
  plug.add(tongue, shell);
  cube.add(plug);

  // 朝向箭头
  const arrows = new THREE.Group();
  for (const sn of SENSORS) {
    const g = new THREE.Group();
    const shaft = new THREE.Mesh(new THREE.CylinderGeometry(0.32, 0.32, 7.5, 12), M.arrow);
    shaft.position.y = 3.75;
    const head = new THREE.Mesh(new THREE.ConeGeometry(1.1, 2.8, 20), M.arrow);
    head.position.y = 8.9;
    g.add(shaft, head);
    g.quaternion.copy(quatTo(sn.n));
    g.position.copy(sn.n).multiplyScalar(H + 2.5).add(sn.win);
    arrows.add(g);
  }
  cube.add(arrows);

  return { cube, lid, arrows, plug, boards, c3, wires };
}

// 传感器板。自己的坐标：X 沿长边指向芯片那头，Y 指向 GND 那侧，元件面（感光面）朝 +Z，原点在板中心。
// 元件位置照商品图摆（u 离 GND 那侧长边、v 离芯片那头短边），大小按常见封装。
function makeSensorBoard() {
  const g = new THREE.Group();
  const { L, W, t, chip, hole } = D.sensor;
  const top = t / 2;
  const at = (u, v) => onBoard(u, v);

  // 板子：圆角矩形挖一个安装孔
  const outline = roundRect(L, W, 0.6);
  const [hx, hy] = at(hole.u, hole.v);
  const h = new THREE.Path();
  h.absarc(hx, hy, hole.d / 2, 0, Math.PI * 2, true);
  outline.holes.push(h);
  const pcb = new THREE.ExtrudeGeometry(outline, { depth: t, bevelEnabled: false, curveSegments: 24 });
  pcb.translate(0, 0, -t / 2);
  g.add(mesh(pcb, M.pcbGreen));
  // 安装孔外面那圈白色丝印
  const ring = mesh(new THREE.RingGeometry(3.05, 3.3, 48), M.silk, false);
  ring.position.set(hx, hy, top + 0.01);
  g.add(ring);

  // 一个两端电极的贴片件：su、sv 是沿 u、v 的尺寸，两端电极在长的那个方向上
  const smd = (u, v, su, sv, hgt, mat) => {
    const [x, y] = at(u, v);
    const body = mesh(new THREE.BoxGeometry(sv, su, hgt), mat, false);
    body.position.set(x, y, top + hgt / 2);
    g.add(body);
    for (const s of [-1, 1]) {
      const alongU = su >= sv;
      const cap = mesh(new THREE.BoxGeometry(alongU ? sv + 0.02 : 0.3, alongU ? 0.3 : su + 0.02, hgt + 0.02), M.steel, false);
      cap.position.set(alongU ? x : x + s * (sv / 2 - 0.15), alongU ? y + s * (su / 2 - 0.15) : y, top + hgt / 2);
      g.add(cap);
    }
  };
  // 三脚稳压管（SOT-23）：两只脚在 v 小的一侧，一只在另一侧
  const sot = (u, v) => {
    const [x, y] = at(u, v);
    const body = mesh(new THREE.BoxGeometry(1.3, 2.9, 1.0), M.black, false);
    body.position.set(x, y, top + 0.6);
    g.add(body);
    for (const [du, dv] of [[-0.95, -1.15], [0.95, -1.15], [0, 1.15]]) {
      const leg = mesh(new THREE.BoxGeometry(0.5, 0.4, 0.15), M.steel, false);
      const [lx, ly] = at(u + du, v + dv);
      leg.position.set(lx, ly, top + 0.08);
      g.add(leg);
    }
  };
  // 三焊盘的跳线（44/45、46/47 是地址跳线）
  const jumper = (u, v) => {
    for (const du of [-0.85, 0, 0.85]) {
      const p = mesh(new THREE.BoxGeometry(1.6, 0.6, 0.05), M.steel, false);
      const [x, y] = at(u + du, v);
      p.position.set(x, y, top + 0.025);
      g.add(p);
    }
  };

  // OPT3001：2×2 的透明封装，中间透出黑色的感光芯片
  const [cx, cy] = at(chip.u, chip.v);
  const pkg = mesh(new THREE.BoxGeometry(2, 2, 0.65), M.ceramic, false);
  pkg.position.set(cx, cy, top + 0.325);
  const die = mesh(new THREE.BoxGeometry(0.8, 0.8, 0.04), M.chip, false);
  die.position.set(cx, cy, top + 0.66);
  g.add(pkg, die);

  smd(1.6, 4.9, 0.8, 1.6, 0.45, M.black);    // 两个 4.7k 上拉电阻，竖着放
  smd(3.6, 4.9, 0.8, 1.6, 0.45, M.black);
  smd(6.9, 4.1, 1.6, 0.8, 0.8, M.smd);       // 电容
  smd(2.7, 8.4, 2.0, 1.25, 0.85, M.smd);
  smd(12.0, 1.3, 1.6, 0.8, 0.45, M.black);
  smd(12.0, 9.8, 1.6, 0.8, 0.8, M.smd);
  sot(6.9, 7.2);
  sot(12.0, 12.6);
  jumper(2.3, 1.6);
  jumper(12.0, 4.4);
  jumper(12.0, 7.6);

  // 5 个排针焊盘：GND、INT、SCL、SDA、VCC
  for (let i = 0; i < 5; i++) {
    const pad = mesh(new THREE.CylinderGeometry(0.85, 0.85, t + 0.06, 16), M.gold, false);
    pad.quaternion.copy(QX90);
    pad.position.set(...pinXY(i), 0);
    g.add(pad);
  }
  return g;
}
// 第 i 个排针在板上的位置，i 从 GND 数起：GND、INT、SCL、SDA、VCC
const pinXY = (i) => onBoard(D.sensor.pins.u + i * 2.54, D.sensor.pins.v);

// 一块传感器板的四根线：从焊盘背面出来，接到主板对应的焊盘上。
// 三块板共用 GND、3V3、SCL（GPIO4），数据线各一根：A 接 GPIO5，B 接 GPIO6，C 接 GPIO7。
function makeWires(sn, k) {
  const g = new THREE.Group();
  const { t } = D.sensor;
  const colors = { 0: 0x3a3b3d, 2: 0xf2c200, 3: sn.sda, 4: 0xd62828 };   // GND 黑、SCL 黄、SDA 各板一色、VCC 红
  const targets = {
    0: c3Pad(1, 1),                                        // GND
    4: c3Pad(1, 2),                                        // 3V3
    2: c3Pad(1, 7),                                        // GPIO4
    3: [c3Pad(1, 8), c3Pad(-1, 8), c3Pad(-1, 7)][k],       // GPIO5 / GPIO6 / GPIO7
  };
  for (const i of [0, 2, 3, 4]) {
    const p0 = V(...pinXY(i), -t / 2).applyMatrix4(sn.m);
    const p1 = p0.clone().addScaledVector(sn.n, -1.4);
    const q0 = targets[i];
    const q1 = q0.clone().add(V(0, 2.6, 0));
    const mid = p1.clone().lerp(q1, 0.5).lerp(V(0, -1, 0), 0.2);
    const curve = new THREE.CatmullRomCurve3([p0, p1, mid, q1, q0], false, 'catmullrom', 0.25);
    const wire = mesh(new THREE.TubeGeometry(curve, 48, 0.32, 6), new THREE.MeshStandardMaterial({ color: colors[i], roughness: 0.6 }), false);
    g.add(wire);
  }
  return g;
}

// 微雪 ESP32-C3-Zero。自己的坐标：宽沿 X，长沿 Y，元件面朝 +Z，USB 口在 -Y 一端。
function makeC3() {
  const g = new THREE.Group();
  const { W, L, t } = D.c3;
  g.add(mesh(new THREE.BoxGeometry(W, L, t), M.pcbBlack));
  const usb = mesh(new THREE.BoxGeometry(8.94, 7.35, 3.2), M.steel);
  usb.position.set(0, -L / 2 - 1.3 + 3.675, t / 2 + 1.6);
  const soc = mesh(new THREE.BoxGeometry(5, 5, 0.85), M.chip);
  soc.position.set(0, 3.2, t / 2 + 0.425);
  const led = mesh(new THREE.BoxGeometry(2, 2, 0.8), M.ceramic, false);
  led.position.set(0, -3.2, t / 2 + 0.4);
  const ant = mesh(new THREE.BoxGeometry(5, 2, 1), M.ceramic, false);
  ant.position.set(0, L / 2 - 1.6, t / 2 + 0.5);
  g.add(usb, soc, led, ant);
  for (const sx of [-1, 1]) {
    const btn = mesh(new THREE.BoxGeometry(3, 2.4, 1.4), M.steel, false);
    btn.position.set(sx * 5.4, -2.6, t / 2 + 0.7);
    g.add(btn);
    for (let i = 0; i < 9; i++) {
      const pad = mesh(new THREE.BoxGeometry(1.3, 1.6, t + 0.04), M.gold, false);
      pad.position.set(sx * (W / 2 - 0.65), -10.16 + i * 2.54, 0);
      g.add(pad);
    }
  }
  return g;
}

// ---------- 理线扣和纳米胶 ----------
// 理线扣：贴在显示器背面，把 USB 线固定住（实体在 parts.js 的 clipSolid），底下垫一层胶。
// 自己的坐标：原点在粘贴面的中心，x 沿显示器宽度，y 沿背面朝上，z 垂直背面、背离显示器。
function buildCordClip() {
  const g = new THREE.Group();
  const c = D.clip;
  g.add(part(clipSolid(), M.mount));

  const tape = mesh(new THREE.ExtrudeGeometry(roundRect(c.w - 1, c.h - 1, 2), { depth: c.tape, bevelEnabled: false, curveSegments: 10 }), M.tape, false);
  g.add(tape);
  g.userData.tape = tape;
  return g;
}

// 方块底下的纳米胶：贴在方块底面和上沿之间，靠前的一条
function buildTape() {
  const T = D.tape;
  const tape = mesh(new THREE.BoxGeometry(T.w, T.t, T.l), M.tape, false);
  tape.userData.home = V(0, T.t / 2, -(T.front + T.l / 2));
  tape.position.copy(tape.userData.home);
  return tape;
}

// ---------- 显示器、桌面、墙 ----------
function makeWallpaper() {
  const c = document.createElement('canvas');
  c.width = 1024; c.height = 576;
  const ctx = c.getContext('2d');
  const sky = ctx.createLinearGradient(0, 0, 0, 576);
  sky.addColorStop(0, '#c9d6de');
  sky.addColorStop(1, '#8fa7b6');
  ctx.fillStyle = sky;
  ctx.fillRect(0, 0, 1024, 576);
  const layers = ['#7b98a9', '#5f8094', '#46687e', '#325066', '#223a4d'];
  layers.forEach((col, i) => {
    const base = 250 + i * 68;
    ctx.fillStyle = col;
    ctx.beginPath();
    ctx.moveTo(0, 576);
    for (let x = 0; x <= 1024; x += 8) {
      const y = base + Math.sin(x / (150 + i * 28) + i * 1.7) * (30 - i * 3) + Math.sin(x / 61 + i) * 6;
      ctx.lineTo(x, y);
    }
    ctx.lineTo(1024, 576);
    ctx.closePath();
    ctx.fill();
  });
  const tex = new THREE.CanvasTexture(c);
  tex.colorSpace = THREE.SRGBColorSpace;
  return tex;
}

// 显示器背面的形状：边沿一圈是薄的，越往中间越鼓。实物的弧度没量过，这里只是为了画出"背面是弧的"。
function backZ(x, y) {
  const { W, H: MH, edge, bulge } = D.mon;
  const u = Math.min(1, Math.abs(x) / (W / 2));
  const v = Math.min(1, Math.abs(y + MH / 2) / (MH / 2));
  return -(edge + bulge * (1 - u * u) * (1 - v * v));
}
// 显示器背后、离背面 d 远的一个点
const behind = (x, y, d) => V(x, y, backZ(x, y) - d);

function buildMonitor() {
  const g = new THREE.Group();
  const { W, H: MH, bezel, edge, bulge } = D.mon;
  const body = mesh(new THREE.BoxGeometry(W, MH, edge - 1), M.monBody);
  body.position.set(0, -MH / 2, -(edge + 1) / 2);
  // 弧形后盖：一张网格按 backZ 鼓出去
  const shellGeo = new THREE.PlaneGeometry(W, MH, 72, 44);
  shellGeo.translate(0, -MH / 2, 0);
  const pos = shellGeo.attributes.position;
  for (let i = 0; i < pos.count; i++) pos.setZ(i, backZ(pos.getX(i), pos.getY(i)));
  shellGeo.scale(-1, 1, 1);   // 左右翻一下，让面朝后
  shellGeo.computeVertexNormals();
  const back = mesh(shellGeo, M.monBody);
  const front = mesh(new THREE.BoxGeometry(W, MH, 1), M.monFront);
  front.position.set(0, -MH / 2, -0.5);
  const screen = new THREE.Mesh(
    new THREE.PlaneGeometry(W - 2 * bezel, MH - bezel - 10),
    new THREE.MeshBasicMaterial({ map: makeWallpaper(), color: 0xd9d9d9 }),
  );
  screen.position.set(0, -bezel - (MH - bezel - 10) / 2, 0.05);
  const stand = new THREE.Group();
  const hub = mesh(new THREE.BoxGeometry(100, 100, 30), M.monBody);
  hub.position.set(0, -MH / 2, -15);
  const neck = mesh(new THREE.BoxGeometry(64, 300, 18), M.monBody);
  neck.position.set(0, -350, -39);
  const foot = mesh(new THREE.BoxGeometry(250, 8, 200), M.monBody);
  foot.position.set(0, -506, -10);
  stand.add(hub, neck, foot);
  stand.position.z = -(edge + bulge) + 5;
  g.add(body, back, front, screen, stand);
  return g;
}

// 理线扣在显示器背面的落点：正中，上沿下方 D.clip.drop 处。
// 返回落点 P 和三个方向（s 沿宽度，t 沿背面朝上，n 背离显示器），按背面的弧度算。
function clipFrame() {
  const P = V(0, -D.clip.drop, 0);
  P.z = backZ(P.x, P.y);
  const h = 0.5;
  const gx = (backZ(P.x + h, P.y) - backZ(P.x - h, P.y)) / (2 * h);
  const gy = (backZ(P.x, P.y + h) - backZ(P.x, P.y - h)) / (2 * h);
  const n = V(gx, gy, -1).normalize();
  const t = AY.clone().addScaledVector(n, -AY.dot(n)).normalize();
  const s = new THREE.Vector3().crossVectors(t, n);
  return { P, s, t, n };
}

// ---------- 场景 ----------
const canvas = document.getElementById('stage');
const renderer = new THREE.WebGLRenderer({ canvas, antialias: true, alpha: true });
renderer.setPixelRatio(Math.min(window.devicePixelRatio || 1, 2));
renderer.toneMapping = THREE.ACESFilmicToneMapping;
renderer.toneMappingExposure = 1.05;
renderer.shadowMap.enabled = true;
renderer.shadowMap.type = THREE.PCFShadowMap;

const scene = new THREE.Scene();
scene.fog = new THREE.Fog(0xdde2e7, 2600, 6500);
const pmrem = new THREE.PMREMGenerator(renderer);
scene.environment = pmrem.fromScene(new RoomEnvironment(), 0.04).texture;
scene.environmentIntensity = 0.7;

const camera = new THREE.PerspectiveCamera(30, 1, 4, 9000);
const controls = new OrbitControls(camera, canvas);
controls.enableDamping = true;
controls.dampingFactor = 0.09;
controls.minDistance = 35;
controls.maxDistance = 2600;

scene.add(new THREE.HemisphereLight(0xffffff, 0xcbd3db, 0.55));
const sun = new THREE.DirectionalLight(0xffffff, 2.3);
sun.castShadow = true;
sun.shadow.mapSize.set(2048, 2048);
sun.shadow.bias = -0.0004;
sun.shadow.normalBias = 0.25;
scene.add(sun, sun.target);
const SUN_DIR = V(-0.42, 0.8, 0.5).normalize();
// 背面补一盏不投影的灯，免得从后面看一片黑
const backFill = new THREE.DirectionalLight(0xffffff, 0.9);
backFill.position.set(260, 220, -600);
scene.add(backFill);

const desk = mesh(new THREE.PlaneGeometry(3400, 1700), M.desk);
desk.rotation.x = -Math.PI / 2;
desk.position.set(0, -510, 240);
const wall = mesh(new THREE.PlaneGeometry(6000, 3200), M.wall);
wall.position.set(0, 400, -420);
scene.add(desk, wall);

scene.add(buildMonitor());

// 方块、方块底下的胶、背面的理线扣都直接放在房间里
const cubeParts = buildCube();
const cubeGroup = cubeParts.cube;
const tape = buildTape();
const clip = buildCordClip();
scene.add(cubeGroup, tape, clip);

let cord = null;

// ---------- 状态 ----------
const reduceMotion = window.matchMedia('(prefers-reduced-motion: reduce)');
const state = { cfg: 'right', view: 'close', shell: 'solid', explode: false, labels: true };
const anim = {
  explode: 0,                    // 0..1
  angle: 0,                      // 方块绕前后轴的转角
  swap: null,                    // { from, to, t }
  fly: null,                     // { p0, p1, t0, t1, t }
};
const cfgAngle = (cfg) => (cfg === 'right' ? 0 : Math.PI / 2);
const ease = (t) => t * t * (3 - 2 * t);
const band = (t, a, b) => ease(Math.min(1, Math.max(0, (t - a) / (b - a))));

// 理线扣的落点和背离显示器的方向
const place = { P: V(0, 0, 0), n: V(0, 0, -1) };
const _m4 = new THREE.Matrix4();
// 把理线扣贴到显示器背面
function placeClip() {
  const f = clipFrame();
  place.P.copy(f.P);
  place.n.copy(f.n);
  clip.quaternion.setFromRotationMatrix(_m4.makeBasis(f.s, f.t, f.n));
  clip.position.copy(f.P);
  clip.updateMatrixWorld(true);
}

// USB 线：从插头尾部往后出来，弯下去留一小段松弛的线，卡进背面的理线扣，再顺着显示器背后垂下去。
// 插头跟着方块动，线每次按插头和理线扣的位置重画。
const _plugEnd = new THREE.Vector3();
const _plugDir = new THREE.Vector3();
const _q = new THREE.Quaternion();
function updateCord() {
  cubeParts.plug.localToWorld(_plugEnd.set(0, PORT_Y, -H - 0.4 - D.plug.len + 1));
  if (cord && cord.userData.end.distanceToSquared(_plugEnd) < 1e-4 && cord.userData.clip.distanceToSquared(clip.position) < 1e-4) return;
  if (cord) { scene.remove(cord); cord.geometry.dispose(); }
  const p = _plugEnd, c = D.clip;
  const back = _plugDir.set(0, 0, -1).applyQuaternion(cubeGroup.getWorldQuaternion(_q));
  const onClip = (y, z) => clip.localToWorld(V(0, y, z));
  const p1 = p.clone().addScaledVector(back, 6);
  const cIn = onClip(c.len / 2 + 8, CLIP_Z + 1.5), cOut = onClip(-c.len / 2 - 9, CLIP_Z + 1);
  const slack = p1.clone().lerp(cIn, 0.45).add(V(0, 0, -5));   // 往后鼓出一点，表示留了余量
  const run = new THREE.CatmullRomCurve3([
    p.clone(), p1, slack, cIn, onClip(c.len / 2, CLIP_Z), onClip(-c.len / 2, CLIP_Z), cOut,
    behind(cOut.x * 0.5 + 10, -170, 14), behind(25, -300, 12), behind(60, -392, 6),
  ]);
  cord = mesh(new THREE.TubeGeometry(run, 96, 1.6, 10), M.rubber);
  cord.userData = { end: p.clone(), clip: clip.position.clone() };
  scene.add(cord);
}

function viewPreset(name) {
  if (name === 'far') return { target: V(0, -215, 0), pos: V(330, -70, 1180), shadow: 620, center: V(0, -200, 0) };
  const c = CUBE_POS;
  if (name === 'side') {
    // 正侧面：方块贴在上沿的位置、往后探出多少、线怎么走，看得最清楚
    const t = c.clone().add(V(0, -24, -18));
    return { target: t, pos: t.clone().add(V(232, 46, -6)), shadow: 120, center: c };
  }
  if (name === 'back') {
    const t = c.clone().add(V(0, -28, -16));
    return { target: t, pos: t.clone().add(V(-130, 56, -196)), shadow: 120, center: c };
  }
  const t = c.clone().add(V(0, -7, 0));
  return { target: t, pos: t.clone().add(V(98, 44, 156)), shadow: 110, center: c };
}

function aimSun(span, center) {
  sun.target.position.copy(center);
  sun.position.copy(center).addScaledVector(SUN_DIR, span * 4 + 300);
  const cam = sun.shadow.camera;
  cam.left = -span; cam.right = span; cam.top = span; cam.bottom = -span;
  cam.near = 10; cam.far = span * 8 + 900;
  cam.updateProjectionMatrix();
  sun.shadow.needsUpdate = true;
}

function goToView(name, instant = false) {
  const v = viewPreset(name);
  aimSun(v.shadow, v.center);
  if (instant || reduceMotion.matches) {
    camera.position.copy(v.pos);
    controls.target.copy(v.target);
    anim.fly = null;
  } else {
    anim.fly = { p0: camera.position.clone(), p1: v.pos, t0: controls.target.clone(), t1: v.target, t: 0 };
  }
}

// 透视只让方块变透明；理线扣里没有东西，保持实体。
function applyShell() {
  const xray = state.shell === 'xray';
  // 透视时外壳、后盖、点缀一起变透明，里面的导轨和凸起保持不透明
  for (const mat of [M.shell, M.accent]) {
    mat.transparent = xray;
    mat.opacity = xray ? 0.26 : 1;
    mat.depthWrite = !xray;
    mat.needsUpdate = true;
  }
}

// ---------- 朝向标注 ----------
const labelLayer = document.getElementById('labels');
const labels = SENSORS.map((sn) => {
  const el = document.createElement('div');
  el.className = 'dir-label';
  el.innerHTML = '<span></span><small></small>';
  el.lastChild.textContent = sn.id;
  labelLayer.append(el);
  return { el, sn, anchor: sn.n.clone().multiplyScalar(H + 17).add(sn.win) };
});
const DIR_NAMES = [[AZ, '前'], [AZ.clone().negate(), '后'], [AY, '上'], [AY.clone().negate(), '下'], [AX, '右'], [AX.clone().negate(), '左']];
function dirName(n) {
  let best = DIR_NAMES[0], dot = -2;
  for (const d of DIR_NAMES) { const v = d[0].dot(n); if (v > dot) { dot = v; best = d; } }
  return best[1];
}
const _v = new THREE.Vector3();
const _toCam = new THREE.Vector3();
const _qw = new THREE.Quaternion();
function updateLabels() {
  const w = canvas.clientWidth, h = canvas.clientHeight;
  const names = [];
  // 位置和朝向都取方块在房间里的值
  const toCam = _toCam.subVectors(camera.position, cubeGroup.getWorldPosition(_toCam));
  const near = toCam.length() < 420;   // 拉远看整台显示器时文字会糊成一团，只留箭头
  toCam.normalize();
  cubeGroup.getWorldQuaternion(_qw);
  for (const l of labels) {
    const n = _v.copy(l.sn.n).applyQuaternion(_qw);
    const facing = n.dot(toCam) > -0.35;   // 箭头在方块背对镜头的那一面时不显示文字
    const name = dirName(n);
    names.push(`${l.sn.id} 朝${name}`);
    if (l.el.firstChild.textContent !== name) l.el.firstChild.textContent = name;
    const p = cubeGroup.localToWorld(_v.copy(l.anchor)).project(camera);
    const visible = state.labels && near && facing && p.z < 1 && p.z > -1;
    l.el.style.display = visible ? '' : 'none';
    if (visible) {
      const x = (p.x * 0.5 + 0.5) * w, y = (-p.y * 0.5 + 0.5) * h;
      l.el.style.transform = `translate(${x.toFixed(1)}px, ${y.toFixed(1)}px) translate(-50%, -50%)`;
    }
  }
  const roles = document.getElementById('roles');   // 展示版里没有这一行
  const text = names.join('，');
  if (roles && !anim.swap && roles.textContent !== text) roles.textContent = text;
}

// ---------- 每帧 ----------
function applyPose() {
  let lift = 0;
  if (anim.swap) {
    const s = anim.swap;
    lift = 9 * (band(s.t, 0, 0.3) - band(s.t, 0.7, 1));   // 先撕下来，转完再贴回去
    anim.angle = s.from + (s.to - s.from) * band(s.t, 0.3, 0.7);
  }
  const e = ease(anim.explode);

  // 两种摆法下方块中心的位置一样，换摆法只是原地转 90 度。拆开时方块往上抬，胶留在中间
  cubeGroup.position.set(CUBE_POS.x, CUBE_POS.y + lift + 14 * e, CUBE_POS.z);
  cubeGroup.quaternion.setFromAxisAngle(AZ, anim.angle);
  tape.position.copy(tape.userData.home);
  tape.position.y += lift + 2 * e;

  // 拆开：后盖退到后面，右面、顶面两块板和主板顺着导轨往后抽出一截（正面那块是烫住的，不动）。
  // 连线是按装好的位置画的，抽出来时先不画
  cubeParts.lid.position.z = -30 * e;
  cubeParts.plug.position.z = -42 * e;   // 插头退到后盖后面
  SENSORS.forEach((sn, k) => {
    if (sn.id !== 'A') cubeParts.boards[k].position.copy(cubeParts.boards[k].userData.home).addScaledVector(sn.a, -15 * e);
  });
  cubeParts.c3.position.copy(cubeParts.c3.userData.home).add(V(0, 0, -17 * e));
  cubeParts.wires.visible = e < 0.02;

  // 理线扣：拆开时离开显示器背面，胶留在两者中间
  clip.position.copy(place.P).addScaledVector(place.n, 6 * e);
  clip.userData.tape.position.z = -3 * e;
  clip.updateMatrixWorld(true);
  cubeGroup.updateMatrixWorld(true);
  cubeParts.arrows.visible = state.labels;
  updateCord();
}

function resize() {
  const w = canvas.clientWidth, h = canvas.clientHeight;
  if (canvas.width !== Math.floor(w * renderer.getPixelRatio()) || canvas.height !== Math.floor(h * renderer.getPixelRatio())) {
    renderer.setSize(w, h, false);
    camera.aspect = w / h;
    camera.updateProjectionMatrix();
    dirty = true;
  }
}

// 只在画面会变的时候才重画（转动、动画、换选项、改窗口大小）；不在屏幕上时整个停下，省电、风扇不转
let dirty = true;
let onScreen = true;
if ('IntersectionObserver' in window) {
  new IntersectionObserver(([e]) => { onScreen = e.isIntersecting; if (onScreen) dirty = true; }).observe(canvas);
}

let last = performance.now();
function frame(now) {
  const dt = Math.min(0.05, (now - last) / 1000);
  last = now;
  if (!onScreen || document.hidden) { requestAnimationFrame(frame); return; }
  const still = reduceMotion.matches;

  const target = state.explode ? 1 : 0;
  const moving = anim.explode !== target || !!anim.swap || !!anim.fly;
  anim.explode = still ? target : anim.explode + (target - anim.explode) * (1 - Math.exp(-dt * 7));
  if (Math.abs(anim.explode - target) < 0.001) anim.explode = target;

  if (anim.swap) {
    anim.swap.t = still ? 1 : Math.min(1, anim.swap.t + dt / 1.15);
    if (anim.swap.t >= 1) { anim.angle = anim.swap.to; anim.swap = null; }
  }
  if (anim.fly) {
    const f = anim.fly;
    f.t = Math.min(1, f.t + dt / 0.9);
    const k = ease(f.t);
    camera.position.lerpVectors(f.p0, f.p1, k);
    controls.target.lerpVectors(f.t0, f.t1, k);
    if (f.t >= 1) anim.fly = null;
  }

  applyPose();
  resize();
  const turned = controls.update(dt);
  if (dirty || moving || turned) {
    renderer.render(scene, camera);
    updateLabels();
    dirty = false;
  }
  requestAnimationFrame(frame);
}

// ---------- 操作面板 ----------
const toolbar = document.getElementById('toolbar');
const buttons = [...toolbar.querySelectorAll('button')];

function syncButtons() {
  for (const b of buttons) {
    const on = b.dataset.toggle ? state[b.dataset.toggle] : state[b.dataset.key] === b.dataset.value;
    b.setAttribute('aria-pressed', String(!!on));
  }
}

function setState(key, value, instant = false) {
  if (key === 'cfg' && value !== state.cfg) {
    const from = anim.angle, to = cfgAngle(value);
    if (instant || reduceMotion.matches) { anim.angle = to; anim.swap = null; } else anim.swap = { from, to, t: 0 };
  }
  state[key] = value;
  if (key === 'view') goToView(value, instant);
  if (key === 'shell') applyShell();
  syncButtons();
  dirty = true;
}

toolbar.addEventListener('click', (e) => {
  const b = e.target.closest('button');
  if (!b) return;
  if (b.dataset.toggle) setState(b.dataset.toggle, !state[b.dataset.toggle]);
  else setState(b.dataset.key, b.dataset.value);
});

// 浏览器不支持 focusgroup 时：自己做方向键在按钮间移动，整个工具栏只占一个 Tab 位
if (!('focusGroup' in HTMLElement.prototype)) {
  buttons.forEach((b, i) => { b.tabIndex = i ? -1 : 0; });
  toolbar.addEventListener('focusin', ({ target }) => {
    if (buttons.includes(target)) buttons.forEach((b) => { b.tabIndex = b === target ? 0 : -1; });
  });
  toolbar.addEventListener('keydown', (e) => {
    const i = buttons.indexOf(document.activeElement);
    if (i < 0) return;
    const step = { ArrowRight: 1, ArrowLeft: -1 }[e.key];
    let j = null;
    if (step) j = (i + step + buttons.length) % buttons.length;
    else if (e.key === 'Home') j = 0;
    else if (e.key === 'End') j = buttons.length - 1;
    if (j === null) return;
    e.preventDefault();
    buttons[j].focus();
  });
}

// ---------- 启动 ----------
// 地址栏 # 后面可以带初始状态，方便截图核对，例如 #cfg=left&shell=xray&view=side&explode=1
const params = new URLSearchParams(location.hash.slice(1));
placeClip();
for (const key of ['cfg', 'shell', 'view']) if (params.has(key)) setState(key, params.get(key), true);
if (params.has('explode')) { state.explode = params.get('explode') === '1'; anim.explode = state.explode ? 1 : 0; }
if (params.has('labels')) state.labels = params.get('labels') !== '0';
// 展示版（个人网站上嵌的那个，模板是 showcase.html）：不显示朝向标注，不用滚轮缩放（免得抢网页的滚动），
// 慢慢自己转，一碰就停
if (document.body.dataset.mode === 'showcase') {
  state.labels = false;
  controls.enableZoom = false;
  controls.autoRotate = !reduceMotion.matches;
  controls.autoRotateSpeed = 2;
  controls.addEventListener('start', () => { controls.autoRotate = false; });
}
applyShell();
syncButtons();
resize();
goToView(state.view, true);
if (params.has('cam')) camera.position.fromArray(params.get('cam').split(',').map(Number));
if (params.has('tgt')) controls.target.fromArray(params.get('tgt').split(',').map(Number));
requestAnimationFrame(frame);
