// 外壳的几何：尺寸、三块板和主板在方块里的摆法、要打印的实体（主体、橙圈、后盖、理线扣）。
// 这里不碰页面和渲染，预览（main.js）和导出 STL（export-stl.mjs）共用这一份，改尺寸只改这里。
// 所有尺寸单位是毫米。方块自己的坐标：原点在方块中心，x 朝右（人面对显示器时的右手边），y 朝上，z 朝人。

import * as THREE from 'three';
import { mergeVertices, toCreasedNormals } from 'three/addons/utils/BufferGeometryUtils.js';
import Module from 'manifold-3d';

// ---------- 尺寸 ----------
export const D = {
  wall: 1.6,          // 方块壁厚
  inner: 24,          // 方块内腔边长（主板长 23.5，要放得下）
  cube: 27.2,         // 方块外尺寸 = inner + 2 * wall
  sensor: {
    L: 19, W: 14, t: 1.6,   // 传感器板长、宽、厚（2026-11-20 卡尺实测 19 x 14.2 x 1.6，宽多的 0.2 不管，仍按 14）
    // 板上的位置：u 是离 GND 那侧长边多远，v 是离芯片那头短边多远。
    // 原来都按商品图量；2026-11-20 用卡尺实测了孔、排针和元件高度（见各行），芯片位置还是按商品图量的
    chip: { u: 6.9, v: 1.3 },            // 感光面中心
    hole: { u: 7.0, v: 12.5, d: 3.0 },   // 安装孔（实测：孔径 3.0，孔边到 GND 长边 5.5、到板尾 5.0；原来估的是 6.8 / 12.6 / ⌀3.4）
    pins: { u: 2.1, v: 17.7 },           // 第一个排针（GND）的中心，往 VCC 方向每个隔 2.54（实测离板尾 1.3，和估的一样）
    standoff: 1.3,          // 板正面离内壁的距离，给芯片和贴片件留空（正面最高的是三脚稳压管，实测 1.1，还空 0.2）
    // 透光窗（对准芯片）离这一面正中多远，往芯片那头偏。三块一样。
    // 正面那块横放，芯片那头离右面那块板的插槽要留够，最多 6.65
    offset: { A: 6.5, B: 6.5, C: 6.5 },
  },
  grid: 1.7,          // 外表面的格子：方块边长的 1/16
  edge: { r: 2.55, ch: 0.8 },   // 前后方向的四条棱用圆角（打印时是竖边），前后两圈外沿只倒 0.8 的角
  // 全身唯一的橙色：正面进光口外面一圈，宽 0.9（0.4 喷嘴走两道线）。正面不放文字和别的记号
  // depth 是嵌进正面多深（正面朝下打印，就是头两层换色）
  accent: { ring: 0.9, depth: 0.4 },
  // 正面那块的柱子和垫台直径；板子四周留的缝；元件让位时两条长边留多宽托住板边；后面走线舱挖到哪（z）
  // stake 是打印时柱子穿过板子以后再多伸出多长，留给烙铁压成帽（预览里画的是压平以后的样子）
  // 柱子原来 ⌀3（按孔 ⌀3.4 估的），实测孔只有 ⌀3.0，改成 ⌀2.6，每边还是松 0.2
  fix: { post: 2.6, boss: 5.6, gap: 0.15, edge: 0.6, bay: -8.0, stake: 2.0 },
  // 后盖插销：后盖四个角各一根短柱子，插进主体四个角的盲孔，靠摩擦力夹住，不用卡紧。孔开在主体四个角的实心方柱里（方柱从接缝通到前壁）。
  // 2026-11-20 第一次打印：原来是 ⌀2.5 × 3 的圆柱、孔也画成 ⌀2.5（想着"先按一样大打，紧了扩孔"），结果孔打出来偏小、插不进；
  // 柱子竖着打，层纹横在柱子上，非常容易断。角上的实心左右只有约 4.4 宽（里面挨着主板槽和走线舱，外面是外壁），圆柱粗不了多少，
  // 上下方向有 6 多，所以改成扁的圆角长条：截面 w × h（w 左右、h 上下）、角半径 r，从接缝往前伸 len，头上倒 tip 的角好插，
  // 根部往外放 root 的斜角加强；孔每边比柱子大 clear、深 depth，孔口倒 mouth 的角好对准（mouth 比 root 大，柱子根部才进得去）。
  // 孔口倒角和根部加强都做不大：孔离外壁只有约 0.9，接缝外那圈细槽又吃掉 0.3。
  // at 是柱子中心离方块中心多远 [x, y]，四个角对称
  // seamGroove 是接缝外面一圈细槽（宽 × 深），留给指甲撬后盖
  // corner 是四个角实心方柱的内侧（走线舱挖到这里为止）
  pin: { w: 2.6, h: 3.6, r: 0.8, len: 2.5, tip: 0.3, root: 0.2, clear: 0.15, depth: 3.0, mouth: 0.3, at: [11.25, 10.1], corner: 9.15, seamGroove: [0.4, 0.3] },
  // 后盖散热孔：cols × rows 个直径 d 的圆孔，间距 pitch（2 格），左右居中，整组中心在 cy（USB 口上方）。
  // 避开后盖内侧四个角的柱子、顶住两块板尾巴的小凸起，以及外面 USB 插头盖住的地方
  vent: { d: 1.5, pitch: 3.4, cols: 5, rows: 4, cy: 2.55 },
  win: { r: 3 },   // 透光窗半径（直孔，不倒角）
  // 微雪 ESP32-C3-Zero；lift 是槽底比原来的内底面高多少；tongue 是从哪（z）往前两个角不挖、留着压住板子（让开边上的焊盘）
  // 板厚 2026-11-20 实测 1.6（原来估的 1.0），宽、长、USB 口高度核对过没问题
  c3: { W: 18, L: 23.5, t: 1.6, lift: 0.2, tongue: 10.9 },
  usbSlot: { w: 9.6, h: 3.8 },      // 方块背面的 USB 口开孔
  plug: { w: 12.4, h: 6.6, len: 20 },   // USB-C 插头的塑料外壳
  tape: { w: 22, l: 8, t: 1.0, front: 1.2 },   // 纳米胶：宽、前后长、厚、前沿离方块正面多远（往里缩一点，从正面看不到胶）
  clip: { drop: 45, w: 10, h: 14, t: 1.5, tape: 1.0, len: 8, r: 3.3, bore: 1.8, slot: 2.6 },   // 理线扣：贴在上沿下方多远、底板宽高厚、胶厚、扣的长度、外半径、内孔半径、开口宽度（线 ⌀3.2 左右）
  mon: { W: 712.6, H: 410, bezel: 6, edge: 9, bulge: 34 },   // edge 是边沿那圈窄面的厚度，也就是顶部能贴胶的宽度；bulge 是背面中间鼓出多少（都没量过，只为画出样子）
};
export const H = D.cube / 2, IH = D.inner / 2;
export const PORT_Y = -IH + D.c3.lift + D.c3.t + 1.6;   // USB 口中心在方块里的高度（方块自己的坐标）
export const SEAM_Z = -IH + 0.4;                         // 后盖和主体的接缝（离背面 2）

