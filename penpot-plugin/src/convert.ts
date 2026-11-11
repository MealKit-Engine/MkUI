// Penpot snapshot (model.ts) → MealKit .ui (engine/src/mealkit/ui/Document.h format).
// What the engine can draw as is becomes property lines, what it can't (paths, booleans, image fills, inner shadows, blends ...) is baked to PNG,
// and unsupported things (blur) warn. Supported range: ROADMAP "Game screen UI part 2", user manual manual/ui.md "Penpot".

import type { Bake, Box, MealkitData, Options, Result, SColor, SFill, SNode, STextRun, SShadow } from './model.ts';

export const kHeader = '# MealKit UI exported from Penpot';
// Format line (engine mealkit/core/TextFormat.h kUi, contract between tools): first line after the header comment
export const kFormatLine = '@format ui 1';

const weightNames: Record<string, string> = {
  '100': 'Thin',
  '200': 'ExtraLight',
  '300': 'Light',
  '400': 'Regular',
  '500': 'Medium',
  '600': 'SemiBold',
  '700': 'Bold',
  '800': 'ExtraBold',
  '900': 'Black',
};

// Numbers: up to two decimal places, without trailing zeros
export function num(v: number): string {
  const r = Math.round(v * 100) / 100;
  return Object.is(r, -0) ? '0' : String(r);
}

function hex2(v: number): string {
  return Math.max(0, Math.min(255, Math.round(v))).toString(16).padStart(2, '0').toUpperCase();
}

// #RRGGBB[AA] or $token
export function color(c: SColor | undefined, opacity = 1): string {
  if (!c) return 'none';
  if (c.token) return '$' + c.token;
  const base = c.hex.replace('#', '').toUpperCase().padEnd(6, '0').slice(0, 6);
  const a = c.opacity * opacity;
  return '#' + base + (a >= 0.999 ? '' : hex2(a * 255));
}

// Node names: text without spaces (widget lookup/focus/set use names)
export function nodeName(name: string, fallback: string): string {
  const s = name.trim().replace(/\s+/g, '_').replace(/[^A-Za-z0-9_.\-]/g, '');
  return s.length > 0 ? s : fallback;
}

// String table key shape (loc::validStringKey + at least one dot: "pause.title")
export function looksLikeKey(name: string): boolean {
  return /^[A-Za-z][A-Za-z0-9_.\-]*$/.test(name) && name.includes('.');
}

export function fontFile(run: STextRun): string {
  const family = run.fontFamily.replace(/\s+/g, '');
  let style = weightNames[run.fontWeight] ?? 'Regular';
  if (run.fontStyle === 'italic') style = style === 'Regular' ? 'Italic' : style + 'Italic';
  return family + '-' + style;
}

const visibleFills = (n: SNode): SFill[] => n.fills;

// Why it's baked to PNG because the engine can't draw it (empty string means drawn as is). Only looks at its own shape (children separately)
function bakeReason(n: SNode): string {
  if (n.data?.bake) return 'marked to bake';
  if (n.type === 'path' || n.type === 'boolean' || n.type === 'svg-raw' || n.type === 'image') return n.type;
  if (n.mask) return 'mask';
  if (n.type === 'ellipse' && Math.abs(n.width - n.height) > 0.01) return 'ellipse that is not a circle';
  if (n.blendMode && n.blendMode !== 'normal') return 'blend mode ' + n.blendMode;
  const fills = visibleFills(n);
  if (fills.length > 1) return 'more than one fill';
  if (fills.some((f) => f.image)) return 'image fill';
  if (fills.some((f) => f.gradient && f.gradient.stops.length > 2)) return 'gradient with more than two stops';
  if (n.strokes.length > 1) return 'more than one stroke';
  if (n.strokes.some((s) => s.gradient || (s.style !== 'solid' && s.style !== 'none'))) return 'dashed or gradient stroke';
  if (n.shadows.some((s) => s.style === 'inner-shadow')) return 'inner shadow';
  return '';
}

