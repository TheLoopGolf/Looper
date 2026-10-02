import { emptyGrid, intersectRect, type Rect, type TileGrid } from '../tiles';
import { createRegion, gridBounds, readGrid, writeGrid } from './region';

/** 2D affine [a, b, c, d, e, f]: x' = a·x + c·y + e, y' = b·x + d·y + f (canvas/DOMMatrix order). */
export type Affine = readonly [number, number, number, number, number, number];

export const IDENTITY: Affine = [1, 0, 0, 1, 0, 0];

export function multiply(m: Affine, n: Affine): Affine {
  // m ∘ n (apply n first)
  return [
    m[0] * n[0] + m[2] * n[1],
    m[1] * n[0] + m[3] * n[1],
    m[0] * n[2] + m[2] * n[3],
    m[1] * n[2] + m[3] * n[3],
    m[0] * n[4] + m[2] * n[5] + m[4],
    m[1] * n[4] + m[3] * n[5] + m[5],
  ];
}

export function invert(m: Affine): Affine {
  const det = m[0] * m[3] - m[1] * m[2];
  if (Math.abs(det) < 1e-12) throw new Error('Transform is not invertible');
  return [m[3] / det, -m[1] / det, -m[2] / det, m[0] / det, (m[2] * m[5] - m[3] * m[4]) / det, (m[1] * m[4] - m[0] * m[5]) / det];
}

export const translate = (tx: number, ty: number): Affine => [1, 0, 0, 1, tx, ty];
export const scale = (sx: number, sy: number): Affine => [sx, 0, 0, sy, 0, 0];
export function rotate(deg: number): Affine {
  const a = (deg * Math.PI) / 180;
  const c = Math.cos(a), s = Math.sin(a);
  return [c, s, -s, c, 0, 0];
}

export function apply(m: Affine, x: number, y: number): [number, number] {
  return [m[0] * x + m[2] * y + m[4], m[1] * x + m[3] * y + m[5]];
}

/** Axis-aligned bounds of a rect after an affine transform. */
export function transformRect(m: Affine, r: Rect): Rect {
  const pts = [apply(m, r.x, r.y), apply(m, r.x + r.width, r.y), apply(m, r.x, r.y + r.height), apply(m, r.x + r.width, r.y + r.height)];
  const x0 = Math.floor(Math.min(...pts.map((p) => p[0]))), y0 = Math.floor(Math.min(...pts.map((p) => p[1])));
  const x1 = Math.ceil(Math.max(...pts.map((p) => p[0]))), y1 = Math.ceil(Math.max(...pts.map((p) => p[1])));
  return { x: x0, y: y0, width: x1 - x0, height: y1 - y0 };
}

const isIntegerTranslation = (m: Affine) => m[0] === 1 && m[1] === 0 && m[2] === 0 && m[3] === 1 && Number.isInteger(m[4]) && Number.isInteger(m[5]);

/**
 * Resamples a grid through `forward` (source → output coordinates) into a new
 * grid of the given size. Integer translations are copied exactly; anything
 * else is bilinear (premultiplied for RGBA). `defaultValue` is the value of
 * missing tiles (255 for reveal-all masks).
 */
export function resampleGrid(grid: TileGrid, forward: Affine, outW: number, outH: number, defaultValue = 0, interp: 'bilinear' | 'nearest' = 'bilinear'): TileGrid {
  const out = emptyGrid(outW, outH, grid.channels);
  const srcBounds = gridBounds(grid);
  if (!srcBounds) return out;
  const dstRect = intersectRect(transformRect(forward, srcBounds), { x: 0, y: 0, width: outW, height: outH });
  if (!dstRect) return out;
  const ch = grid.channels;
  if (isIntegerTranslation(forward)) {
    const src = readGrid(grid, { x: dstRect.x - forward[4], y: dstRect.y - forward[5], width: dstRect.width, height: dstRect.height }, defaultValue);
    return writeGrid(out, { ...src, x: dstRect.x, y: dstRect.y }, defaultValue);
  }
  const inv = invert(forward);
  const src = readGrid(grid, srcBounds, defaultValue);
  const dst = createRegion(dstRect, ch, defaultValue);
  const sw = src.width, sh = src.height;
  const fetch = (x: number, y: number, c: number) => {
    if (x < 0 || y < 0 || x >= sw || y >= sh) return c === ch - 1 || ch === 1 ? defaultValue : 0;
    return src.data[(y * sw + x) * ch + c];
  };
  for (let y = 0; y < dstRect.height; y++) {
    for (let x = 0; x < dstRect.width; x++) {
      const [fx, fy] = apply(inv, dstRect.x + x + 0.5, dstRect.y + y + 0.5);
      const o = (y * dstRect.width + x) * ch;
      const sx = fx - 0.5 - src.x, sy = fy - 0.5 - src.y;
      if (interp === 'nearest') {
        const nx = Math.round(sx), ny = Math.round(sy);
        for (let c = 0; c < ch; c++) dst.data[o + c] = fetch(nx, ny, c);
        continue;
      }
      const x0 = Math.floor(sx), y0 = Math.floor(sy);
      const tx = sx - x0, ty = sy - y0;
      const w = [(1 - tx) * (1 - ty), tx * (1 - ty), (1 - tx) * ty, tx * ty];
      const px = [x0, x0 + 1, x0, x0 + 1], py = [y0, y0, y0 + 1, y0 + 1];
      if (ch === 1) {
        let v = 0;
        for (let k = 0; k < 4; k++) v += fetch(px[k], py[k], 0) * w[k];
        dst.data[o] = Math.round(v);
        continue;
      }
      let a = 0, r = 0, g = 0, b = 0;
      for (let k = 0; k < 4; k++) {
        const al = fetch(px[k], py[k], 3) * w[k];
        a += al;
        r += fetch(px[k], py[k], 0) * al;
        g += fetch(px[k], py[k], 1) * al;
        b += fetch(px[k], py[k], 2) * al;
      }
      if (a > 0) {
        dst.data[o] = Math.round(r / a);
        dst.data[o + 1] = Math.round(g / a);
        dst.data[o + 2] = Math.round(b / a);
      }
      dst.data[o + 3] = Math.round(a);
    }
  }
  return writeGrid(out, dst, defaultValue);
}
