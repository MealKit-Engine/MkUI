// Plugin code running inside Penpot: opens the panel (ui.html), reads/writes engine values (plugin data) of the selected layer,
// turns screen boards into snapshot → .ui, and exports shapes to bake as PNG. File writing is done by the panel (MkUI Designer server or zip).

import type { Board, Shape } from '@penpot/plugin-types';
import { convertBoard, convertTheme, nodeName, type TokenEntry } from './convert.ts';
import type { ExportedScreen, LayerInfo, ToPlugin, ToUi } from './messages.ts';
import type { MealkitData } from './model.ts';
import { kDataKey, readData, snapshot } from './snapshot.ts';
import { pngSize } from './encode.ts';
import { createExample } from './example.ts';

const kFileKey = 'mealkit.file'; // .ui path of a screen board
const kPreviewScale = 0.5; // board image for pixel comparison (the engine's --ui-render also draws at half the board size)

// Replaced with its own address when the MkUI Designer serves plugin.js (a relative path when served elsewhere, like zip mode)
const base = '__MEALKIT_BASE__';
const uiUrl = base.startsWith('__') ? 'ui.html' : base + 'ui.html';

penpot.ui.open('MealKit', `${uiUrl}?theme=${penpot.theme}`, { width: 380, height: 680 });

function send(msg: ToUi) {
  penpot.ui.sendMessage(msg);
}

function isTopBoard(s: Shape): boolean {
  return s.type === 'board' && (!s.parent || s.parent.type !== 'board' || s.parent.id === '00000000-0000-0000-0000-000000000000');
}

function topBoards(): Board[] {
  const root = penpot.root;
  if (!root || !('children' in root)) return [];
  return (root.children as Shape[]).filter((s) => s.type === 'board') as Board[];
}

function fileOf(board: Shape): string {
  const saved = board.getPluginData(kFileKey);
  if (saved) return saved;
  return 'ui/' + nodeName(board.name, 'screen').toLowerCase() + '.ui';
}

function sendSelection() {
  const layers: LayerInfo[] = penpot.selection.map((s) => ({
    id: s.id,
    name: s.name,
    type: s.type,
    topBoard: isTopBoard(s),
    data: readData(s) ?? {},
    file: isTopBoard(s) ? fileOf(s) : '',
  }));
  send({ type: 'selection', layers });
}

function sendBoards() {
  send({ type: 'boards', boards: topBoards().map((b) => ({ id: b.id, name: b.name, file: fileOf(b) })) });
}

function findShape(id: string): Shape | null {
  return penpot.currentPage?.getShapeById(id) ?? null;
}

function activeTokens(): TokenEntry[] {
  const out: TokenEntry[] = [];
  const catalog = penpot.library.local.tokens;
  if (!catalog) return out;
  for (const set of catalog.sets) {
    if (!set.active) continue;
    for (const t of set.tokens) {
      const type = (t as unknown as { type?: string }).type ?? '';
      const value = t.resolvedValueString;
      if (value !== undefined) out.push({ name: t.name, type, value });
    }
  }
  return out;
}