// Drawing lines that can change by state (property name → lines). State variants write the differences as on <state> lines
function visualLines(n: SNode, warn: (msg: string) => void): Map<string, string[]> {
  const out = new Map<string, string[]>();
  const fill = visibleFills(n)[0];
  if (n.type !== 'text') {
    if (fill?.color) out.set('fill', ['fill ' + color(fill.color)]);
    else if (fill?.gradient) {
      const g = fill.gradient;
      const a = g.stops[0], b = g.stops[g.stops.length - 1];
      const c0 = color({ hex: a.hex, opacity: a.opacity }), c1 = color({ hex: b.hex, opacity: b.opacity });
      if (g.type === 'linear') out.set('fill', [`fill linear ${num(g.startX)} ${num(g.startY)} ${num(g.endX)} ${num(g.endY)} ${c0} ${c1}`]);
      else {
        // Radial: center = start point, horizontal radius = start → end distance (box 0..1), vertical = horizontal × width
        const rx = Math.hypot(g.endX - g.startX, g.endY - g.startY);
        out.set('fill', [`fill radial ${num(g.startX)} ${num(g.startY)} ${num(rx)} ${num(rx * (g.width || 1))} ${c0} ${c1}`]);
      }
    } else out.set('fill', ['fill none']);
    const stroke = n.strokes[0];
    if (stroke && stroke.color && stroke.width > 0) {
      if (stroke.alignment !== 'inner') warn(`stroke alignment '${stroke.alignment}' is drawn inside`);
      out.set('stroke', [`stroke ${num(stroke.width)} ${color(stroke.color)}`]);
    } else out.set('stroke', ['stroke 0 none']);
    const r = n.type === 'ellipse' ? [n.width / 2, n.width / 2, n.width / 2, n.width / 2] : n.radius;
    if (n.radiusToken) out.set('radius', ['radius $' + n.radiusToken]);
    else if (r[0] === r[1] && r[1] === r[2] && r[2] === r[3]) out.set('radius', ['radius ' + num(r[0])]);
    else out.set('radius', [`radius ${num(r[0])} ${num(r[1])} ${num(r[2])} ${num(r[3])}`]);
  }
  const shadows = n.shadows.filter((s: SShadow) => s.style === 'drop-shadow');
  out.set('shadow', shadows.length === 0 ? ['shadow none'] : ['shadow none', ...shadows.map((s) => `shadow ${num(s.offsetX)} ${num(s.offsetY)} ${num(s.blur)} ${num(s.spread)} ${color(s.color)}`)]);
  out.set('opacity', ['opacity ' + num(n.opacity)]);
  if (n.type === 'text' && n.text) {
    const base = baseRun(n.text.runs);
    if (base?.fill) out.set('color', ['color ' + color(base.fill)]);
  }
  return out;
}

// Default style: the span used by the most characters
function baseRun(runs: STextRun[]): STextRun | undefined {
  let best: STextRun | undefined;
  for (const r of runs) if (!best || r.end - r.start > best.end - best.start) best = r;
  return best;
}

function runStyleKey(r: STextRun): string {
  return JSON.stringify([fontFile(r), r.fontSize, r.letterSpacing, r.textDecoration, r.fill ? color(r.fill) : '']);
}

