// Plugin panel (iframe, served by the MkUI Designer): edits engine values (bindings/widgets) of the selected layer and exports screen boards.
// If the MkUI Designer is running, gets fonts/actions from /api/project for the dropdowns, and sends exported files to /api/export.
// Without it, downloads a zip (just extract it into the project folder).

import { buildImport, buildZip, text, type ImportEntry } from './encode.ts';
import type { BoardInfo, ExportedScreen, LayerInfo, ToPlugin, ToUi } from './messages.ts';
import type { MealkitData } from './model.ts';

interface ProjectInfo {
  name: string;
  language: string;
  fonts: string[];
  actions: string[];
  uiFiles: string[];
  viewModels: { name: string; fields: { name: string; kind: string }[] }[];
}

const $ = <T extends HTMLElement>(id: string) => document.getElementById(id) as T;
let project: ProjectInfo | null = null;
let layers: LayerInfo[] = [];
let boards: BoardInfo[] = [];
const checked = new Set<string>();

function post(msg: ToPlugin) {
  parent.postMessage(msg, '*');
}

function el<K extends keyof HTMLElementTagNameMap>(tag: K, attrs: Record<string, string> = {}, ...kids: (Node | string)[]): HTMLElementTagNameMap[K] {
  const e = document.createElement(tag);
  for (const [k, v] of Object.entries(attrs)) e.setAttribute(k, v);
  for (const k of kids) e.append(k);
  return e;
}

// ---------------------------------------------------------------- Connection

async function connect() {
  const status = $('status');
  try {
    const r = await fetch('api/project', { cache: 'no-store' });
    if (!r.ok) throw new Error(String(r.status));
    project = (await r.json()) as ProjectInfo;
    status.textContent = `MkUI Designer: ${project.name} (exports go straight into the project)`;
    status.className = 'ok';
  } catch {
    project = null;
    status.textContent = 'MkUI Designer not found: Export downloads a zip to unpack in the project folder.';
    status.className = 'warn';
  }
  fillLists();
  renderLayer();
}

function fillLists() {
  const fields = $('fields');
  fields.replaceChildren();
  for (const vm of project?.viewModels ?? []) for (const f of vm.fields) fields.append(el('option', { value: f.name, label: `${vm.name} (${f.kind})` }));
  const actions = $('actions');
  actions.replaceChildren();
  for (const a of project?.actions ?? []) actions.append(el('option', { value: a }));
}

// ---------------------------------------------------------------- Layer values

type FieldKind = 'text' | 'number' | 'check' | 'select';
interface FieldDef {
  key: keyof MealkitData;
  label: string;
  kind: FieldKind;
  list?: string;
  options?: string[];
  show: (l: LayerInfo, d: MealkitData) => boolean;
  hint?: string;
}

const always = () => true;
const isText = (l: LayerInfo) => l.type === 'text';
const widgetIs = (...w: string[]) => (_: LayerInfo, d: MealkitData) => w.includes(d.widget ?? '');

