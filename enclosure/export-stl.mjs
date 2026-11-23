// 导出打印文件（STL）：主体（黑、橙圈两个文件）、后盖、理线扣。
//
// 用法（在 enclosure 目录）：
//     node export-stl.mjs            → 写到仓库的 hardware/stl/
//     node export-stl.mjs 某个目录     → 写到指定目录
//
// 每个文件都已经摆成打印朝向（贴打印板的一面在 z = 0）：主体正面朝下，后盖外面朝下，理线扣底板朝下。
// STL 不记颜色，所以主体按颜色拆成两个文件，坐标是对齐的：
// 在 Bambu Studio 里同时选中 01-body-black、01-body-orange-ring 两个文件导入，问"是否作为一个多零件对象加载"时选"是"，再给橙圈指定橙色耗材。
// 橙圈体积很小，Bambu Studio 如果问要不要按英寸换算成毫米，选"否"。
// 重新导出会覆盖同名文件；改过零件名的话，旧名字的文件要自己删。
import { mkdir, writeFile } from 'node:fs/promises';
import { dirname, join } from 'node:path';
import { fileURLToPath } from 'node:url';
import * as THREE from 'three';
import { STLExporter } from 'three/addons/exporters/STLExporter.js';
import { initKernel, printParts, toGeometry } from './src/parts.js';

const OUT_DIR = process.argv[2] || join(dirname(fileURLToPath(import.meta.url)), '../hardware/stl');

// 读二进制 STL：三角面数、有几条边不是正好被两个三角面共用（0 才算封闭）、体积。
// 顶点按精确坐标比：manifold 出来的网格顶点是共用的，坐标完全一样；有的地方两个点只差不到 0.0001，四舍五入会把它们并成一个、误报没封好
function checkStl(buf) {
  const dv = new DataView(buf.buffer, buf.byteOffset, buf.byteLength);
  const n = dv.getUint32(80, true);
  const edges = new Map();
  const key = (p) => p.join(',');
  let vol = 0;
  for (let i = 0; i < n; i++) {
    const o = 84 + i * 50 + 12;
    const p = [0, 1, 2].map((j) => [0, 1, 2].map((k) => dv.getFloat32(o + (j * 3 + k) * 4, true)));
    const [a, b, c] = p;
    vol += (a[0] * (b[1] * c[2] - b[2] * c[1]) - a[1] * (b[0] * c[2] - b[2] * c[0]) + a[2] * (b[0] * c[1] - b[1] * c[0])) / 6;
    const ks = p.map(key);
    for (const [s, t] of [[0, 1], [1, 2], [2, 0]]) {
      if (ks[s] === ks[t]) continue;   // 退化的三角面，边长为 0
      const e = ks[s] < ks[t] ? `${ks[s]}|${ks[t]}` : `${ks[t]}|${ks[s]}`;
      edges.set(e, (edges.get(e) || 0) + 1);
    }
  }
  let bad = 0;
  for (const c of edges.values()) if (c !== 2) bad++;
  return { n, bad, vol };
}

await initKernel();
const exporter = new STLExporter();
await mkdir(OUT_DIR, { recursive: true });
console.log('→', OUT_DIR);
let failed = false;
for (const [fname, parts] of Object.entries(printParts())) {
  for (const [pname, solid] of Object.entries(parts)) {
    const file = pname ? `${fname}-${pname}.stl` : `${fname}.stl`;
    const geo = toGeometry(solid);
    const dv = exporter.parse(new THREE.Mesh(geo), { binary: true });
    const buf = Buffer.from(dv.buffer, dv.byteOffset, dv.byteLength);
    await writeFile(join(OUT_DIR, file), buf);
    const { n, bad, vol } = checkStl(buf);
    const { min, max } = solid.boundingBox();
    const pieces = solid.decompose().length;
    console.log(`  ${file}: ${(max[0] - min[0]).toFixed(1)} × ${(max[1] - min[1]).toFixed(1)} × ${(max[2] - min[2]).toFixed(2)}，`
      + `${(vol / 1000).toFixed(2)} cm³，${pieces} 块，${n} 个三角面${bad ? `，有 ${bad} 条边没有封好！` : '，封闭'}`);
    if (bad || pieces !== 1) failed = true;
  }
}
if (failed) {
  console.error('有零件没封好或者不是一整块，先别打印');
  process.exit(1);
}