// Text + style spans → numbered tags (<1>…</1>) and span lines
function textContent(runs: STextRun[], base: STextRun, fonts: Set<string>, errors: string[], nodeLabel: string): { text: string; spans: string[] } {
  const tags = new Map<string, number>();
  const spans: string[] = [];
  let text = '';
  const baseKey = runStyleKey(base);
  for (const r of runs) {
    const font = fontFile(r);
    if (!fonts.has(font)) errors.push(`${nodeLabel}: font '${font}' is not in assets/fonts`);
    const chars = r.characters.replace(/\r?\n/g, '\\n');
    const key = runStyleKey(r);
    if (key === baseKey || chars.trim().length === 0) {
      text += chars;
      continue;
    }
    let tag = tags.get(key);
    if (tag === undefined) {
      tag = tags.size + 1;
      tags.set(key, tag);
      const parts: string[] = [];
      if (font !== fontFile(base)) parts.push('font ' + font);
      if (r.fontSize !== base.fontSize) parts.push('size ' + num(r.fontSize));
      if (r.fill && color(r.fill) !== (base.fill ? color(base.fill) : '')) parts.push('color ' + color(r.fill));
      if (r.letterSpacing !== base.letterSpacing) parts.push('letterSpacing ' + num(r.letterSpacing));
      if (r.textDecoration === 'underline' && base.textDecoration !== 'underline') parts.push('underline');
      if (r.textDecoration === 'line-through' && base.textDecoration !== 'line-through') parts.push('strike');
      if (parts.length > 0) spans.push(`span ${tag} ${parts.join(' ')}`);
    }
    text += `<${tag}>${chars}</${tag}>`;
  }
  return { text, spans };
}

// Child order in .ui is both layout order and draw order (written first is below). Snapshot children are from Penpot's bottom layer up,
// but Penpot's Flex/Grid lay out from the top layer (top of the layer list): with a layout they're written reversed (only the front/back of overlapping children changes)
function flowOrder(n: SNode): SNode[] {
  return n.layout ? [...n.children].reverse() : n.children;
}

interface Ctx {
  opt: Options;
  fonts: Set<string>;
  lines: string[];
  bakes: Bake[];
  strings: Map<string, string>;
  boxes: Box[];
  warnings: string[];
  errors: string[];
  count: number;
  screen: string; // PNG folder name for baking
  usedImages: Set<string>;
}

function indentOf(depth: number): string {
  return '  '.repeat(depth);
}

function imagePath(ctx: Ctx, name: string): string {
  let base = `ui/${ctx.screen}/${name}`;
  let path = base + '.png';
  for (let i = 2; ctx.usedImages.has(path); ++i) path = `${base}_${i}.png`;
  ctx.usedImages.add(path);
  return path;
}

function sizeWord(n: SNode, horizontal: boolean, parentLayout: boolean): string {
  const v = horizontal ? n.width : n.height;
  if (n.type === 'text' && n.text) {
    // Text size is decided by Penpot's grow type (layoutChild comes as fix even inside layouts). Only fill is relative to the parent
    const s = parentLayout && n.child && !n.child.absolute ? (horizontal ? n.child.horizontalSizing : n.child.verticalSizing) : 'fix';
    if (s === 'fill') return 'fill';
    if (n.text.growType === 'auto-width') return 'hug';
    if (n.text.growType === 'auto-height' && !horizontal) return 'hug';
    return num(v);
  }
  if (parentLayout && n.child && !n.child.absolute) {
    const s = horizontal ? n.child.horizontalSizing : n.child.verticalSizing;
    if (s === 'fill') return 'fill';
    if (s === 'auto') return 'hug';
    return num(v);
  }
  if (n.layout && (horizontal ? n.boardSizingH : n.boardSizingV) === 'auto') return 'hug';
  return num(v);
}