const defs: FieldDef[] = [
  { key: 'navigation', label: 'Keyboard and gamepad navigation (menus)', kind: 'check', show: (l) => l.topBoard },
  { key: 'firstFocus', label: 'First focused layer', kind: 'text', show: (l) => l.topBoard },
  { key: 'back', label: 'Back action (Esc, gamepad B)', kind: 'text', list: 'actions', show: (l) => l.topBoard },
  { key: 'widget', label: 'Widget', kind: 'select', options: ['', 'button', 'toggle', 'slider', 'select', 'textInput', 'scroll', 'list'], show: (l) => !l.topBoard },
  { key: 'action', label: 'Action', kind: 'text', list: 'actions', show: widgetIs('button', 'toggle', 'slider', 'select', 'textInput') },
  { key: 'field', label: 'View model field', kind: 'text', list: 'fields', show: widgetIs('toggle', 'slider', 'select', 'textInput', 'list') },
  { key: 'min', label: 'Min', kind: 'number', show: widgetIs('slider') },
  { key: 'max', label: 'Max', kind: 'number', show: widgetIs('slider') },
  { key: 'step', label: 'Step (0 = smooth)', kind: 'number', show: widgetIs('slider') },
  { key: 'options', label: 'Options (string keys, comma separated)', kind: 'text', show: widgetIs('select') },
  { key: 'placeholder', label: 'Placeholder (string key)', kind: 'text', show: widgetIs('textInput') },
  { key: 'maxLength', label: 'Max characters', kind: 'number', show: widgetIs('textInput') },
  { key: 'scrollHorizontal', label: 'Scroll sideways', kind: 'check', show: widgetIs('scroll') },
  { key: 'key', label: 'String key (empty: the layer name if it looks like menu.start)', kind: 'text', show: isText },
  { key: 'overflow', label: 'Overflow', kind: 'select', options: ['', 'ellipsis', 'clip'], show: isText },
  { key: 'maxLines', label: 'Max lines (0 = any)', kind: 'number', show: isText },
  { key: 'wrap', label: 'Line breaks', kind: 'select', options: ['', 'word', 'syllable', 'none'], show: isText },
  { key: 'visible', label: 'Visible when (field, !field = when not)', kind: 'text', list: 'fields', show: (l) => !l.topBoard },
  { key: 'disabled', label: 'Disabled when', kind: 'text', list: 'fields', show: (l) => !l.topBoard },
  { key: 'widthRatio', label: 'Width ratio (0..1 field, health bars)', kind: 'text', list: 'fields', show: (l) => !l.topBoard && !isText(l) },
  { key: 'colorField', label: 'Color from field', kind: 'text', list: 'fields', show: (l) => !l.topBoard },
  { key: 'focusUp', label: 'Focus up', kind: 'text', show: (_, d) => !!d.widget },
  { key: 'focusDown', label: 'Focus down', kind: 'text', show: (_, d) => !!d.widget },
  { key: 'focusLeft', label: 'Focus left', kind: 'text', show: (_, d) => !!d.widget },
  { key: 'focusRight', label: 'Focus right', kind: 'text', show: (_, d) => !!d.widget },
  { key: 'transition', label: 'State change time (s)', kind: 'number', show: (_, d) => !!d.widget },
  { key: 'bake', label: 'Always bake to PNG', kind: 'check', show: (l) => !l.topBoard && !isText(l) },
];

let saveTimer = 0;
function save(layer: LayerInfo) {
  clearTimeout(saveTimer);
  saveTimer = setTimeout(() => post({ type: 'setData', id: layer.id, data: layer.data }), 250) as unknown as number;
}

function renderLayer() {
  const box = $('layer');
  box.replaceChildren();
  if (layers.length !== 1) {
    box.append(el('p', { class: 'dim' }, layers.length === 0 ? 'Select a layer to set its bindings and widget.' : 'Select one layer.'));
    return;
  }
  const l = layers[0];
  box.append(el('div', { class: 'title' }, `${l.name}`, el('span', { class: 'dim' }, ` ${l.type}`)));
  if (l.topBoard) {
    const input = el('input', { type: 'text', value: l.file });
    input.addEventListener('change', () => post({ type: 'setFile', id: l.id, file: input.value.trim() }));
    box.append(el('label', {}, 'File (assets path)', input));
  }
  const d = l.data;
  for (const f of defs) {
    if (!f.show(l, d)) continue;
    const v = d[f.key];
    let input: HTMLInputElement | HTMLSelectElement;
    if (f.kind === 'select') {
      input = el('select');
      for (const o of f.options ?? []) input.append(el('option', { value: o }, o || '(none)'));
      input.value = String(v ?? '');
    } else {
      input = el('input', { type: f.kind === 'check' ? 'checkbox' : f.kind === 'number' ? 'number' : 'text' });
      if (f.list) input.setAttribute('list', f.list);
      if (f.kind === 'check') (input as HTMLInputElement).checked = !!v;
      else input.value = v === undefined ? '' : String(v);
    }
    input.addEventListener('change', () => {
      const rec = d as Record<string, unknown>;
      if (f.kind === 'check') rec[f.key] = (input as HTMLInputElement).checked;
      else if (f.kind === 'number') rec[f.key] = input.value === '' ? undefined : Number(input.value);
      else rec[f.key] = input.value.trim() || undefined;
      save(l);
      if (f.key === 'widget') renderLayer();
    });
    box.append(f.kind === 'check' ? el('label', { class: 'check' }, input, f.label) : el('label', {}, f.label, input));
  }
  if (!l.topBoard && !isText(l)) {
    const s = d.slice ?? [0, 0, 0, 0];
    const row = el('div', { class: 'row' });
    ['left', 'top', 'right', 'bottom'].forEach((name, i) => {
      const input = el('input', { type: 'number', value: String(s[i]), title: name });
      input.addEventListener('change', () => {
        const next = [...(d.slice ?? [0, 0, 0, 0])] as [number, number, number, number];
        next[i] = Number(input.value) || 0;
        d.slice = next.some((x) => x !== 0) ? next : undefined;
        save(l);
      });
      row.append(input);
    });
    box.append(el('label', {}, '9-slice margins when baked (left top right bottom)', row));
  }
}

