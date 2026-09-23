import {
  createDocument,
  createPixelLayer,
  gridFromPixels,
  historyMetadata,
  readCnva,
  writeCnva,
  type CommandBus,
  type Document,
} from '@canvas-ai/core';
import { downscaleRGBA, type Renderer } from '@canvas-ai/render';

export const IMAGE_EXTENSIONS = ['png', 'jpg', 'jpeg', 'webp', 'gif', 'bmp', 'avif'];

let docCounter = 0;
export const newDocId = () => `doc_${Date.now().toString(36)}${(++docCounter).toString(36)}`;

export const baseName = (name: string) => name.replace(/\.[^.]+$/, '');

/** Decodes an image file with the browser's codecs into straight-alpha RGBA. */
export async function decodeImage(bytes: Uint8Array): Promise<{ width: number; height: number; pixels: Uint8ClampedArray }> {
  const bitmap = await createImageBitmap(new Blob([bytes as Uint8Array<ArrayBuffer>]), {
    premultiplyAlpha: 'none',
    colorSpaceConversion: 'none',
  });
  const canvas = new OffscreenCanvas(bitmap.width, bitmap.height);
  const ctx = canvas.getContext('2d', { willReadFrequently: true })!;
  ctx.drawImage(bitmap, 0, 0);
  bitmap.close();
  const data = ctx.getImageData(0, 0, canvas.width, canvas.height);
  return { width: data.width, height: data.height, pixels: data.data };
}

export async function documentFromImage(bytes: Uint8Array, name: string): Promise<Document> {
  const img = await decodeImage(bytes);
  const doc = createDocument({ id: newDocId(), name: baseName(name), width: img.width, height: img.height });
  const layer = createPixelLayer(doc, `layer_bg_${doc.id}`, 'Background', gridFromPixels(img.pixels, img.width, img.height));
  return { ...doc, layers: [layer] };
}

export function documentFromCnva(bytes: Uint8Array): Document {
  const { document } = readCnva(bytes);
  // Give every opened copy a fresh id so two tabs of the same file never collide.
  return { ...document, id: newDocId() };
}

export async function saveCnva(bus: CommandBus, renderer: Renderer | null): Promise<Uint8Array> {
  const doc = bus.document;
  let thumbnail: { width: number; height: number; pixels: Uint8ClampedArray } | undefined;
  if (renderer) {
    const full = await renderer.readComposite();
    thumbnail = downscaleRGBA(full, doc.width, doc.height, 256);
  }
  return writeCnva({ document: doc, thumbnail, history: historyMetadata(bus.history.entries, bus.history.cursor) });
}

export type ExportFormat = 'png' | 'jpeg' | 'webp';

/** Encodes a flattened image with the browser's encoders. JPEG has no alpha, so it is flattened onto `matte`. */
export async function encodeImage(
  pixels: Uint8ClampedArray,
  width: number,
  height: number,
  format: ExportFormat,
  quality = 0.92,
  matte: [number, number, number] = [255, 255, 255],
): Promise<Uint8Array> {
  let data = pixels;
  if (format === 'jpeg') {
    data = new Uint8ClampedArray(pixels.length);
    for (let i = 0; i < pixels.length; i += 4) {
      const a = pixels[i + 3] / 255;
      for (let c = 0; c < 3; c++) data[i + c] = pixels[i + c] * a + matte[c] * (1 - a);
      data[i + 3] = 255;
    }
  }
  const canvas = new OffscreenCanvas(width, height);
  canvas.getContext('2d')!.putImageData(new ImageData(data as Uint8ClampedArray<ArrayBuffer>, width, height), 0, 0);
  const blob = await canvas.convertToBlob({ type: `image/${format}`, quality });
  return new Uint8Array(await blob.arrayBuffer());
}