function emitPlacement(ctx: Ctx, n: SNode, out: string[], parentLayout: 'none' | 'flex' | 'grid', rotated: boolean): void {
  const inFlow = parentLayout !== 'none' && n.child !== undefined && !n.child.absolute;
  if (parentLayout !== 'none' && n.child?.absolute) out.push('absolute');
  if (!inFlow) out.push(`pos ${num(n.x)} ${num(n.y)}`);
  out.push(`size ${sizeWord(n, true, parentLayout !== 'none')} ${sizeWord(n, false, parentLayout !== 'none')}`);
  if (!inFlow && (n.constraintsH !== 'left' || n.constraintsV !== 'top')) out.push(`constraints ${n.constraintsH} ${n.constraintsV}`);
  if (inFlow && n.child) {
    const c = n.child;
    const m = c.margin;
    if (m.some((v) => v !== 0)) out.push(`margin ${m.map(num).join(' ')}`);
    if (c.alignSelf && c.alignSelf !== 'auto') out.push('alignSelf ' + c.alignSelf);
    if (parentLayout === 'grid' && c.justifySelf && c.justifySelf !== 'auto') out.push('justifySelf ' + c.justifySelf);
    if (c.minWidth !== null || c.minHeight !== null) out.push(`min ${c.minWidth !== null ? num(c.minWidth) : '-'} ${c.minHeight !== null ? num(c.minHeight) : '-'}`);
    if (c.maxWidth !== null || c.maxHeight !== null) out.push(`max ${c.maxWidth !== null ? num(c.maxWidth) : '-'} ${c.maxHeight !== null ? num(c.maxHeight) : '-'}`);
  }
  if (parentLayout === 'grid' && n.cell && inFlow) {
    const c = n.cell;
    out.push(c.rowSpan > 1 || c.columnSpan > 1 ? `cell ${c.row} ${c.column} ${c.rowSpan} ${c.columnSpan}` : `cell ${c.row} ${c.column}`);
  }
  if (n.rotation !== 0) {
    out.push('rotation ' + num(n.rotation));
    if (!rotated) ctx.warnings.push(`${n.name}: rotated, the layout check skips it and its children`);
  }
  if (n.hidden) out.push('hidden');
}

function emitLayout(n: SNode, out: string[]): void {
  const l = n.layout;
  if (!l) return;
  if (l.kind === 'grid') {
    out.push(l.dir === 'column' ? 'layout grid column' : 'layout grid');
    const track = (t: { type: string; value: number }) =>
      t.type === 'fixed' ? num(t.value) : t.type === 'flex' ? num(t.value) + 'fr' : t.type === 'percent' ? num(t.value) + '%' : 'auto';
    if (l.columns.length > 0) out.push('columns ' + l.columns.map(track).join(' '));
    if (l.rows.length > 0) out.push('rows ' + l.rows.map(track).join(' '));
    out.push('justifyItems ' + l.justifyItems);
  } else {
    let word = l.dir.startsWith('column') ? 'layout column' : 'layout row';
    if (l.dir.endsWith('-reverse')) word += ' reverse';
    if (l.wrap) word += ' wrap';
    out.push(word);
  }
  if (l.rowGap !== 0 || l.columnGap !== 0) out.push(l.rowGap === l.columnGap ? 'gap ' + num(l.rowGap) : `gap ${num(l.rowGap)} ${num(l.columnGap)}`);
  if (l.padding.some((v) => v !== 0)) out.push('padding ' + l.padding.map(num).join(' '));
  if (l.justifyContent !== 'start') out.push('justify ' + (l.kind === 'flex' && l.justifyContent === 'stretch' ? 'start' : l.justifyContent));
  if (l.alignItems !== 'start') out.push('align ' + l.alignItems);
  if (l.alignContent !== 'start') out.push('alignContent ' + l.alignContent);
}

