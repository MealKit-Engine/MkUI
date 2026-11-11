// Penpot shapes → snapshot (model.ts). Runs only inside the plugin (Penpot API). Penpot values like 'mixed' are resolved here so
// the converter (convert.ts) only sees simple JSON.

import type { Board, Fill, Shape, Text, TextRange, Shadow as PShadow } from '@penpot/plugin-types';
import type { MealkitData, SColor, SFill, SNode, STextRun } from './model.ts';

export const kDataKey = 'mealkit'; // the shape's plugin data (JSON MealkitData)

export function readData(shape: Shape): MealkitData | undefined {
  const raw = shape.getPluginData(kDataKey);
  if (!raw) return undefined;
  try {
    const d = JSON.parse(raw) as MealkitData;
    return Object.keys(d).length > 0 ? d : undefined;
  } catch {
    return undefined;
  }
}

function fillColor(f: Fill | undefined, token?: string): SColor | undefined {
  if (!f || !f.fillColor) return undefined;
  return { hex: f.fillColor, opacity: f.fillOpacity ?? 1, token };
}

function snapFill(f: Fill, token?: string): SFill {
  if (f.fillImage) return { image: true };
  if (f.fillColorGradient) {
    const g = f.fillColorGradient;
    return {
      gradient: {
        type: g.type,
        startX: g.startX,
        startY: g.startY,
        endX: g.endX,
        endY: g.endY,
        width: g.width,
        stops: g.stops.map((s) => ({ hex: s.color, opacity: s.opacity ?? 1, offset: s.offset })),
      },
    };
  }
  return { color: fillColor(f, token) };
}

function numberOf(v: string | number | null | undefined, fallback: number): number {
  const n = typeof v === 'number' ? v : parseFloat(String(v ?? ''));
  return Number.isFinite(n) ? n : fallback;
}

const runProps = ['fontFamily', 'fontWeight', 'fontStyle', 'fontSize', 'lineHeight', 'letterSpacing', 'textTransform', 'textDecoration'] as const;

function isMixed(r: TextRange): boolean {
  for (const p of runProps) if (r[p] === 'mixed') return true;
  return r.fills === 'mixed';
}

function runOf(r: TextRange, start: number, end: number): STextRun {
  const fills = r.fills === 'mixed' ? [] : r.fills;
  const fill = fills.find((f) => f.fillColor || f.fillColorGradient);
  return {
    start,
    end,
    characters: r.characters,
    fontFamily: String(r.fontFamily ?? ''),
    fontWeight: String(r.fontWeight ?? '400'),
    fontStyle: String(r.fontStyle ?? 'normal'),
    fontSize: numberOf(r.fontSize, 14),
    lineHeight: numberOf(r.lineHeight, 1.2),
    letterSpacing: numberOf(r.letterSpacing, 0),
    textTransform: String(r.textTransform ?? 'none'),
    textDecoration: String(r.textDecoration ?? 'none'),
    fill: fill?.fillColor ? fillColor(fill) : fill?.fillColorGradient ? { hex: fill.fillColorGradient.stops[0]?.color ?? '#000000', opacity: 1 } : undefined,
    gradientFill: !!fill?.fillColorGradient,
  };
}

// Style spans: if a range is 'mixed', split it in half and look again; adjacent spans with the same style are merged
function textRuns(t: Text): STextRun[] {
  const out: STextRun[] = [];
  const walk = (start: number, end: number) => {
    if (end <= start) return;
    const r = t.getRange(start, end);
    if (end - start === 1 || !isMixed(r)) {
      const run = runOf(r, start, end);
      const prev = out[out.length - 1];
      if (prev && JSON.stringify({ ...prev, start: 0, end: 0, characters: '' }) === JSON.stringify({ ...run, start: 0, end: 0, characters: '' })) {
        prev.end = end;
        prev.characters += run.characters;
      } else out.push(run);
      return;
    }
    const mid = (start + end) >> 1;
    walk(start, mid);
    walk(mid, end);
  };
  walk(0, t.characters.length);
  return out;
}

function shadowOf(s: PShadow): SNode['shadows'][number] {
  return {
    style: s.style ?? 'drop-shadow',
    offsetX: s.offsetX ?? 0,
    offsetY: s.offsetY ?? 0,
    blur: s.blur ?? 0,
    spread: s.spread ?? 0,
    color: { hex: s.color?.color ?? '#000000', opacity: s.color?.opacity ?? 1 },
  };
}

function rangeOr(t: Text, key: 'align', fallback: string | null): string | null {
  if (t.characters.length === 0) return fallback;
  const v = t.getRange(0, t.characters.length)[key];
  return v && v !== 'mixed' ? v : fallback;
}

export interface SnapOptions {
  variants: boolean; // include states from component variants too (not repeated inside variants)
}

