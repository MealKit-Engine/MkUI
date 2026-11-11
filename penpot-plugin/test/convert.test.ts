// Converter tests: fixed Penpot snapshot data → .ui. The expected files (test/expected/*.ui) are reread with the real parser by the engine unit test (penpot_expected)
// to check for errors. If the conversion result is changed on purpose, rewrite the expected files with UPDATE=1.
//   node --test test/            (in tools/penpot-plugin)
//   set UPDATE=1 && node --test test/

import { test } from 'node:test';
import assert from 'node:assert/strict';
import { readFileSync, writeFileSync, mkdirSync } from 'node:fs';
import { dirname, join } from 'node:path';
import { fileURLToPath } from 'node:url';
import { convertBoard, convertTheme, parseCssColor, color, num, fontFile, looksLikeKey } from '../src/convert.ts';
import type { Options, SNode, STextRun, SLayout } from '../src/model.ts';

const here = dirname(fileURLToPath(import.meta.url));

function node(p: Partial<SNode> & { name: string }): SNode {
  return {
    id: 'id-' + p.name.replace(/\s+/g, '-'),
    type: 'rectangle',
    x: 0,
    y: 0,
    absX: p.x ?? 0,
    absY: p.y ?? 0,
    width: 100,
    height: 100,
    rotation: 0,
    opacity: 1,
    blendMode: 'normal',
    hidden: false,
    clip: false,
    mask: false,
    constraintsH: 'left',
    constraintsV: 'top',
    fills: [],
    strokes: [],
    shadows: [],
    blur: false,
    backgroundBlur: false,
    radius: [0, 0, 0, 0],
    children: [],
    ...p,
  };
}

function run(characters: string, start: number, p: Partial<STextRun> = {}): STextRun {
  return {
    start,
    end: start + characters.length,
    characters,
    fontFamily: 'Pretendard',
    fontWeight: '500',
    fontStyle: 'normal',
    fontSize: 32,
    lineHeight: 1.2,
    letterSpacing: 0,
    textTransform: 'none',
    textDecoration: 'none',
    fill: { hex: '#f2f4f8', opacity: 1 },
    ...p,
  };
}

function layout(p: Partial<SLayout> & { kind: 'flex' | 'grid' }): SLayout {
  return {
    dir: 'column',
    wrap: false,
    rowGap: 0,
    columnGap: 0,
    padding: [0, 0, 0, 0],
    justifyContent: 'start',
    alignItems: 'start',
    alignContent: 'start',
    justifyItems: 'start',
    rows: [],
    columns: [],
    ...p,
  };
}

const flowChild = (h: 'fix' | 'fill' | 'auto', v: 'fix' | 'fill' | 'auto') => ({
  absolute: false,
  horizontalSizing: h,
  verticalSizing: v,
  alignSelf: 'auto',
  justifySelf: 'auto',
  margin: [0, 0, 0, 0] as [number, number, number, number],
  minWidth: null,
  maxWidth: null,
  minHeight: null,
  maxHeight: null,
});

const opt: Options = { file: 'ui/pause.ui', fileName: 'Sandbox UI', fileId: 'file-1', fonts: ['Pretendard-Medium'], nodeBudget: 500 };