function emitData(d: MealkitData | undefined, out: string[]): void {
  if (!d) return;
  if (d.visible) out.push('visible ' + d.visible);
  if (d.disabled) out.push('disabled ' + d.disabled);
  if (d.widthRatio) out.push('widthRatio ' + d.widthRatio);
  if (d.colorField) out.push('colorField ' + d.colorField);
  switch (d.widget) {
    case 'button':
      if (d.action) out.push('button ' + d.action);
      break;
    case 'toggle':
      if (d.field) out.push(`toggle ${d.field}${d.action ? ' ' + d.action : ''}`);
      break;
    case 'slider':
      if (d.field) out.push(`slider ${d.field} ${num(d.min ?? 0)} ${num(d.max ?? 1)} ${num(d.step ?? 0)}${d.action ? ' ' + d.action : ''}`);
      break;
    case 'select':
      if (d.field && d.options) out.push(`select ${d.field} ${d.options.replace(/\s+/g, '')}${d.action ? ' ' + d.action : ''}`);
      break;
    case 'textInput':
      if (d.field) out.push(`textInput ${d.field}${d.action ? ' ' + d.action : ''}`);
      if (d.placeholder) out.push('placeholder ' + d.placeholder);
      if (d.maxLength) out.push('maxLength ' + d.maxLength);
      break;
    case 'scroll':
      out.push(d.scrollHorizontal ? 'scroll horizontal' : 'scroll');
      break;
    case 'list':
      if (d.field) out.push('list ' + d.field);
      break;
  }
  if (d.focusUp) out.push('focusUp ' + d.focusUp);
  if (d.focusDown) out.push('focusDown ' + d.focusDown);
  if (d.focusLeft) out.push('focusLeft ' + d.focusLeft);
  if (d.focusRight) out.push('focusRight ' + d.focusRight);
  if (d.transition !== undefined) out.push('transition ' + num(d.transition));
}

function emitStates(ctx: Ctx, n: SNode, base: Map<string, string[]>, out: string[]): void {
  for (const s of n.states ?? []) {
    const state = s.state.toLowerCase();
    if (!['hover', 'pressed', 'focus', 'disabled', 'checked'].includes(state)) {
      if (!['normal', 'default'].includes(state)) ctx.warnings.push(`${n.name}: variant state '${s.state}' is not hover, pressed, focus, disabled or checked`);
      continue;
    }
    if (bakeReason(s.node)) ctx.warnings.push(`${n.name}: the ${state} variant cannot be drawn without baking (${bakeReason(s.node)}), its look is ignored`);
    const lines = visualLines(s.node, () => {});
    for (const [prop, v] of lines) {
      const b = base.get(prop);
      if (b && b.join('\n') === v.join('\n')) continue;
      for (const line of v) out.push(`on ${state} ${line}`);
    }
    if (JSON.stringify(s.node.children.map((c) => visualLines(c, () => {}))) !== JSON.stringify(n.children.map((c) => visualLines(c, () => {}))))
      ctx.warnings.push(`${n.name}: the ${state} variant changes child layers, only the top layer's look is used`);
  }
}

