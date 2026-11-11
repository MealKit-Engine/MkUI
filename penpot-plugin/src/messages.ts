// Messages between the plugin code (plugin.ts, inside Penpot) ↔ the panel (ui.ts, iframe)

import type { Box, MealkitData, StringEntry } from './model.ts';

export interface LayerInfo {
  id: string;
  name: string;
  type: string;
  topBoard: boolean; // a board directly under the page (one screen)
  data: MealkitData;
  file: string; // screen board: .ui path to export (relative to assets)
}

export interface BoardInfo {
  id: string;
  name: string;
  file: string;
}

export interface ExportedScreen {
  board: string;
  file: string;
  ui: string;
  images: { path: string; data: Uint8Array; logicalW: number; logicalH: number }[];
  strings: StringEntry[];
  boxes: Box[];
  preview?: Uint8Array; // PNG of the whole board (scale kPreviewScale, pixel comparison art/ui/preview)
  warnings: string[];
  errors: string[];
  nodeCount: number;
}

export type ToUi =
  | { type: 'init'; fileName: string; fileId: string; theme: string }
  | { type: 'selection'; layers: LayerInfo[] }
  | { type: 'boards'; boards: BoardInfo[] }
  | { type: 'progress'; text: string }
  | {
      type: 'exported';
      screens: ExportedScreen[];
      theme?: { ui: string; warnings: string[] };
      source?: { name: string; data: Uint8Array };
      fileName: string;
    }
  | { type: 'error'; message: string };

export type ToPlugin =
  | { type: 'ready' }
  | { type: 'setData'; id: string; data: MealkitData }
  | { type: 'setFile'; id: string; file: string }
  | { type: 'select'; id: string }
  | { type: 'example' }
  | { type: 'export'; boardIds: string[]; fonts: string[]; nodeBudget: number; theme: boolean; source: boolean };
