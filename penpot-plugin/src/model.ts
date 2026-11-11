// Snapshot of Penpot shapes (JSON). snapshot.ts makes it inside the plugin with the Penpot API, and convert.ts turns it into .ui.
// The converter doesn't know the Penpot API, so it's tested in Node with fixed data (test/fixtures).

export type Sizing = 'fix' | 'fill' | 'auto';
export type AlignValue = 'start' | 'end' | 'center' | 'stretch';
export type ContentValue = 'start' | 'center' | 'end' | 'space-between' | 'space-around' | 'space-evenly' | 'stretch';

export interface SColor {
  hex: string; // #rrggbb
  opacity: number; // 0..1
  token?: string; // design token name (as theme token $name)
}

export interface SGradient {
  type: 'linear' | 'radial';
  startX: number; // box-relative 0..1
  startY: number;
  endX: number;
  endY: number;
  width: number; // radial: width to height ratio
  stops: { hex: string; opacity: number; offset: number }[];
}

export interface SFill {
  color?: SColor;
  gradient?: SGradient;
  image?: boolean;
}

export interface SStroke {
  color?: SColor;
  gradient?: boolean;
  width: number;
  alignment: 'center' | 'inner' | 'outer';
  style: string; // solid, dashed, dotted, ...
}

export interface SShadow {
  style: 'drop-shadow' | 'inner-shadow';
  offsetX: number;
  offsetY: number;
  blur: number;
  spread: number;
  color: SColor;
}

export interface STextRun {
  start: number;
  end: number;
  characters: string;
  fontFamily: string;
  fontWeight: string;
  fontStyle: string; // normal | italic
  fontSize: number;
  lineHeight: number; // multiple of the font size
  letterSpacing: number;
  textTransform: string; // none | uppercase | lowercase | capitalize
  textDecoration: string; // none | underline | line-through
  fill?: SColor;
  gradientFill?: boolean;
}

export interface SText {
  characters: string;
  growType: 'fixed' | 'auto-width' | 'auto-height';
  align: string; // left | center | right | justify
  verticalAlign: string; // top | center | bottom
  runs: STextRun[];
}

export interface STrack {
  type: 'flex' | 'fixed' | 'percent' | 'auto';
  value: number;
}

export interface SLayout {
  kind: 'flex' | 'grid';
  dir: string; // flex: row | row-reverse | column | column-reverse, grid: row | column
  wrap: boolean;
  rowGap: number;
  columnGap: number;
  padding: [number, number, number, number]; // top right bottom left
  justifyContent: ContentValue;
  alignItems: AlignValue;
  alignContent: ContentValue;
  justifyItems: AlignValue;
  rows: STrack[];
  columns: STrack[];
}

export interface SChild {
  absolute: boolean;
  horizontalSizing: Sizing;
  verticalSizing: Sizing;
  alignSelf: string; // auto | start | center | end | stretch
  justifySelf: string;
  margin: [number, number, number, number];
  minWidth: number | null;
  maxWidth: number | null;
  minHeight: number | null;
  maxHeight: number | null;
}

export interface SCell {
  row: number; // from 1
  column: number;
  rowSpan: number;
  columnSpan: number;
}

// Engine values set in the plugin panel (the shape's plugin data "mealkit", JSON)
export interface MealkitData {
  visible?: string;
  disabled?: string;
  widthRatio?: string;
  colorField?: string;
  widget?: 'button' | 'toggle' | 'slider' | 'select' | 'textInput' | 'scroll' | 'list';
  action?: string;
  field?: string;
  min?: number;
  max?: number;
  step?: number;
  options?: string; // string keys, comma-separated
  placeholder?: string;
  maxLength?: number;
  scrollHorizontal?: boolean;
  focusUp?: string;
  focusDown?: string;
  focusLeft?: string;
  focusRight?: string;
  slice?: [number, number, number, number]; // 9-slice margins (left top right bottom, logical pixels of the baked PNG)
  overflow?: 'ellipsis' | 'clip';
  maxLines?: number;
  wrap?: 'word' | 'syllable' | 'none';
  transition?: number;
  bake?: boolean; // always bake to PNG
  key?: string; // text: string key (if empty, the layer name when it's shaped like a key)
  // Boards (screens) only
  navigation?: boolean;
  firstFocus?: string;
  back?: string;
}

export interface SNode {
  id: string;
  name: string;
  type: string; // board | rectangle | ellipse | text | group | path | boolean | image | svg-raw
  x: number; // relative to the parent
  y: number;
  absX: number; // relative to the exported board
  absY: number;
  width: number;
  height: number;
  rotation: number;
  opacity: number;
  blendMode: string;
  hidden: boolean;
  clip: boolean;
  mask: boolean; // mask group
  constraintsH: string; // left | right | leftright | center | scale
  constraintsV: string; // top | bottom | topbottom | center | scale
  boardSizingH?: 'auto' | 'fix'; // boards with a layout: fit content
  boardSizingV?: 'auto' | 'fix';
  fills: SFill[];
  strokes: SStroke[];
  shadows: SShadow[];
  blur: boolean;
  backgroundBlur: boolean;
  radius: [number, number, number, number]; // top left, top right, bottom right, bottom left
  radiusToken?: string;
  layout?: SLayout;
  child?: SChild; // when the parent has a layout
  cell?: SCell; // when the parent is a Grid
  text?: SText;
  data?: MealkitData;
  states?: { state: string; node: SNode }[]; // states from component variants (hover, pressed ...)
  children: SNode[]; // from the bottom layer (drawn first)
}

// Export results
export interface Bake {
  id: string; // id of the shape to bake
  path: string; // PNG path relative to assets
  scale: number;
  skipChildren: boolean; // background only (children converted separately)
}

export interface StringEntry {
  key: string;
  text: string;
}

export interface Box {
  id: string;
  name: string;
  x: number;
  y: number;
  w: number;
  h: number;
  text?: string; // text nodes: Penpot's text (with tags, newlines as \n). Compared only when the engine draws the same text
}

export interface Result {
  ui: string;
  bakes: Bake[];
  strings: StringEntry[];
  boxes: Box[];
  warnings: string[];
  errors: string[];
  nodeCount: number;
}

export interface Options {
  file: string; // .ui path relative to assets (ui/pause.ui)
  fileName: string; // Penpot file name
  fileId: string;
  fonts: string[]; // file names in assets/fonts (without extension)
  nodeBudget: number; // node count budget per screen
}