export function snapshot(shape: Shape, board: Shape, opt: SnapOptions = { variants: true }): SNode {
  const tokens = shape.tokens ?? ({} as Shape['tokens']);
  const fills = shape.fills === 'mixed' ? [] : shape.fills;
  const n: SNode = {
    id: shape.id,
    name: shape.name,
    type: shape.type,
    x: shape === board ? 0 : shape.parentX,
    y: shape === board ? 0 : shape.parentY,
    absX: shape.x - board.x,
    absY: shape.y - board.y,
    width: shape.width,
    height: shape.height,
    rotation: shape.rotation ?? 0,
    opacity: shape.opacity ?? 1,
    blendMode: shape.blendMode ?? 'normal',
    hidden: shape.hidden,
    clip: shape.type === 'board' ? (shape as Board).clipContent : false,
    mask: shape.type === 'group' ? shape.isMask() : false,
    constraintsH: shape.constraintsHorizontal ?? 'left',
    constraintsV: shape.constraintsVertical ?? 'top',
    fills: shape.type === 'text' ? [] : fills.map((f) => snapFill(f, tokens['fill'])),
    strokes: shape.strokes.map((s) => ({
      color: s.strokeColor ? { hex: s.strokeColor, opacity: s.strokeOpacity ?? 1, token: tokens['strokeColor'] } : undefined,
      gradient: !!s.strokeColorGradient,
      width: s.strokeWidth ?? 1,
      alignment: s.strokeAlignment ?? 'center',
      style: s.strokeStyle ?? 'solid',
    })),
    shadows: shape.shadows.filter((s) => !s.hidden).map(shadowOf),
    blur: !!shape.blur && !shape.blur.hidden && (shape.blur.value ?? 0) > 0,
    backgroundBlur: !!shape.backgroundBlur && !shape.backgroundBlur.hidden && (shape.backgroundBlur.value ?? 0) > 0,
    radius: [shape.borderRadiusTopLeft ?? 0, shape.borderRadiusTopRight ?? 0, shape.borderRadiusBottomRight ?? 0, shape.borderRadiusBottomLeft ?? 0],
    // $token only when all four corners have the same token (numbers otherwise)
    radiusToken:
      tokens['borderRadiusTopLeft'] && ['borderRadiusTopRight', 'borderRadiusBottomRight', 'borderRadiusBottomLeft'].every((k) => tokens[k as keyof typeof tokens] === tokens['borderRadiusTopLeft'])
        ? tokens['borderRadiusTopLeft']
        : undefined,
    data: readData(shape),
    children: [],
  };
  if (shape.type === 'board') {
    const b = shape as Board;
    n.boardSizingH = b.horizontalSizing;
    n.boardSizingV = b.verticalSizing;
    const l = b.grid ?? b.flex;
    if (l) {
      const grid = b.grid !== undefined && b.grid !== null;
      n.layout = {
        kind: grid ? 'grid' : 'flex',
        dir: l.dir,
        wrap: !grid && b.flex?.wrap === 'wrap',
        rowGap: l.rowGap ?? 0,
        columnGap: l.columnGap ?? 0,
        padding: [l.topPadding ?? 0, l.rightPadding ?? 0, l.bottomPadding ?? 0, l.leftPadding ?? 0],
        justifyContent: l.justifyContent ?? 'start',
        alignItems: l.alignItems ?? 'start',
        alignContent: l.alignContent ?? 'start',
        justifyItems: l.justifyItems ?? 'start',
        rows: grid ? b.grid!.rows.map((t) => ({ type: t.type, value: t.value ?? 0 })) : [],
        columns: grid ? b.grid!.columns.map((t) => ({ type: t.type, value: t.value ?? 0 })) : [],
      };
    }
  }
  if (shape.layoutChild) {
    const c = shape.layoutChild;
    n.child = {
      absolute: c.absolute,
      horizontalSizing: c.horizontalSizing,
      verticalSizing: c.verticalSizing,
      alignSelf: c.alignSelf ?? 'auto',
      justifySelf: 'auto',
      margin: [c.topMargin ?? 0, c.rightMargin ?? 0, c.bottomMargin ?? 0, c.leftMargin ?? 0],
      minWidth: c.minWidth ?? null,
      maxWidth: c.maxWidth ?? null,
      minHeight: c.minHeight ?? null,
      maxHeight: c.maxHeight ?? null,
    };
  }
  if (shape.layoutCell && shape.layoutCell.row !== undefined) {
    const c = shape.layoutCell;
    n.cell = { row: c.row ?? 1, column: c.column ?? 1, rowSpan: c.rowSpan ?? 1, columnSpan: c.columnSpan ?? 1 };
  }
  if (shape.type === 'text') {
    const t = shape as Text;
    n.text = {
      characters: t.characters,
      growType: t.growType,
      // Alignment lives on the paragraph (text range): the shape's align may differ from the paragraph
      align: String(rangeOr(t, 'align', t.align) ?? 'left'),
      verticalAlign: String(t.verticalAlign ?? 'top'),
      runs: textRuns(t),
    };
  }
  if ('children' in shape && Array.isArray(shape.children)) {
    // Penpot child order starts from what's drawn on top (front). In .ui, what's written first is drawn below
    const kids = [...shape.children].sort((a, b) => a.parentIndex - b.parentIndex);
    n.children = kids.map((c) => snapshot(c, board, { variants: opt.variants }));
  }
  if (opt.variants && shape.isComponentInstance()) {
    // If the component is a variant with a "state" property, variants with other values become states
    const comp = shape.component();
    if (comp && comp.isVariant() && comp.variants) {
      const prop = comp.variants.properties.find((p) => p.toLowerCase() === 'state');
      if (prop) {
        const others = comp.variants.variantComponents();
        n.states = [];
        for (const v of others) {
          if (!v.isVariant()) continue;
          const value = v.variantProps[prop];
          if (!value || value === comp.variantProps[prop]) continue;
          const main = v.mainInstance();
          n.states.push({ state: value, node: snapshot(main, main, { variants: false }) });
        }
      }
    }
  }
  return n;
}