// Pause screen: Flex panel, text with keys and style spans, a button with a hover variant, image fill (baking), a flattened group, Grid, blur warning
function pauseBoard(): SNode {
  const title = node({
    name: 'pause.title',
    type: 'text',
    width: 436,
    height: 48,
    child: flowChild('fill', 'auto'),
    text: { characters: 'Paused now', growType: 'auto-height', align: 'center', verticalAlign: 'top', runs: [run('Paused ', 0, { fontSize: 40 }), run('now', 7, { fontSize: 40, fill: { hex: '#ffc83d', opacity: 1, token: 'accent' } })] },
  });
  const label = node({
    name: 'pause.resume',
    type: 'text',
    width: 120,
    height: 38,
    text: { characters: 'Resume', growType: 'auto-width', align: 'left', verticalAlign: 'top', runs: [run('Resume', 0, { fill: { hex: '#10141c', opacity: 1 } })] },
    child: flowChild('auto', 'auto'),
  });
  const hover = node({ name: 'resume', type: 'board', fills: [{ color: { hex: '#ffd86a', opacity: 1 } }], radius: [12, 12, 12, 12] });
  const resume = node({
    name: 'resume',
    type: 'board',
    width: 436,
    height: 60,
    fills: [{ color: { hex: '#ffc83d', opacity: 1, token: 'accent' } }],
    radius: [12, 12, 12, 12],
    layout: layout({ kind: 'flex', dir: 'row', justifyContent: 'center', alignItems: 'center' }),
    child: flowChild('fill', 'fix'),
    data: { widget: 'button', action: 'pause.resume', focusDown: 'leave' },
    states: [
      { state: 'Default', node: node({ name: 'resume', type: 'board', fills: [{ color: { hex: '#ffc83d', opacity: 1, token: 'accent' } }], radius: [12, 12, 12, 12] }) },
      { state: 'Hover', node: hover },
    ],
    children: [label],
  });
  const icon = node({ name: 'Pause Icon', type: 'path', width: 24, height: 24, child: { ...flowChild('fix', 'fix'), absolute: true }, x: 400, y: 20, fills: [{ color: { hex: '#ffffff', opacity: 1 } }] });
  const grid = node({
    name: 'stats',
    type: 'board',
    width: 436,
    height: 100,
    child: flowChild('fill', 'auto'),
    boardSizingV: 'auto',
    layout: layout({ kind: 'grid', dir: 'row', columns: [{ type: 'flex', value: 1 }, { type: 'fixed', value: 120 }], rows: [{ type: 'auto', value: 0 }, { type: 'auto', value: 0 }], rowGap: 8, columnGap: 16, justifyItems: 'stretch', alignItems: 'center' }),
    children: [
      node({ name: 'a', width: 50, height: 20, child: flowChild('fill', 'fix'), cell: { row: 1, column: 1, rowSpan: 1, columnSpan: 1 }, fills: [{ color: { hex: '#ffffff', opacity: 0.15 } }] }),
      node({ name: 'b', width: 50, height: 20, child: flowChild('fix', 'fix'), cell: { row: 2, column: 1, rowSpan: 1, columnSpan: 2 } }),
    ],
  });
  const panel = node({
    name: 'panel',
    type: 'board',
    x: 710,
    y: 300,
    width: 500,
    height: 400,
    constraintsH: 'center',
    constraintsV: 'center',
    boardSizingV: 'auto',
    fills: [{ color: { hex: '#10141c', opacity: 0.8 } }],
    strokes: [{ color: { hex: '#ffffff', opacity: 0.15 }, width: 1, alignment: 'inner', style: 'solid' }],
    radius: [16, 16, 16, 16],
    shadows: [{ style: 'drop-shadow', offsetX: 0, offsetY: 12, blur: 40, spread: 0, color: { hex: '#000000', opacity: 0.67 } }],
    backgroundBlur: true,
    layout: layout({ kind: 'flex', dir: 'column', rowGap: 14, columnGap: 14, padding: [32, 36, 32, 36] }),
    children: [icon, grid, resume, title], // from the bottom layer: in Flex, title comes first
  });
  const dim = node({ name: 'dim', type: 'rectangle', width: 1920, height: 1080, constraintsH: 'leftright', constraintsV: 'topbottom', fills: [{ color: { hex: '#05070c', opacity: 0.6 } }] });
  const photo = node({ name: 'photo', type: 'rectangle', x: 10, y: 20, width: 64, height: 64, fills: [{ image: true }], data: { slice: [8, 8, 8, 8] } });
  const deco = node({ name: 'Group 1', type: 'group', x: 100, y: 900, width: 200, height: 50, children: [node({ name: 'dot', type: 'ellipse', x: 5, y: 5, width: 20, height: 20, fills: [{ color: { hex: '#ffffff', opacity: 1 } }] }), photo] });
  return node({ name: 'pause', type: 'board', width: 1920, height: 1080, data: { navigation: true, firstFocus: 'resume', back: 'pause.resume' }, children: [dim, panel, deco] });
}