export const V = (x, y, z) => new THREE.Vector3(x, y, z);
export const AX = V(1, 0, 0), AY = V(0, 1, 0), AZ = V(0, 0, 1);

export const CLIP_Z = D.clip.tape + D.clip.t + 2.2;   // 理线扣的扣中心离显示器背面多远

// 传感器板自己的坐标：原点在板中心，X 沿长边指向芯片那头，Y 指向 GND 那侧长边，Z 是元件面。
// 把商品图上量的 (u, v) 换成板上坐标。
export const onBoard = (u, v) => [D.sensor.L / 2 - v, D.sensor.W / 2 - u];
export const [CHIP_X, CHIP_Y] = onBoard(D.sensor.chip.u, D.sensor.chip.v);

// 三块传感器板。n 是感光面朝向，a 是板的长边方向（焊盘在 -a 一端，芯片在 +a 一端）。
// 芯片在板子一头的边上，透光窗没法在各面正中，都往芯片那头偏 D.sensor.offset：
// 正面那块横放、芯片朝右（靠右面那条竖棱），右面、顶面两块芯片朝前。
// （试过正面那块竖放：板子上沿会撞上顶面那块的插槽，从后面推不进去，所以改回横放）
export const SENSORS = [
  { id: 'A', n: AZ.clone(), a: AX.clone(), sda: 0xff8a00 },
  { id: 'B', n: AX.clone(), a: AZ.clone(), sda: 0x2f9e44 },
  { id: 'C', n: AY.clone(), a: AZ.clone(), sda: 0x1c7ed6 },
].map((s) => {
  const S = D.sensor;
  const off = S.offset[s.id];
  const w = new THREE.Vector3().crossVectors(s.n, s.a);
  // 板子中心放在让芯片正好落到 a * offset 的地方
  const pcb = s.n.clone().multiplyScalar(IH - S.standoff - S.t / 2)
    .addScaledVector(s.a, off - CHIP_X).addScaledVector(w, -CHIP_Y);
  const m = new THREE.Matrix4().makeBasis(s.a, w, s.n).setPosition(pcb);
  // 内壁坐标：原点在这一面内壁的正中，x 沿 a、y 沿 w、z 朝外，所以离内壁往里 d 就是 z = -d
  const wall = new THREE.Matrix4().makeBasis(s.a, w, s.n).setPosition(s.n.clone().multiplyScalar(IH));
  const win = s.a.clone().multiplyScalar(off);   // 透光窗中心偏离这一面正中的量
  return { ...s, off, w, pcb, m, wall, win };
});


