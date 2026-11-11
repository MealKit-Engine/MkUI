// Bundling exported files: the request body sent to the editor import server (editor/ImportServer.cpp), and a zip downloaded when there's no editor.

export interface ImportEntry {
  kind: 'ui' | 'png' | 'clean' | 'theme' | 'strings' | 'penpot' | 'boxes' | 'preview';
  path: string; // ui/png/theme/clean: relative to assets, penpot/boxes/preview: file name relative to art/ui, strings: "-"
  data: Uint8Array;
  force?: boolean; // ui: replaces hand-written files too
}

// The plugin sandbox (SES) has no TextEncoder: used only in the panel (iframe)
let enc: TextEncoder | null = null;
export function text(s: string): Uint8Array {
  enc ??= new TextEncoder();
  return enc.encode(s);
}

// "MEALKIT-IMPORT 1\n" + (<kind> <path> <size>[ force]\n<bytes>\n)*
export function buildImport(entries: ImportEntry[]): Uint8Array {
  const parts: Uint8Array[] = [text('MEALKIT-IMPORT 1\n')];
  for (const e of entries) {
    parts.push(text(`${e.kind} ${e.path} ${e.data.length}${e.force ? ' force' : ''}\n`), e.data, text('\n'));
  }
  return concat(parts);
}

export function concat(parts: Uint8Array[]): Uint8Array {
  const out = new Uint8Array(parts.reduce((n, p) => n + p.length, 0));
  let at = 0;
  for (const p of parts) {
    out.set(p, at);
    at += p.length;
  }
  return out;
}

let crcTable: Uint32Array | null = null;
export function crc32(data: Uint8Array): number {
  if (!crcTable) {
    crcTable = new Uint32Array(256);
    for (let i = 0; i < 256; ++i) {
      let c = i;
      for (let k = 0; k < 8; ++k) c = c & 1 ? 0xedb88320 ^ (c >>> 1) : c >>> 1;
      crcTable[i] = c >>> 0;
    }
  }
  let c = 0xffffffff;
  for (let i = 0; i < data.length; ++i) c = crcTable[(c ^ data[i]) & 0xff] ^ (c >>> 8);
  return (c ^ 0xffffffff) >>> 0;
}

// Uncompressed zip (UTF-8 file names)
export function buildZip(files: { path: string; data: Uint8Array }[]): Uint8Array {
  const local: Uint8Array[] = [];
  const central: Uint8Array[] = [];
  let offset = 0;
  for (const f of files) {
    const name = text(f.path);
    const crc = crc32(f.data);
    const h = new DataView(new ArrayBuffer(30));
    h.setUint32(0, 0x04034b50, true);
    h.setUint16(4, 20, true);
    h.setUint16(6, 0x0800, true); // UTF-8 names
    h.setUint32(14, crc, true);
    h.setUint32(18, f.data.length, true);
    h.setUint32(22, f.data.length, true);
    h.setUint16(26, name.length, true);
    local.push(new Uint8Array(h.buffer), name, f.data);
    const c = new DataView(new ArrayBuffer(46));
    c.setUint32(0, 0x02014b50, true);
    c.setUint16(4, 20, true);
    c.setUint16(6, 20, true);
    c.setUint16(8, 0x0800, true);
    c.setUint32(16, crc, true);
    c.setUint32(20, f.data.length, true);
    c.setUint32(24, f.data.length, true);
    c.setUint16(28, name.length, true);
    c.setUint32(42, offset, true);
    central.push(new Uint8Array(c.buffer), name);
    offset += 30 + name.length + f.data.length;
  }
  const centralBytes = concat(central);
  const end = new DataView(new ArrayBuffer(22));
  end.setUint32(0, 0x06054b50, true);
  end.setUint16(8, files.length, true);
  end.setUint16(10, files.length, true);
  end.setUint32(12, centralBytes.length, true);
  end.setUint32(16, offset, true);
  return concat([...local, centralBytes, new Uint8Array(end.buffer)]);
}

// Size of a baked PNG (IHDR)
export function pngSize(data: Uint8Array): { w: number; h: number } | null {
  if (data.length < 24 || data[0] !== 0x89 || data[1] !== 0x50) return null;
  const v = new DataView(data.buffer, data.byteOffset, data.byteLength);
  return { w: v.getUint32(16), h: v.getUint32(20) };
}
