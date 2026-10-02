import { CommandBus, createDefaultRegistry, createDocument } from '../src';

export function makeBus(width = 512, height = 512) {
  let t = 0;
  const bus = new CommandBus(createDefaultRegistry(), createDocument({ id: 'doc', width, height }), {
    idPrefix: 't',
    now: () => (t += 10_000),
  });
  return bus;
}

export const ids = (bus: CommandBus) => bus.document.layers.map((l) => l.id);

/** Fast typed-array equality (vitest's deep equality is very slow on large buffers). */
export function sameBytes(a: ArrayLike<number> & ArrayBufferView, b: ArrayLike<number> & ArrayBufferView): boolean {
  return Buffer.from(a.buffer, a.byteOffset, a.byteLength).equals(Buffer.from(b.buffer, b.byteOffset, b.byteLength));
}

import { existsSync, mkdirSync, readFileSync, writeFileSync } from 'node:fs';
import { dirname, join } from 'node:path';
import { fileURLToPath } from 'node:url';
import { expect } from 'vitest';
import { decodePng, encodePng, readRegion, type Document, type LayerNode, type TileGrid } from '../src';

const GOLDEN_DIR = join(dirname(fileURLToPath(import.meta.url)), 'golden');

/** Compares RGBA pixels with a stored golden PNG (≤ 1 LSB). Creates it when missing or UPDATE_GOLDENS=1. */
export function checkGolden(name: string, pixels: Uint8ClampedArray, width: number, height: number) {
  const file = join(GOLDEN_DIR, `${name}.png`);
  if (process.env.UPDATE_GOLDENS === '1' || !existsSync(file)) {
    mkdirSync(GOLDEN_DIR, { recursive: true });
    writeFileSync(file, encodePng(pixels, width, height, 4, 9));
    if (process.env.UPDATE_GOLDENS !== '1') throw new Error(`Golden ${name}.png was missing and has been created; re-run to verify`);
    return;
  }
  const g = decodePng(readFileSync(file));
  expect([g.width, g.height]).toEqual([width, height]);
  let max = 0;
  for (let i = 0; i < pixels.length; i++) max = Math.max(max, Math.abs(g.pixels[i] - pixels[i]));
  expect(max, `${name}: max channel difference`).toBeLessThanOrEqual(1);
}

export const pixel = (grid: TileGrid, x: number, y: number) => [...readRegion(grid, { x, y, width: 1, height: 1 })];
export const sel = (doc: Document, x: number, y: number) => (doc.selection ? pixel(doc.selection.mask, x, y)[0] : 0);
export const layerAt = (doc: Document, i: number) => doc.layers[i] as LayerNode & { tiles: TileGrid };
export const create = (bus: ReturnType<typeof makeBus>, params: Record<string, unknown> = {}) => bus.dispatch<{ layerId: string }>('layer.create', params).layerId;
