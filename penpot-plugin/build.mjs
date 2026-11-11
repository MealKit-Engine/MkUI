// Plugin build: src → dist (dist is committed to the repo. The MkUI Designer serves it as is without Node.js).
//   npm install   (once at first, in penpot-plugin)
//   npm run build
// __MEALKIT_BASE__ in dist/plugin.js is replaced with its own address when the MkUI Designer serves it.

import { build } from 'esbuild';
import { copyFileSync, mkdirSync, readFileSync, writeFileSync } from 'node:fs';

const common = { bundle: true, format: 'iife', target: 'es2020', charset: 'utf8', legalComments: 'none', logLevel: 'warning' };
mkdirSync('dist', { recursive: true });
await build({ ...common, entryPoints: ['src/plugin.ts'], outfile: 'dist/plugin.js' });
const ui = await build({ ...common, entryPoints: ['src/ui.ts'], write: false });
const script = ui.outputFiles[0].text.replace(/<\/script/gi, '<\\/script');
const html = readFileSync('src/ui.html', 'utf8').replace('<script>/*UI_SCRIPT*/</script>', () => `<script>\n${script}</script>`);
writeFileSync('dist/ui.html', html);
copyFileSync('src/manifest.json', 'dist/manifest.json');
copyFileSync('src/icon.png', 'dist/icon.png');
console.log('dist: manifest.json, plugin.js, ui.html, icon.png');
