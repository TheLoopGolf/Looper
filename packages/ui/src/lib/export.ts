import { downscaleRGBA, renderDocumentCPU, type Document } from '@canvas-ai/core';
import { strToU8, zipSync } from 'fflate';
import { encodeImage, type ExportFormat } from './image-io';

export interface ExportOptions {
  format: ExportFormat;
  /** 0..1 for lossy formats. */
  quality: number;
  width: number;
  height: number;
  /** When false, a Software tag is written (PNG tEXt / JPEG comment). Nothing else is ever embedded. */
  stripMetadata: boolean;
}

/** Resizes straight RGBA: area-averaging when shrinking, bilinear (canvas) when enlarging. */
export async function resizeRGBA(px: Uint8ClampedArray, w: number, h: number, tw: number, th: number): Promise<Uint8ClampedArray> {
  if (tw === w && th === h) return px;
  if (tw <= w && th <= h) {
    // downscaleRGBA fits within a box; scale both axes exactly by fitting the larger ratio first.
    const out = downscaleRGBA(px, w, h, Math.max(tw, th));
    if (out.width === tw && out.height === th) return out.pixels;
  }
  const src = new OffscreenCanvas(w, h);
  src.getContext('2d')!.putImageData(new ImageData(px as Uint8ClampedArray<ArrayBuffer>, w, h), 0, 0);
  const dst = new OffscreenCanvas(tw, th);
  const g = dst.getContext('2d')!;
  g.imageSmoothingEnabled = true;
  g.imageSmoothingQuality = 'high';
  g.drawImage(src, 0, 0, tw, th);
  return g.getImageData(0, 0, tw, th).data;
}

const CRC = (() => {
  const t = new Uint32Array(256);
  for (let n = 0; n < 256; n++) {
    let c = n;
    for (let k = 0; k < 8; k++) c = c & 1 ? 0xedb88320 ^ (c >>> 1) : c >>> 1;
    t[n] = c >>> 0;
  }
  return t;
})();

function pngTextChunk(key: string, value: string): Uint8Array {
  const data = strToU8(`${key}\0${value}`);
  const chunk = new Uint8Array(12 + data.length);
  const dv = new DataView(chunk.buffer);
  dv.setUint32(0, data.length);
  chunk.set(strToU8('tEXt'), 4);
  chunk.set(data, 8);
  let c = 0xffffffff;
  for (let i = 4; i < 8 + data.length; i++) c = CRC[(c ^ chunk[i]) & 0xff] ^ (c >>> 8);
  dv.setUint32(8 + data.length, (c ^ 0xffffffff) >>> 0);
  return chunk;
}

/** Adds a "Software" tag (PNG tEXt after IHDR, JPEG COM after SOI). */
export function addSoftwareTag(bytes: Uint8Array, format: ExportFormat): Uint8Array {
  const tag = 'Canvas AI';
  if (format === 'png' && bytes[1] === 0x50) {
    const ihdrEnd = 8 + 12 + 13;
    const chunk = pngTextChunk('Software', tag);
    const out = new Uint8Array(bytes.length + chunk.length);
    out.set(bytes.subarray(0, ihdrEnd));
    out.set(chunk, ihdrEnd);
    out.set(bytes.subarray(ihdrEnd), ihdrEnd + chunk.length);
    return out;
  }
  if (format === 'jpeg' && bytes[0] === 0xff && bytes[1] === 0xd8) {
    const text = strToU8(`Software: ${tag}`);
    const seg = new Uint8Array(4 + text.length);
    seg.set([0xff, 0xfe, (text.length + 2) >> 8, (text.length + 2) & 255]);
    seg.set(text, 4);
    const out = new Uint8Array(bytes.length + seg.length);
    out.set(bytes.subarray(0, 2));
    out.set(seg, 2);
    out.set(bytes.subarray(2), 2 + seg.length);
    return out;
  }
  return bytes;
}

export async function exportComposite(pixels: Uint8ClampedArray, doc: Pick<Document, 'width' | 'height'>, o: ExportOptions): Promise<Uint8Array> {
  const resized = await resizeRGBA(pixels, doc.width, doc.height, o.width, o.height);
  const bytes = await encodeImage(resized, o.width, o.height, o.format, o.quality);
  return o.stripMetadata ? bytes : addSoftwareTag(bytes, o.format);
}

/** Each top-level layer rendered on its own (hidden ones included) as PNGs in a zip. */
export async function exportLayersZip(doc: Document, o: Pick<ExportOptions, 'width' | 'height' | 'stripMetadata'>): Promise<Uint8Array> {
  const files: Record<string, [Uint8Array, { level: 0 }]> = {};
  const used = new Set<string>();
  for (const [i, layer] of [...doc.layers].reverse().entries()) {
    const px = renderDocumentCPU({ ...doc, layers: [{ ...layer, visible: true }] });
    const bytes = await exportComposite(px, doc, { ...o, format: 'png', quality: 1 });
    let name = `${String(i + 1).padStart(2, '0')}-${layer.name.replace(/[^\w.-]+/g, '_') || 'layer'}.png`;
    while (used.has(name)) name = name.replace(/\.png$/, '_.png');
    used.add(name);
    files[name] = [bytes, { level: 0 }];
  }
  return zipSync(files);
}

let avifSupport: Promise<boolean> | null = null;
export function supportsAvif(): Promise<boolean> {
  avifSupport ??= new OffscreenCanvas(1, 1)
    .convertToBlob({ type: 'image/avif' })
    .then((b) => b.type === 'image/avif')
    .catch(() => false);
  return avifSupport;
}