// 主板在方块里的位置：平躺在底面上，元件面朝上，USB 口朝后并顶到后壁。
export const C3_M = new THREE.Matrix4()
  .makeBasis(V(-1, 0, 0), V(0, 0, 1), V(0, 1, 0))
  .setPosition(0, -IH + D.c3.lift + D.c3.t / 2, -IH + D.c3.L / 2);

// ---------- 实体运算（并、差、交） ----------
// 用 manifold-3d 算：结果保证是封闭的实体，导出的 STL 没有裂缝。
// （原来用 three-bvh-csg，预览看着没问题，但导出的网格在接缝处有上千条没封好的边，2026-11-20 换掉）
// 用之前要先 await initKernel()：浏览器里传 { wasmBinary }（打包进页面），Node 里不用传。
let K = null;
export async function initKernel(opts = {}) {
  if (K) return;
  K = await Module(opts);
  K.setup();
}
export const ADDITION = 'add', SUBTRACTION = 'subtract', INTERSECTION = 'intersect';

const _mat = new THREE.Matrix4();
const _one = V(1, 1, 1);
// 把几何体摆到指定位置和朝向，变换直接烘进顶点，这样所有实体都在同一个坐标系里运算。
// three.js 的几何体为了法线和贴图会把顶点拆开，这里只留位置、把重合的顶点并回去，才是一个封闭的实体。
export function solid(geo, pos = [0, 0, 0], quat = null) {
  _mat.compose(V(...pos), quat || new THREE.Quaternion(), _one);
  geo.applyMatrix4(_mat);
  const p = new THREE.BufferGeometry();
  p.setAttribute('position', (geo.index ? geo.toNonIndexed() : geo).getAttribute('position'));
  const w = mergeVertices(p, 1e-4);
  return new K.Manifold(new K.Mesh({
    numProp: 3,
    vertProperties: new Float32Array(w.attributes.position.array),
    triVerts: new Uint32Array(w.index.array),
  }));
}
export function op(a, b, kind) {
  return a[kind](b);
}
// 实体 → three.js 几何体。折角超过 30 度的棱保持锐利，圆柱面平滑着色
export function toGeometry(m) {
  const mesh = m.getMesh();
  const g = new THREE.BufferGeometry();
  const n = mesh.numProp;
  const pos = new Float32Array(mesh.vertProperties.length / n * 3);
  for (let i = 0, j = 0; i < mesh.vertProperties.length; i += n, j += 3) {
    pos[j] = mesh.vertProperties[i]; pos[j + 1] = mesh.vertProperties[i + 1]; pos[j + 2] = mesh.vertProperties[i + 2];
  }
  g.setAttribute('position', new THREE.BufferAttribute(pos, 3));
  g.setIndex(new THREE.BufferAttribute(new Uint32Array(mesh.triVerts), 1));
  return toCreasedNormals(g, Math.PI / 6);
}
export const quatTo = (dir) => new THREE.Quaternion().setFromUnitVectors(AY, dir);