async function exportBoards(msg: Extract<ToPlugin, { type: 'export' }>) {
  const file = penpot.currentFile;
  const fileName = file?.name ?? 'Penpot';
  const screens: ExportedScreen[] = [];
  for (const id of msg.boardIds) {
    const board = findShape(id);
    if (!board || board.type !== 'board') continue;
    send({ type: 'progress', text: `Reading ${board.name}...` });
    const snap = snapshot(board, board);
    const path = fileOf(board);
    const r = convertBoard(snap, { file: path, fileName, fileId: file?.id ?? '', fonts: msg.fonts, nodeBudget: msg.nodeBudget });
    const images: ExportedScreen['images'] = [];
    for (const b of r.bakes) {
      const shape = findShape(b.id);
      if (!shape) continue;
      send({ type: 'progress', text: `Baking ${b.path}...` });
      try {
        const data = await shape.export({ type: 'png', scale: b.scale, skipChildren: b.skipChildren });
        const size = pngSize(data);
        const logicalW = size ? size.w / b.scale : shape.width, logicalH = size ? size.h / b.scale : shape.height;
        // With shadows/blur the PNG is larger than the shape: the engine stretches it into the shape box
        if (Math.abs(logicalW - shape.width) > 1 || Math.abs(logicalH - shape.height) > 1)
          r.warnings.push(`${shape.name}: the baked image (${logicalW}x${logicalH}) is larger than the layer (${shape.width}x${shape.height}), effects outside the layer are squeezed in`);
        images.push({ path: b.path, data, logicalW, logicalH });
      } catch (e) {
        r.errors.push(`${shape.name}: could not export the PNG (${String(e)})`);
      }
    }
    let preview: Uint8Array | undefined;
    try {
      preview = await board.export({ type: 'png', scale: kPreviewScale });
    } catch (e) {
      r.warnings.push(`could not export the preview image (${String(e)})`);
    }
    screens.push({ board: board.name, file: path, ui: r.ui, images, strings: r.strings, boxes: r.boxes, preview, warnings: r.warnings, errors: r.errors, nodeCount: r.nodeCount });
  }
  let theme: { ui: string; warnings: string[] } | undefined;
  if (msg.theme) {
    const tokens = activeTokens();
    if (tokens.length > 0) theme = convertTheme(tokens, fileName);
  }
  let source: { name: string; data: Uint8Array } | undefined;
  let saveWarning = '';
  if (msg.source && file) {
    send({ type: 'progress', text: 'Saving the Penpot file...' });
    try {
      source = { name: nodeName(fileName, 'ui') + '.penpot', data: await file.export('penpot', 'all') };
    } catch (e) {
      // Some versions fail Penpot's plugin file export (2.18: "No matching clause"): tell the user to save manually
      send({ type: 'progress', text: '' });
      saveWarning = `Could not save the Penpot file from the plugin (${String(e)}). Save it by hand: main menu > File > Export as .penpot, into art/ui/ of the project.`;
    }
  }
  if (saveWarning && screens.length > 0) screens[0].warnings.unshift(saveWarning);
  send({ type: 'exported', screens, theme, source, fileName });
}

penpot.ui.onMessage<ToPlugin>((msg) => {
  switch (msg.type) {
    case 'ready':
      send({ type: 'init', fileName: penpot.currentFile?.name ?? '', fileId: penpot.currentFile?.id ?? '', theme: penpot.theme });
      sendSelection();
      sendBoards();
      break;
    case 'setData': {
      const s = findShape(msg.id);
      if (!s) break;
      const clean: MealkitData = {};
      for (const [k, v] of Object.entries(msg.data)) if (v !== '' && v !== undefined && v !== null && v !== false) (clean as Record<string, unknown>)[k] = v;
      s.setPluginData(kDataKey, Object.keys(clean).length > 0 ? JSON.stringify(clean) : '');
      break;
    }
    case 'setFile': {
      const s = findShape(msg.id);
      if (s) s.setPluginData(kFileKey, msg.file);
      sendBoards();
      break;
    }
    case 'select': {
      const s = findShape(msg.id);
      if (s) penpot.selection = [s];
      break;
    }
    case 'export':
      exportBoards(msg).catch((e) => send({ type: 'error', message: String(e) }));
      break;
    case 'example':
      try {
        const b = createExample();
        penpot.selection = [b];
        sendBoards();
      } catch (e) {
        send({ type: 'error', message: `Could not create the example: ${String(e)}` });
      }
      break;
  }
});

penpot.on('selectionchange', () => sendSelection());
penpot.on('pagechange', () => sendBoards());
penpot.on('themechange', (theme) => send({ type: 'init', fileName: penpot.currentFile?.name ?? '', fileId: penpot.currentFile?.id ?? '', theme }));