function expect(name: string, text: string) {
  const file = join(here, 'expected', name);
  if (process.env.UPDATE === '1') {
    mkdirSync(dirname(file), { recursive: true });
    writeFileSync(file, text);
  }
  assert.equal(text, readFileSync(file, 'utf8').replace(/\r\n/g, '\n'), `${name} differs (UPDATE=1 to rewrite)`);
}

test('pause board', () => {
  const r = convertBoard(pauseBoard(), opt);
  expect('pause.ui', r.ui);
  assert.deepEqual(r.errors, []);
  assert.deepEqual(
    r.bakes.map((b) => [b.path, b.skipChildren]),
    [
      ['ui/pause/Pause_Icon.png', false],
      ['ui/pause/photo.png', false],
    ],
  );
  assert.deepEqual(r.strings, [
    { key: 'pause.title', text: 'Paused <1>now</1>' },
    { key: 'pause.resume', text: 'Resume' },
  ]);
  assert.ok(r.warnings.some((w) => w.includes('background blur')));
  assert.ok(!r.warnings.some((w) => w.includes('variant state')), 'Default is the normal state');
  // A flattened group has no box either. Child positions are moved by the group's offset
  assert.ok(!r.boxes.some((b) => b.name === 'Group_1'));
  assert.match(r.ui, /frame dot\n {4}id id-dot\n {4}pos 105 905\n/);
});

test('fonts and keys', () => {
  const r = convertBoard(node({ name: 'x', type: 'board', children: [node({ name: 'hello', type: 'text', text: { characters: 'Hi', growType: 'fixed', align: 'left', verticalAlign: 'top', runs: [run('Hi', 0, { fontWeight: '700', fontStyle: 'italic' })] } })] }), opt);
  assert.deepEqual(r.errors, ["x/hello: font 'Pretendard-BoldItalic' is not in assets/fonts"]);
  assert.ok(r.warnings.some((w) => w.includes('not a string key')));
  assert.match(r.ui, /content Hi\n/);
  assert.equal(fontFile(run('a', 0, { fontWeight: '400' })), 'Pretendard-Regular');
  assert.ok(looksLikeKey('menu.start') && !looksLikeKey('Start') && !looksLikeKey('menu start'));
});

test('budget and rotation', () => {
  const kids = Array.from({ length: 6 }, (_, i) => node({ name: 'r' + i, fills: [{ color: { hex: '#ffffff', opacity: 1 } }], rotation: i === 0 ? 45 : 0 }));
  const r = convertBoard(node({ name: 'x', type: 'board', children: kids }), { ...opt, nodeBudget: 5 });
  assert.ok(r.warnings.some((w) => w.includes('over the budget of 5')));
  assert.ok(!r.boxes.some((b) => b.name === 'r0'), 'rotated nodes are not compared');
  assert.match(r.ui, /rotation 45\n/);
});

test('colors, numbers and theme', () => {
  assert.equal(color({ hex: '#10141c', opacity: 0.8 }), '#10141CCC');
  assert.equal(color({ hex: '#ffffff', opacity: 1 }), '#FFFFFF');
  assert.equal(num(1.005), 1.01 === Math.round(1.005 * 100) / 100 ? '1.01' : '1');
  assert.equal(num(-0.001), '0');
  assert.equal(parseCssColor('#abc'), '#AABBCC');
  assert.equal(parseCssColor('rgba(255, 0, 0, 0.5)'), '#FF000080');
  assert.equal(parseCssColor('#112233ff'), '#112233');
  const t = convertTheme(
    [
      { name: 'accent', type: 'color', value: '#ffc83d' },
      { name: 'radius', type: 'borderRadius', value: '12' },
      { name: 'bad', type: 'color', value: 'hsl(1,2,3)' },
    ],
    'Sandbox UI',
  );
  expect('theme.ui', t.ui);
  assert.equal(t.warnings.length, 1);
});