// 圆角矩形轮廓，中心在原点
export function roundRect(w, h, r) {
  const s = new THREE.Shape();
  const x = w / 2, y = h / 2;
  s.moveTo(-x + r, -y);
  s.lineTo(x - r, -y);
  s.absarc(x - r, -y + r, r, -Math.PI / 2, 0, false);
  s.lineTo(x, y - r);
  s.absarc(x - r, y - r, r, 0, Math.PI / 2, false);
  s.lineTo(-x + r, y);
  s.absarc(-x + r, y - r, r, Math.PI / 2, Math.PI, false);
  s.lineTo(-x, -y + r);
  s.absarc(-x + r, -y + r, r, Math.PI, Math.PI * 1.5, false);
  return s;
}

// 沿前后方向一节一节的圆角长条（放样），中心在 (x, y)：rings 是从后往前的一串截面 [宽, 高, 角半径, z]，
// 相邻两个截面连成直面或斜面，两头封口。每个截面的点数一样（四个角各 seg 段圆弧），点一一对应连起来，整根是一个封闭的几何体。
// 后盖插销（根部加强、直的一段、柱头倒角）和主体上的孔（孔口倒角、孔身）都用它一次做出来。
// （原来用几段拼：几段的侧面差一点点重合，并起来导出的 STL 有几百条边没封好，2026-11-20 改成放样）
export function loftGeo(x, y, rings, seg = 8) {
  const ring = ([w, h, r, z]) => {
    const pts = [];
    const cx = w / 2 - r, cy = h / 2 - r;
    for (const [sx, sy, a0] of [[1, 1, 0], [-1, 1, 0.5], [-1, -1, 1], [1, -1, 1.5]]) {
      for (let i = 0; i <= seg; i++) {
        const a = (a0 + 0.5 * i / seg) * Math.PI;   // 逆时针（从 +z 往下看）
        pts.push([x + sx * cx + r * Math.cos(a), y + sy * cy + r * Math.sin(a), z]);
      }
    }
    return pts;
  };
  const R = rings.map(ring), n = R[0].length;
  const pos = [];
  const tri = (a, b, c) => pos.push(...a, ...b, ...c);
  for (let k = 0; k + 1 < R.length; k++) {
    for (let j = 0; j < n; j++) {
      const a = R[k][j], b = R[k][(j + 1) % n], c = R[k + 1][(j + 1) % n], d = R[k + 1][j];
      tri(a, b, c);
      tri(a, c, d);
    }
  }
  // 两头封口：从中心点扇形铺满（截面是凸的）
  const lo = R[0], hi = R[R.length - 1];
  const c0 = [x, y, lo[0][2]], c1 = [x, y, hi[0][2]];
  for (let j = 0; j < n; j++) {
    tri(c0, lo[(j + 1) % n], lo[j]);
    tri(c1, hi[j], hi[(j + 1) % n]);
  }
  const g = new THREE.BufferGeometry();
  g.setAttribute('position', new THREE.Float32BufferAttribute(pos, 3));
  g.computeVertexNormals();   // 预览里直接拿来画
  return g;
}

// 四根后盖插销的中心 [x, y]
export const pinCenters = () => [[-1, -1], [-1, 1], [1, -1], [1, 1]].map(([sx, sy]) => [sx * D.pin.at[0], sy * D.pin.at[1]]);

// 透光窗的孔和正面橙圈占的那一圈（一个实心圆片），挖主体和做橙圈用同一个，接缝才严丝合缝。
// 孔往外多出 0.6 挖干净；往里一直挖到板子正面那个平面（离内壁 standoff）。
// 芯片离板边只有 1.3，窗口半径 3，所以每个窗口都有一条最宽约 1.55 的月牙伸出板子边，月牙后面原来是实心。
// 原来孔只挖穿外壁（往里只多 0.01，为了不在实心里留悬空的小坑），结果月牙那块实心贴着外壁内侧，
// 从外面看窗口被切掉一块、不是圆的，也挡了芯片那一侧的光（2026-11-20 发现）。现在挖到板子正面：从外面看是 ⌀6、深 2.9 的平底圆孔。
// 代价：正面窗口的月牙底在正面朝下打印时是一小条悬空（最宽 1.55，圆弧那边托着），藏在孔底；右面、顶面的月牙打印时在下面，不悬空
const WIN_IN = IH - D.sensor.standoff, WIN_OUT = H + 0.6;
const windowGeo = () => new THREE.CylinderGeometry(D.win.r, D.win.r, WIN_OUT - WIN_IN, 48);
const windowAt = (sn) => sn.n.clone().multiplyScalar((WIN_IN + WIN_OUT) / 2).add(sn.win).toArray();
const ringDisc = () => {
  const d = D.accent.depth;
  const win = SENSORS.find((sn) => sn.id === 'A').win;
  const g = new THREE.CylinderGeometry(D.win.r + D.accent.ring, D.win.r + D.accent.ring, d + 0.6, 96);
  return solid(g, [win.x, win.y, H - d + (d + 0.6) / 2], quatTo(AZ));   // 往外多出 0.6，挖得干净
};