// ---------------------------------------------------------------- Export

function renderBoards() {
  const list = $('boards');
  list.replaceChildren();
  for (const b of boards) {
    const box = el('input', { type: 'checkbox' });
    box.checked = checked.has(b.id);
    box.addEventListener('change', () => (box.checked ? checked.add(b.id) : checked.delete(b.id)));
    const name = el('a', { href: '#' }, b.name);
    name.addEventListener('click', (e) => {
      e.preventDefault();
      post({ type: 'select', id: b.id });
    });
    list.append(el('label', { class: 'check' }, box, name, el('span', { class: 'dim' }, ' → assets/' + b.file)));
  }
  if (boards.length === 0) list.append(el('p', { class: 'dim' }, 'No boards on this page. Each top-level board is one screen.'));
}

function log(line: string, cls = '') {
  $('log').append(el('div', { class: cls }, line));
}

async function finishExport(msg: Extract<ToUi, { type: 'exported' }>) {
  const force = ($('force') as HTMLInputElement).checked;
  const entries: ImportEntry[] = [];
  const strings = new Map<string, string>();
  let blocked = false;
  for (const s of msg.screens) {
    log(`${s.board} → assets/${s.file}: ${s.nodeCount} nodes, ${s.images.length} image(s)`);
    for (const w of s.warnings) log('  ' + w, 'warn');
    for (const e of s.errors) log('  ' + e, 'err');
    if (s.errors.length > 0) {
      blocked = true;
      continue;
    }
    entries.push({ kind: 'ui', path: s.file, data: text(s.ui), force });
    const screen = s.file.replace(/^ui\//, '').replace(/\.ui$/, '');
    entries.push({ kind: 'clean', path: `ui/${screen}/`, data: new Uint8Array() });
    for (const img of s.images) entries.push({ kind: 'png', path: img.path, data: img.data });
    for (const kv of s.strings) strings.set(kv.key, kv.text);
    const boxes = [`# Penpot layout boxes of assets/${s.file} (logical units from the board's top left): id x y w h name [tab text]`];
    for (const b of s.boxes) boxes.push(`${b.id} ${b.x} ${b.y} ${b.w} ${b.h} ${b.name}${b.text !== undefined ? '\t' + b.text : ''}`);
    entries.push({ kind: 'boxes', path: screen + '.boxes', data: text(boxes.join('\n') + '\n') });
    if (s.preview) entries.push({ kind: 'preview', path: screen + '.png', data: s.preview });
  }
  if (blocked) {
    log('Fix the errors above and export again (nothing was written for those screens).', 'err');
  }
  if (strings.size > 0) entries.push({ kind: 'strings', path: '-', data: text([...strings].map(([k, v]) => `${k} = ${v}`).join('\n') + '\n') });
  if (msg.theme) {
    entries.push({ kind: 'theme', path: 'ui/theme.ui', data: text(msg.theme.ui) });
    for (const w of msg.theme.warnings) log('  ' + w, 'warn');
  }
  if (msg.source) entries.push({ kind: 'penpot', path: msg.source.name, data: msg.source.data });
  if (entries.length === 0) return;

  if (project) {
    try {
      const r = await fetch('api/export', { method: 'POST', body: buildImport(entries) as BodyInit, headers: { 'Content-Type': 'application/octet-stream' } });
      const res = (await r.json()) as { ok: boolean; written: string[]; errors: string[]; warnings: string[] };
      for (const w of res.written ?? []) log('wrote ' + w, 'ok');
      for (const w of res.warnings ?? []) log(w, 'warn');
      for (const e of res.errors ?? []) log(e, 'err');
      log(res.ok ? 'Done. MkUI and running games reload the screens.' : 'Some files were not written.', res.ok ? 'ok' : 'err');
    } catch (e) {
      log('Could not reach the MkUI Designer: ' + String(e), 'err');
    }
    return;
  }
  // zip: paths relative to the project folder. Strings separately, to be added to the table directly
  const files: { path: string; data: Uint8Array }[] = [];
  for (const e of entries) {
    if (e.kind === 'ui' || e.kind === 'png' || e.kind === 'theme') files.push({ path: 'assets/' + e.path, data: e.data });
    else if (e.kind === 'penpot') files.push({ path: 'art/ui/' + e.path, data: e.data });
    else if (e.kind === 'boxes') files.push({ path: 'art/ui/boxes/' + e.path, data: e.data });
    else if (e.kind === 'preview') files.push({ path: 'art/ui/preview/' + e.path, data: e.data });
    else if (e.kind === 'strings') files.push({ path: 'penpot-strings.txt', data: e.data });
  }
  const blob = new Blob([buildZip(files) as BlobPart], { type: 'application/zip' });
  const a = el('a', { href: URL.createObjectURL(blob), download: `${msg.fileName || 'penpot'}-ui.zip` });
  document.body.append(a);
  a.click();
  a.remove();
  log('Downloaded the zip. Unpack it in the project folder; add the keys in penpot-strings.txt to assets/strings.', 'ok');
}

// ---------------------------------------------------------------- Startup

window.addEventListener('message', (ev: MessageEvent<ToUi>) => {
  const msg = ev.data;
  if (!msg || typeof msg !== 'object') return;
  switch (msg.type) {
    case 'init':
      document.body.dataset.theme = msg.theme;
      $('file').textContent = msg.fileName;
      break;
    case 'selection':
      layers = msg.layers;
      renderLayer();
      break;
    case 'boards':
      boards = msg.boards;
      for (const id of [...checked]) if (!boards.some((b) => b.id === id)) checked.delete(id);
      renderBoards();
      break;
    case 'progress':
      $('progress').textContent = msg.text;
      break;
    case 'exported':
      $('progress').textContent = '';
      finishExport(msg).finally(() => (($('export') as HTMLButtonElement).disabled = false));
      break;
    case 'error':
      $('progress').textContent = '';
      log(msg.message, 'err');
      ($('export') as HTMLButtonElement).disabled = false;
      break;
  }
});

$('export').addEventListener('click', () => {
  if (checked.size === 0) {
    log('Tick the boards to export.', 'warn');
    return;
  }
  $('log').replaceChildren();
  ($('export') as HTMLButtonElement).disabled = true;
  post({
    type: 'export',
    boardIds: [...checked],
    fonts: project?.fonts ?? [],
    nodeBudget: 500,
    theme: ($('theme') as HTMLInputElement).checked,
    source: ($('source') as HTMLInputElement).checked,
  });
});

$('example').addEventListener('click', () => post({ type: 'example' }));

document.body.dataset.theme = new URLSearchParams(location.search).get('theme') ?? 'light';
post({ type: 'ready' });
connect();