function convertNode(ctx: Ctx, n: SNode, depth: number, parentLayout: 'none' | 'flex' | 'grid', rotated: boolean, parentName: string): void {
  const label = parentName ? `${parentName}/${n.name}` : n.name;
  const warn = (msg: string) => ctx.warnings.push(`${label}: ${msg}`);
  if (n.blur) warn('layer blur is not supported (ignored)');
  if (n.backgroundBlur) warn('background blur is not supported (ignored)');
  const name = nodeName(n.name, n.type + (ctx.count + 1));
  const ind = indentOf(depth);
  const props: string[] = [];
  const isRotated = rotated || n.rotation !== 0;
  if (!isRotated) ctx.boxes.push({ id: n.id, name, x: n.absX, y: n.absY, w: n.width, h: n.height });
  ctx.count++;

  // Groups without effects are flattened: children go directly into the group's parent (moved by the group's position)
  if (n.type === 'group' && !n.mask && parentLayout === 'none' && !n.data && n.opacity === 1 && n.rotation === 0 && !n.hidden && n.shadows.length === 0 && !bakeReason(n)) {
    ctx.count--;
    ctx.boxes.pop();
    for (const c of n.children) convertNode(ctx, { ...c, x: c.x + n.x, y: c.y + n.y, constraintsH: c.constraintsH, constraintsV: c.constraintsV }, depth, 'none', rotated, label);
    return;
  }

  const reason = bakeReason(n);
  const bakeWhole = reason !== '' && (n.children.length === 0 || n.type === 'boolean' || n.type === 'path' || n.type === 'svg-raw' || n.mask || n.data?.bake === true);
  if (bakeWhole) {
    const path = imagePath(ctx, name);
    ctx.bakes.push({ id: n.id, path, scale: 2, skipChildren: false });
    ctx.lines.push(`${ind}image ${name}`);
    ctx.lines.push(`${ind}  id ${n.id}`);
    emitPlacement(ctx, n, props, parentLayout, rotated);
    props.push('src ' + path);
    if (n.data?.slice) props.push('slice ' + n.data.slice.map((v) => num(v * 2)).join(' '));
    if (n.opacity !== 1) props.push('opacity ' + num(n.opacity));
    emitData(n.data, props);
    for (const p of props) ctx.lines.push(`${ind}  ${p}`);
    ctx.lines.push(`${ind}end`);
    return;
  }

  if (n.type === 'text' && n.text) {
    const box = ctx.boxes[ctx.boxes.length - 1];
    if (box && box.id === n.id && n.text) box.text = n.text.characters.replace(/\r?\n/g, '\\n');
    ctx.lines.push(`${ind}text ${name}`);
    ctx.lines.push(`${ind}  id ${n.id}`);
    emitPlacement(ctx, n, props, parentLayout, rotated);
    const t = n.text;
    const base = baseRun(t.runs);
    if (!base) {
      warn('empty text');
    } else {
      const { text, spans } = textContent(t.runs, base, ctx.fonts, ctx.errors, label);
      const key = n.data?.key || (looksLikeKey(n.name) ? n.name.trim() : '');
      if (key) {
        props.push('key ' + key);
        ctx.strings.set(key, text);
      } else {
        warn('the layer name is not a string key (like "menu.start"), so the text is not translated');
        props.push('content ' + text);
      }
      props.push('font ' + fontFile(base));
      props.push('fontSize ' + num(base.fontSize));
      if (base.fill) props.push('color ' + color(base.fill));
      if (base.gradientFill || t.runs.some((r) => r.gradientFill)) warn('gradient text is drawn with its first color');
      if (base.letterSpacing !== 0) props.push('letterSpacing ' + num(base.letterSpacing));
      props.push('lineHeight ' + num(base.lineHeight));
      if (base.textDecoration === 'underline') props.push('underline');
      if (base.textDecoration === 'line-through') props.push('strike');
      if (base.textTransform === 'uppercase') props.push('transform upper');
      else if (base.textTransform === 'lowercase') props.push('transform lower');
      else if (base.textTransform === 'capitalize') warn('capitalize is not supported');
      if (t.align === 'center' || t.align === 'right') props.push('textAlign ' + t.align);
      else if (t.align === 'justify') warn('justified text is drawn left-aligned');
      if (t.verticalAlign === 'center' || t.verticalAlign === 'bottom') props.push('verticalAlign ' + t.verticalAlign);
      props.push(...spans);
    }
    const d = n.data;
    if (d?.overflow) props.push('overflow ' + d.overflow);
    if (d?.maxLines) props.push('maxLines ' + d.maxLines);
    if (d?.wrap) props.push('wrap ' + d.wrap);
    const visuals = visualLines(n, warn);
    for (const [prop, v] of visuals) if (prop === 'opacity' ? n.opacity !== 1 : prop === 'shadow' && v.length > 1) props.push(...v.filter((l) => l !== 'shadow none'));
    emitData(d, props);
    for (const p of props) ctx.lines.push(`${ind}  ${p}`);
    ctx.lines.push(`${ind}end`);
    return;
  }

  // frame (board, rectangle, ellipse, group)
  ctx.lines.push(`${ind}frame ${name}`);
  ctx.lines.push(`${ind}  id ${n.id}`);
  emitPlacement(ctx, n, props, parentLayout, rotated);
  if (n.clip && n.type === 'board' && n.children.length > 0) props.push('clip');
  emitLayout(n, props);
  const visuals = visualLines(n, warn);
  if (reason) {
    // Bake only the background (children converted separately): image of the frontmost child
    const path = imagePath(ctx, name + '_bg');
    ctx.bakes.push({ id: n.id, path, scale: 2, skipChildren: true });
    for (const [prop, v] of visuals) if (prop === 'opacity' && n.opacity !== 1) props.push(...v);
  } else {
    for (const [prop, v] of visuals) {
      if (prop === 'fill' && v[0] === 'fill none') continue;
      if (prop === 'stroke' && v[0] === 'stroke 0 none') continue;
      if (prop === 'radius' && v[0] === 'radius 0') continue;
      if (prop === 'shadow' && v.length === 1) continue;
      if (prop === 'opacity' && n.opacity === 1) continue;
      props.push(...v.filter((l) => !(prop === 'shadow' && l === 'shadow none')));
    }
    emitStates(ctx, n, visuals, props);
  }
  emitData(n.data, props);
  for (const p of props) ctx.lines.push(`${ind}  ${p}`);
  if (reason) {
    const bg = ctx.bakes[ctx.bakes.length - 1];
    const bgName = name + '_bg';
    ctx.lines.push(`${ind}  image ${bgName}`);
    ctx.lines.push(`${ind}    absolute`);
    ctx.lines.push(`${ind}    pos 0 0`);
    ctx.lines.push(`${ind}    size ${num(n.width)} ${num(n.height)}`);
    ctx.lines.push(`${ind}    constraints leftright topbottom`);
    ctx.lines.push(`${ind}    src ${bg.path}`);
    if (n.data?.slice) ctx.lines.push(`${ind}    slice ${n.data.slice.map((v) => num(v * 2)).join(' ')}`);
    ctx.lines.push(`${ind}  end`);
    ctx.count++;
    warn(`baked to ${bg.path} (${reason})`);
  }
  const layout: 'none' | 'flex' | 'grid' = n.layout ? n.layout.kind : 'none';
  for (const c of flowOrder(n)) convertNode(ctx, c, depth + 1, layout, isRotated, label);
  ctx.lines.push(`${ind}end`);
}