// ---------- 传感器方块的外壳：主体、正面橙圈、后盖 ----------
// 返回三个实体（方块自己的坐标）。正面那块的柱子、后盖上的小凸起和插销在 fixtureGeos 里，没有并进来。
export function shellSolids() {
  const C = D.cube;

  // 外形：圆角方形沿前后方向拉伸，前后两圈外沿倒 0.8 的角（正面朝下打印，竖边圆角、贴板的一面是倒角）
  const { r, ch } = D.edge;
  const outer = new THREE.ExtrudeGeometry(roundRect(C - 2 * ch, C - 2 * ch, r - ch), {
    depth: C - 2 * ch, bevelEnabled: true, bevelThickness: ch, bevelSize: ch, bevelOffset: 0, bevelSegments: 1, curveSegments: 12,
  });
  outer.translate(0, 0, -(C - 2 * ch) / 2);
  outer.deleteAttribute('uv');
  let s = solid(outer);
  // 里面不是掏空的：整块实心，只挖 innerVoids() 列出的地方
  for (const [x0, x1, y0, y1, z0, z1] of innerVoids()) {
    s = op(s, solid(new THREE.BoxGeometry(x1 - x0, y1 - y0, z1 - z0), [(x0 + x1) / 2, (y0 + y1) / 2, (z0 + z1) / 2]), SUBTRACTION);
  }

  // 三个透光窗：直孔，不倒角，对准各自的芯片（往芯片那头偏离各面正中）
  for (const sn of SENSORS) {
    s = op(s, solid(windowGeo(), windowAt(sn), quatTo(sn.n)), SUBTRACTION);
  }

  // 背面的 USB 口开孔
  s = op(s, solid(new THREE.BoxGeometry(D.usbSlot.w, D.usbSlot.h, D.wall + 1.2), [0, PORT_Y, -H + D.wall / 2]), SUBTRACTION);

  // 接缝外面一圈细槽：一半在主体、一半在后盖，指甲伸进去就能撬开后盖
  const [gw, gd] = D.pin.seamGroove;
  const slab = new THREE.ExtrudeGeometry(roundRect(C + 4, C + 4, r + 2), { depth: gw, bevelEnabled: false, curveSegments: 12 });
  const keep = new THREE.ExtrudeGeometry(roundRect(C - 2 * gd, C - 2 * gd, r - gd), { depth: gw + 1, bevelEnabled: false, curveSegments: 12 });
  for (const g of [slab, keep]) g.deleteAttribute('uv');
  slab.translate(0, 0, -IH + 0.4 - gw / 2);
  keep.translate(0, 0, -IH + 0.4 - gw / 2 - 0.5);
  s = op(s, op(solid(slab), solid(keep), SUBTRACTION), SUBTRACTION);

  // 后盖散热孔：只穿过后盖那块背板（后盖外面朝下打印，孔是竖的，不用支撑）。先把所有孔并成一个再一次挖掉
  const V_ = D.vent;
  let vents = null;
  for (let i = 0; i < V_.cols; i++) {
    for (let j = 0; j < V_.rows; j++) {
      const x = (i - (V_.cols - 1) / 2) * V_.pitch, y = V_.cy + (j - (V_.rows - 1) / 2) * V_.pitch;
      const g = new THREE.CylinderGeometry(V_.d / 2, V_.d / 2, 3, 24);
      g.rotateX(Math.PI / 2);
      const b = solid(g, [x, y, -IH - 0.5]);   // 穿透后盖，一直通到后面的走线舱
      vents = vents ? op(vents, b, ADDITION) : b;
    }
  }
  s = op(s, vents, SUBTRACTION);

  // 后盖：背面整块可以拆下来，USB 口开在它上面。主板从后面推进去，前端顶到前壁，后盖盖上后卡住 USB 口。
  // 胶贴在底面或左侧面，都在方块主体上，后盖不受力
  const cutter = () => solid(new THREE.BoxGeometry(40, 40, 14), [0, 0, -IH + 0.4 - 7]);
  const lid = op(s, cutter(), INTERSECTION);
  let body = op(s, cutter(), SUBTRACTION);
  // 后盖插销孔：开在主体四个角的实心方柱上，从接缝往前，每边比柱子大 clear；孔口倒 mouth 的 45 度角。
  // 倒角那段从接缝后面 0.1 就开始斜，接缝面上正好大 mouth，免得截面和接缝面重合
  const P = D.pin;
  const hw = P.w + 2 * P.clear, hh = P.h + 2 * P.clear, hr = P.r + P.clear, m = P.mouth + 0.1;
  for (const [px, py] of pinCenters()) {
    body = op(body, solid(loftGeo(px, py, [
      [hw + 2 * m, hh + 2 * m, hr + m, SEAM_Z - 0.5],
      [hw + 2 * m, hh + 2 * m, hr + m, SEAM_Z - 0.1],
      [hw, hh, hr, SEAM_Z + P.mouth],
      [hw, hh, hr, SEAM_Z + P.depth],
    ])), SUBTRACTION);
  }

  // 全身唯一的橙色：正面进光口外面一圈。嵌进正面 accent.depth（正面朝下打印，就是头两层换色），
  // 主体上挖掉这一圈，橙圈是单独一个实体，正好填回去
  body = op(body, ringDisc(), SUBTRACTION);
  const winA = SENSORS.find((sn) => sn.id === 'A');
  const ring = op(
    op(ringDisc(), solid(new THREE.BoxGeometry(20, 20, 2), [winA.win.x, winA.win.y, H + 1]), SUBTRACTION),   // 去掉多出正面的部分
    solid(windowGeo(), windowAt(winA), quatTo(winA.n)),
    SUBTRACTION,
  );

  return { body, lid, ring };
}

