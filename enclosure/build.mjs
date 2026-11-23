// 把 src/main.js 和依赖打成一段脚本，塞进 src/template.html，生成不依赖网络的预览页。
// 用法（在 enclosure 目录，先 npm install）：node build.mjs [输出文件] [模板]，默认写到 out/预览.html（不进 git）。
// 个人网站上嵌的展示版换模板：node build.mjs <网站>/public/ambient/3d.html src/showcase.html
// 实体运算用的 manifold-3d 是 WebAssembly，wasm 文件也直接打包进页面（main.js 里 import 进来），所以用 ES 模块格式、页面里是 <script type="module">。
import { build } from 'esbuild';
import { mkdir, readFile, writeFile } from 'node:fs/promises';
import { dirname, join } from 'node:path';
import { fileURLToPath } from 'node:url';

const OUT = process.argv[2] || join(dirname(fileURLToPath(import.meta.url)), 'out/预览.html');

const out = await build({
  entryPoints: ['src/main.js'],
  bundle: true,
  format: 'esm',
  minify: true,
  target: 'es2022',
  write: false,
  legalComments: 'eof',   // 保留 three.js、manifold-3d 的许可证声明
  loader: { '.wasm': 'binary' },
  external: ['node:module'],   // manifold-3d 在 Node 里才会用到，浏览器里不走那段
});
const js = out.outputFiles[0].text.replace(/<\/script/gi, '<\\/script');
const tpl = await readFile(process.argv[3] || 'src/template.html', 'utf8');
await mkdir(dirname(OUT), { recursive: true });
await writeFile(OUT, tpl.replace('/*__BUNDLE__*/', () => js));
console.log(`${OUT} ${(js.length / 1024).toFixed(0)} KB`);