// One board → .ui
export function convertBoard(board: SNode, opt: Options): Result {
  const screen = opt.file.replace(/^ui\//, '').replace(/\.ui$/, '');
  const ctx: Ctx = {
    opt,
    fonts: new Set(opt.fonts),
    lines: [],
    bakes: [],
    strings: new Map(),
    boxes: [],
    warnings: [],
    errors: [],
    count: 1,
    screen,
    usedImages: new Set(),
  };
  const name = nodeName(board.name, screen);
  ctx.lines.push(`${kHeader}: ${opt.fileName} (file ${opt.fileId}, board ${board.id}). Do not edit; change it in Penpot and export again.`);
  ctx.lines.push(kFormatLine);
  ctx.lines.push(`board ${name} ${num(board.width)} ${num(board.height)}`);
  ctx.lines.push(`  id ${board.id}`);
  ctx.boxes.push({ id: board.id, name, x: 0, y: 0, w: board.width, h: board.height });
  const d = board.data;
  if (d?.navigation) ctx.lines.push('  navigation');
  if (d?.firstFocus) ctx.lines.push('  firstFocus ' + d.firstFocus);
  if (d?.back) ctx.lines.push('  back ' + d.back);
  const props: string[] = [];
  if (board.clip && board.children.length > 0) props.push('clip');
  emitLayout(board, props);
  const reason = bakeReason(board);
  if (reason) {
    ctx.warnings.push(`${board.name}: the board background is baked (${reason})`);
    const path = imagePath(ctx, name + '_bg');
    ctx.bakes.push({ id: board.id, path, scale: 1, skipChildren: true });
    props.push(`image ${name}_bg`, `  absolute`, `  pos 0 0`, `  size ${num(board.width)} ${num(board.height)}`, `  constraints scale scale`, `  src ${path}`, `end`);
    ctx.count++;
  } else {
    for (const [prop, v] of visualLines(board, (m) => ctx.warnings.push(`${board.name}: ${m}`))) {
      if (prop === 'fill' && v[0] !== 'fill none') props.push(...v);
      if (prop === 'opacity' && board.opacity !== 1) props.push(...v);
    }
  }
  emitData({ ...d, navigation: undefined, firstFocus: undefined, back: undefined }, props);
  for (const p of props) ctx.lines.push('  ' + p);
  const layout: 'none' | 'flex' | 'grid' = board.layout ? board.layout.kind : 'none';
  for (const c of flowOrder(board)) convertNode(ctx, c, 1, layout, board.rotation !== 0, board.name);
  ctx.lines.push('end');
  if (ctx.count > opt.nodeBudget) ctx.warnings.push(`${ctx.count} nodes, over the budget of ${opt.nodeBudget} per screen`);
  return {
    ui: ctx.lines.join('\n') + '\n',
    bakes: ctx.bakes,
    strings: [...ctx.strings].map(([key, text]) => ({ key, text })),
    boxes: ctx.boxes,
    warnings: ctx.warnings,
    errors: ctx.errors,
    nodeCount: ctx.count,
  };
}

// Design tokens → assets/ui/theme.ui (colors and numbers)
export interface TokenEntry {
  name: string;
  type: string; // color, dimension, spacing, borderRadius, sizing, number, opacity ...
  value: string; // resolved value
}

export function convertTheme(tokens: TokenEntry[], fileName: string): { ui: string; warnings: string[] } {
  const lines = [`${kHeader}: design tokens of ${fileName}. Do not edit; change the tokens in Penpot and export again.`, kFormatLine];
  const warnings: string[] = [];
  const numberTypes = new Set(['dimension', 'spacing', 'borderRadius', 'sizing', 'number', 'opacity', 'borderWidth', 'fontSizes', 'letterSpacing', 'rotation']);
  for (const t of [...tokens].sort((a, b) => a.name.localeCompare(b.name))) {
    const name = t.name.replace(/\s+/g, '-');
    if (t.type === 'color') {
      const c = parseCssColor(t.value);
      if (c) lines.push(`color ${name} ${c}`);
      else warnings.push(`token ${t.name}: cannot read the color '${t.value}'`);
    } else if (numberTypes.has(t.type)) {
      const v = parseFloat(t.value);
      if (Number.isFinite(v)) lines.push(`number ${name} ${num(v)}`);
      else warnings.push(`token ${t.name}: cannot read the number '${t.value}'`);
    }
  }
  return { ui: lines.join('\n') + '\n', warnings };
}

// #rgb, #rrggbb, #rrggbbaa, rgb(), rgba() → #RRGGBB[AA]
export function parseCssColor(v: string): string | null {
  const s = v.trim();
  let m = /^#([0-9a-f]{3})$/i.exec(s);
  if (m) return '#' + [...m[1]].map((c) => c + c).join('').toUpperCase();
  m = /^#([0-9a-f]{6})([0-9a-f]{2})?$/i.exec(s);
  if (m) return ('#' + m[1] + (m[2] && m[2].toUpperCase() !== 'FF' ? m[2] : '')).toUpperCase();
  m = /^rgba?\(\s*([\d.]+)\s*,\s*([\d.]+)\s*,\s*([\d.]+)\s*(?:,\s*([\d.]+%?)\s*)?\)$/i.exec(s);
  if (m) {
    let a = m[4] === undefined ? 1 : m[4].endsWith('%') ? parseFloat(m[4]) / 100 : parseFloat(m[4]);
    a = Math.max(0, Math.min(1, a));
    return '#' + hex2(+m[1]) + hex2(+m[2]) + hex2(+m[3]) + (a >= 0.999 ? '' : hex2(a * 255));
  }
  return null;
}