// ---------- 方块里面：整块实心，只挖需要的地方 ----------
// 像 CNC 铣一样：主体从一整块实心开始，只挖下面这些（在 shellSolids 里挖），其它全是实心。
//  1. 正面那块板：板子从后面推进去的通道（板子外面留 gap），一直通到前壁；板子前面再挖元件让位，上下两条长边留 edge 宽托住板边
//  2. 右面、顶面两块板：插槽（板子外面留 gap），从后面挖到芯片那头的挡头；朝外那面挖元件让位，两条长边留 edge 托边
//  3. 主板：板子躺在槽底；上面元件、焊盘上的线、USB 口的空间一直通到正面那块板的通道底；前面两个角不挖，留着压住板子
//  4. 后面一段走线舱：三块传感器板的排针和线都在这里
// 留下的实心自然就是：右面、顶面两块板背后各一块实心板托住它们，四个角是实心方柱（开后盖插销孔）。
// 打印时正面朝下：挖的地方全都通到接缝（朝上开口），往前的尽头都是平底，没有悬空，不用支撑。
// 所有挖的地方都从后盖内面（-IH）开始，所以后盖在实心的地方会多出 0.4 高，正好和主体的实心对上。

// 一块传感器板占的空间（包围盒）
export function boardBox(sn) {
  const S = D.sensor;
  const half = new THREE.Vector3()
    .addScaledVector(V(Math.abs(sn.a.x), Math.abs(sn.a.y), Math.abs(sn.a.z)), S.L / 2)
    .addScaledVector(V(Math.abs(sn.w.x), Math.abs(sn.w.y), Math.abs(sn.w.z)), S.W / 2)
    .addScaledVector(V(Math.abs(sn.n.x), Math.abs(sn.n.y), Math.abs(sn.n.z)), S.t / 2);
  return new THREE.Box3(sn.pcb.clone().sub(half), sn.pcb.clone().add(half));
}

