import type { Document } from '../document';
import { tileCols, tileKey, tileRows, TILE_SIZE, type Rect } from '../tiles';
import { adjustBuffers, blendBuffers, mixBuffers } from './blend';
import { documentTilePlan, type MaskRef, type Plan } from './plan';

const maskBuf = (m: MaskRef | null) => (m ? { data: m.tile.data, density: m.density } : null);

const TILE_BYTES = TILE_SIZE * TILE_SIZE * 4;

export function executePlanCPU(plan: Plan, target: Uint8ClampedArray): Uint8ClampedArray {
  for (const item of plan) {
    switch (item.kind) {
      case 'layer':
        blendBuffers(target, item.tile.data, item.mode, item.opacity, maskBuf(item.mask));
        break;
      case 'isolated': {
        const inner = executePlanCPU(item.children, new Uint8ClampedArray(TILE_BYTES));
        blendBuffers(target, inner, item.mode, item.opacity, maskBuf(item.mask));
        break;
      }
      case 'passThrough':
        if (item.opacity >= 1 && !item.mask) executePlanCPU(item.children, target);
        else mixBuffers(target, executePlanCPU(item.children, new Uint8ClampedArray(target)), item.opacity, maskBuf(item.mask));
        break;
      case 'adjust':
        adjustBuffers(target, item.adjustment, item.mode, item.opacity, maskBuf(item.mask));
        break;
    }
  }
  return target;
}

/** Composites one 256×256 tile (straight RGBA8). */
export function compositeTileCPU(doc: Document, tx: number, ty: number): Uint8ClampedArray {
  return executePlanCPU(documentTilePlan(doc, tileKey(tx, ty)), new Uint8ClampedArray(TILE_BYTES));
}

/** Flattens the document (or a region of it) to straight RGBA8 — the reference renderer for export and tests. */
export function renderDocumentCPU(doc: Document, rect: Rect = { x: 0, y: 0, width: doc.width, height: doc.height }): Uint8ClampedArray {
  const out = new Uint8ClampedArray(rect.width * rect.height * 4);
  const tx0 = Math.max(0, Math.floor(rect.x / TILE_SIZE));
  const ty0 = Math.max(0, Math.floor(rect.y / TILE_SIZE));
  const tx1 = Math.min(tileCols(doc.width) - 1, Math.floor((rect.x + rect.width - 1) / TILE_SIZE));
  const ty1 = Math.min(tileRows(doc.height) - 1, Math.floor((rect.y + rect.height - 1) / TILE_SIZE));
  for (let ty = ty0; ty <= ty1; ty++) {
    for (let tx = tx0; tx <= tx1; tx++) {
      const plan = documentTilePlan(doc, tileKey(tx, ty));
      if (plan.length === 0) continue;
      const tile = executePlanCPU(plan, new Uint8ClampedArray(TILE_BYTES));
      const x0 = Math.max(rect.x, tx * TILE_SIZE);
      const x1 = Math.min(rect.x + rect.width, (tx + 1) * TILE_SIZE, doc.width);
      const y0 = Math.max(rect.y, ty * TILE_SIZE);
      const y1 = Math.min(rect.y + rect.height, (ty + 1) * TILE_SIZE, doc.height);
      for (let y = y0; y < y1; y++) {
        const src = ((y - ty * TILE_SIZE) * TILE_SIZE + (x0 - tx * TILE_SIZE)) * 4;
        out.set(tile.subarray(src, src + (x1 - x0) * 4), ((y - rect.y) * rect.width + (x0 - rect.x)) * 4);
      }
    }
  }
  return out;
}

/** Area-averaged (alpha-weighted) downscale of straight RGBA to fit within `maxSize`. */
export function downscaleRGBA(
  src: Uint8ClampedArray,
  srcW: number,
  srcH: number,
  maxSize: number,
): { width: number; height: number; pixels: Uint8ClampedArray } {
  const scale = Math.min(1, maxSize / Math.max(srcW, srcH));
  const width = Math.max(1, Math.round(srcW * scale));
  const height = Math.max(1, Math.round(srcH * scale));
  const out = new Uint8ClampedArray(width * height * 4);
  for (let y = 0; y < height; y++) {
    const sy0 = Math.floor((y * srcH) / height);
    const sy1 = Math.max(sy0 + 1, Math.floor(((y + 1) * srcH) / height));
    for (let x = 0; x < width; x++) {
      const sx0 = Math.floor((x * srcW) / width);
      const sx1 = Math.max(sx0 + 1, Math.floor(((x + 1) * srcW) / width));
      let r = 0, g = 0, b = 0, a = 0;
      for (let sy = sy0; sy < sy1; sy++) {
        for (let sx = sx0; sx < sx1; sx++) {
          const i = (sy * srcW + sx) * 4;
          const al = src[i + 3];
          r += src[i] * al;
          g += src[i + 1] * al;
          b += src[i + 2] * al;
          a += al;
        }
      }
      const o = (y * width + x) * 4;
      if (a > 0) {
        out[o] = r / a;
        out[o + 1] = g / a;
        out[o + 2] = b / a;
      }
      out[o + 3] = a / ((sy1 - sy0) * (sx1 - sx0));
    }
  }
  return { width, height, pixels: out };
}

/** Thumbnail of the flattened document (CPU path, used where no GPU renderer exists). */
export function renderThumbnailCPU(doc: Document, maxSize = 256): { width: number; height: number; pixels: Uint8ClampedArray } {
  return downscaleRGBA(renderDocumentCPU(doc), doc.width, doc.height, maxSize);
}

export function allTileKeysOf(doc: { width: number; height: number }): number[] {
  const keys: number[] = [];
  for (let ty = 0; ty < tileRows(doc.height); ty++) for (let tx = 0; tx < tileCols(doc.width); tx++) keys.push(tileKey(tx, ty));
  return keys;
}