// 要挖掉的地方，每个是 [x0, x1, y0, y1, z0, z1]
export function innerVoids() {
  const g = D.fix.gap, edge = D.fix.edge, z0 = -IH - 0.05;
  const [A, B, Cs] = ['A', 'B', 'C'].map((id) => boardBox(SENSORS.find((s) => s.id === id)));
  const c = D.c3, yb = -IH + c.lift, yt = yb + c.t, half = c.W / 2 + g;
  const chBottom = A.min.y - g;   // 正面那块板通道的底，主板上面的空间一直通到这里
  return [
    // 1. 正面那块板：通道 + 前面元件让位
    [A.min.x - g, A.max.x + g, A.min.y - g, A.max.y + g, z0, A.max.z],
    [A.min.x - g, A.max.x + g, A.min.y + edge, A.max.y - edge, A.max.z - 0.01, IH],
    // 2. 右面那块：插槽 + 朝外元件让位
    [B.min.x - g, B.max.x + g, B.min.y - g, B.max.y + g, z0, B.max.z + g],
    [B.max.x + g - 0.01, IH, B.min.y + edge, B.max.y - edge, z0, B.max.z + g],
    //    顶面那块：插槽 + 朝外元件让位
    [Cs.min.x - g, Cs.max.x + g, Cs.min.y - g, Cs.max.y + g, z0, Cs.max.z + g],
    [Cs.min.x + edge, Cs.max.x - edge, Cs.max.y + g - 0.01, IH, z0, Cs.max.z + g],
    // 3. 主板：槽（板子躺在槽底）+ 上面一直通到正面板通道底；最前面只挖中间，两个角留着压住板子
    [-half, half, yb, chBottom + 0.01, z0, D.c3.tongue],
    [-(c.W / 2 - 1.1), c.W / 2 - 1.1, yb, chBottom + 0.01, D.c3.tongue - 0.01, -IH + c.L + g],
    [-half, half, yb, yt + g, D.c3.tongue - 0.01, -IH + c.L + g],
    // 4. 走线舱：接缝往前一段，左右上下到四个角方柱为止
    [-D.pin.corner, B.min.x - g + 0.01, -D.pin.corner, Cs.min.y - g + 0.01, z0, D.fix.bay],
  ];
}

// 挖完以后还要加上的小件：正面那块板的柱子（主体上），后盖上顶住板子尾巴的小凸起和四根插销（后盖上）。
// 返回摆好位置的几何体，分成长在主体上的和长在后盖上的。
// staked = true 画的是装好以后的样子（柱头已经用烙铁压成帽）；false 是打印出来的样子（柱子穿过板子再多伸出 D.fix.stake）。
export function fixtureGeos({ staked = true } = {}) {
  const body = [];
  const lid = [];
  const S = D.sensor, F = D.fix;
  const zAxis = (geo) => geo.rotateX(Math.PI / 2);   // 圆柱轴从 y 转到 z
  const box = (x0, x1, y0, y1, z0, z1, list) => {
    const b = new THREE.BoxGeometry(x1 - x0, y1 - y0, z1 - z0);
    b.translate((x0 + x1) / 2, (y0 + y1) / 2, (z0 + z1) / 2);
    list.push(b);
  };
  const cyl = (x, y, r, z0, z1, list, r2 = r) => {
    const m = zAxis(new THREE.CylinderGeometry(r2, r, z1 - z0, 32));   // r 在 z0 那头，r2 在 z1 那头
    m.translate(x, y, (z0 + z1) / 2);
    list.push(m);
  };

  SENSORS.forEach((sn) => {
    const bb = boardBox(sn);
    if (sn.id === 'A') {
      // 正面那块：安装孔那里留一圈垫台托住板子（元件让位里唯一不挖的地方），中间的柱子穿过孔，柱头用烙铁压平
      const [hx, hy] = onBoard(S.hole.u, S.hole.v);
      const p = V(hx, hy, 0).applyMatrix4(sn.m);
      cyl(p.x, p.y, F.boss / 2, bb.max.z, IH + 0.3, body);
      if (staked) {
        cyl(p.x, p.y, F.post / 2, bb.min.z - 0.05, bb.max.z, body);
        cyl(p.x, p.y, F.post / 2 + 0.6, bb.min.z - 0.7, bb.min.z, body, F.post / 2 + 1.1);
      } else {
        cyl(p.x, p.y, F.post / 2, bb.min.z - F.stake, bb.max.z + 0.05, body);
      }
    } else {
      // 右面、顶面两块：后盖内侧的小凸起伸进插槽，顶住板子尾巴，板子就不会往后滑
      const c = sn.pcb, z1 = bb.min.z - 0.05, z0 = -IH - 0.3;
      if (sn.id === 'B') box(bb.min.x + 0.2, bb.max.x - 0.2, c.y - 3, c.y + 3, z0, z1, lid);
      else box(c.x - 3, c.x + 3, bb.min.y + 0.2, bb.max.y - 0.2, z0, z1, lid);
    }
  });

  // 后盖插销：四个角各一根扁柱子，插进主体四个角实心方柱上的盲孔（孔在 shellSolids 里挖），靠摩擦力夹住。
  // 从后往前：埋在后盖里的一截 → 根部 45 度往外放 root（从接缝后面 0.1 就开始斜，接缝面上正好大 root）→ 直的一段 → 柱头收小 tip
  const P = D.pin, k = P.root + 0.1;
  for (const [px, py] of pinCenters()) {
    lid.push(loftGeo(px, py, [
      [P.w + 2 * k, P.h + 2 * k, P.r + k, SEAM_Z - 0.5],
      [P.w + 2 * k, P.h + 2 * k, P.r + k, SEAM_Z - 0.1],
      [P.w, P.h, P.r, SEAM_Z + P.root],
      [P.w, P.h, P.r, SEAM_Z + P.len - P.tip],
      [P.w - 2 * P.tip, P.h - 2 * P.tip, P.r - P.tip, SEAM_Z + P.len],
    ]));
  }
  return { body, lid };
}

// ---------- 理线扣 ----------
// 理线扣：贴在显示器背面，把 USB 线固定住，线的重量和拉扯到这里为止，传不到方块上。
// 自己的坐标：原点在粘贴面的中心，x 沿显示器宽度，y 沿背面朝上，z 垂直背面、背离显示器。
export function clipSolid() {
  const c = D.clip;
  const base = new THREE.ExtrudeGeometry(roundRect(c.w, c.h, 2.5), {
    depth: c.t - 0.8, bevelEnabled: true, bevelThickness: 0.4, bevelSize: 0.4, bevelOffset: -0.4, bevelSegments: 2, curveSegments: 10,
  });
  let s = solid(base, [0, 0, c.tape + 0.4]);
  // C 形的扣，开口朝外，USB 线从外面按进去
  s = op(s, solid(new THREE.CylinderGeometry(c.r, c.r, c.len, 32), [0, 0, CLIP_Z]), ADDITION);
  s = op(s, solid(new THREE.CylinderGeometry(c.bore, c.bore, c.len + 2, 24), [0, 0, CLIP_Z]), SUBTRACTION);
  s = op(s, solid(new THREE.BoxGeometry(c.slot, c.len + 2, c.r + 1), [0, 0, CLIP_Z + (c.r + 1) / 2]), SUBTRACTION);
  return s;
}

// ---------- 打印件 ----------
// 要打印的零件，都并成一个实体、摆成打印朝向（贴打印板的一面在 z = 0，水平方向居中）。
// 同一个零件按颜色拆成几块时（主体的黑和橙），几块一起挪，坐标是对齐的。
// 返回 { 文件名: { 零件名: 实体 } }，零件名为空表示整件一种颜色。
export function printParts() {
  const { body, lid, ring } = shellSolids();
  const fx = fixtureGeos({ staked: false });
  let bodyAll = body, lidAll = lid;
  for (const g of fx.body) bodyAll = op(bodyAll, solid(g), ADDITION);
  for (const g of fx.lid) lidAll = op(lidAll, solid(g), ADDITION);

  // flip：先绕 x 转 180 度（主体正面从 +z 转到 -z，朝下），同一组的零件一起转、一起挪
  const place = (group, flip = false) => {
    let parts = Object.entries(group);
    if (flip) parts = parts.map(([k, m]) => [k, m.rotate([180, 0, 0])]);
    const { min, max } = parts[0][1].boundingBox();
    const d = [-(min[0] + max[0]) / 2, -(min[1] + max[1]) / 2, -min[2]];
    return Object.fromEntries(parts.map(([k, m]) => [k, m.translate(d)]));
  };
  return {
    // 文件名用英文（打印文件也给英文、日文读者用）：01-body-black / 01-body-orange-ring / 02-back-cover / 03-cable-clip
    '01-body': place({ 'black': bodyAll, 'orange-ring': ring }, true),   // 主体：黑色本体 + 正面那圈橙
    '02-back-cover': place({ '': lidAll }),   // 后盖外面朝下，本来就是这个朝向
    '03-cable-clip': place({ '': clipSolid() }),   // 理线扣，底板朝下
  };
}
