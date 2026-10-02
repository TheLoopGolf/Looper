import type { RGBA } from '../document';
import { createTile, tileCoords, TILE_SIZE, withTiles, type Rect, type Tile, type TileGrid } from '../tiles';
import { isTileEmpty } from '../tiles';
import { type StrokeAccumulator } from './brush';
import { createRegion, type Region } from './region';
import { overPixel } from './shapes';

export type PaintMode = 'paint' | 'erase';

export interface StrokeApplyOptions {
  readonly mode: PaintMode;
  readonly color: RGBA;
  /** Selection mask limiting the stroke (null = everything). */
  readonly selection: TileGrid | null;
  /** For 1-channel targets: value of missing tiles (255 for reveal-all masks). */
  readonly defaultValue?: number;
  /** For 1-channel targets: value painting moves toward (default: the color's grey level; erasing moves toward 0). */
  readonly value?: number;
}

const luma8 = (c: RGBA) => Math.round(0.299 * c[0] + 0.587 * c[1] + 0.114 * c[2]);

/**
 * Applies accumulated stroke coverage to `source` for the given tile keys.
 * Always starts from `source` (the pre-stroke content), so repeated previews
 * and the final command give identical results.
 */
export function applyStroke(source: TileGrid, acc: StrokeAccumulator, keys: Iterable<number>, opts: StrokeApplyOptions, base: TileGrid = source): TileGrid {
  const { channels } = source;
  const opacity = acc.brush.opacity;
  const updates: [number, Tile | null][] = [];
  const def = opts.defaultValue ?? 0;
  for (const key of keys) {
    const cov = acc.tiles.get(key);
    if (!cov) continue;
    const prev = source.tiles.get(key);
    const sel = opts.selection ? opts.selection.tiles.get(key) : null;
    if (opts.selection && !sel) continue; // nothing selected in this tile
    const data = prev ? new Uint8ClampedArray(prev.data) : new Uint8ClampedArray(TILE_SIZE * TILE_SIZE * channels).fill(channels === 1 ? def : 0);
    const [tx, ty] = tileCoords(key);
    const w = Math.min(TILE_SIZE, source.width - tx * TILE_SIZE);
    const h = Math.min(TILE_SIZE, source.height - ty * TILE_SIZE);
    const target = channels === 1 ? (opts.mode === 'paint' ? (opts.value ?? luma8(opts.color)) : 0) : 0;
    for (let y = 0; y < h; y++) {
      for (let x = 0; x < w; x++) {
        const p = y * TILE_SIZE + x;
        let c = cov[p] * opacity;
        if (sel) c *= sel.data[p] / 255;
        if (c <= 0) continue;
        if (channels === 1) {
          data[p] = Math.round(data[p] + (target - data[p]) * c);
        } else if (opts.mode === 'erase') {
          data[p * 4 + 3] = Math.round(data[p * 4 + 3] * (1 - c));
        } else {
          overPixel(data, p * 4, opts.color[0], opts.color[1], opts.color[2], (c * opts.color[3]) / 255);
        }
      }
    }
    const tile = createTile(channels, data);
    const empty = channels === 1 ? def === 0 && isTileEmpty(tile) : isTileEmpty(tile);
    updates.push([key, empty ? null : tile]);
  }
  // `base` lets incremental previews keep tiles painted by earlier calls.
  return withTiles(base, updates);
}

// ---------------------------------------------------------------------------
// Fills
// ---------------------------------------------------------------------------

/** Composites a solid color over an RGBA region through per-pixel coverage (1-channel region of the same rect). */
export function fillRegion(dst: Region, coverage: Region | null, color: RGBA, opacity = 1): void {
  for (let i = 0, p = 0; i < dst.data.length; i += 4, p++) {
    const c = (coverage ? coverage.data[p] / 255 : 1) * opacity * (color[3] / 255);
    overPixel(dst.data, i, color[0], color[1], color[2], c);
  }
}

export interface GradientStop {
  readonly offset: number;
  readonly color: RGBA;
}

export type GradientKind = 'linear' | 'radial' | 'angle' | 'reflected' | 'diamond';

function sampleStops(stops: readonly GradientStop[], t: number): [number, number, number, number] {
  const s = [...stops].sort((a, b) => a.offset - b.offset);
  if (t <= s[0].offset) return [...s[0].color] as [number, number, number, number];
  for (let i = 0; i < s.length - 1; i++) {
    if (t <= s[i + 1].offset) {
      const span = s[i + 1].offset - s[i].offset || 1;
      const f = (t - s[i].offset) / span;
      const a = s[i].color, b = s[i + 1].color;
      return [a[0] + (b[0] - a[0]) * f, a[1] + (b[1] - a[1]) * f, a[2] + (b[2] - a[2]) * f, a[3] + (b[3] - a[3]) * f];
    }
  }
  return [...s[s.length - 1].color] as [number, number, number, number];
}

export function gradientT(kind: GradientKind, x: number, y: number, x0: number, y0: number, x1: number, y1: number): number {
  const dx = x1 - x0, dy = y1 - y0;
  const len2 = dx * dx + dy * dy || 1;
  const px = x - x0, py = y - y0;
  switch (kind) {
    case 'linear':
      return Math.min(1, Math.max(0, (px * dx + py * dy) / len2));
    case 'reflected':
      return Math.min(1, Math.abs((px * dx + py * dy) / len2));
    case 'radial':
      return Math.min(1, Math.sqrt((px * px + py * py) / len2));
    case 'diamond': {
      const len = Math.sqrt(len2);
      const ux = dx / len, uy = dy / len;
      return Math.min(1, (Math.abs(px * ux + py * uy) + Math.abs(-px * uy + py * ux)) / len);
    }
    case 'angle': {
      const a = Math.atan2(py, px) - Math.atan2(dy, dx);
      return (((a / (2 * Math.PI)) % 1) + 1) % 1;
    }
  }
}

/** Composites a gradient over an RGBA region through optional coverage. */
export function gradientRegion(
  dst: Region,
  coverage: Region | null,
  g: { kind: GradientKind; x0: number; y0: number; x1: number; y1: number; stops: readonly GradientStop[]; opacity: number },
): void {
  for (let y = 0; y < dst.height; y++) {
    for (let x = 0; x < dst.width; x++) {
      const p = y * dst.width + x;
      const c = coverage ? coverage.data[p] / 255 : 1;
      if (c <= 0) continue;
      const t = gradientT(g.kind, dst.x + x + 0.5, dst.y + y + 0.5, g.x0, g.y0, g.x1, g.y1);
      const col = sampleStops(g.stops, t);
      overPixel(dst.data, p * 4, Math.round(col[0]), Math.round(col[1]), Math.round(col[2]), (c * g.opacity * col[3]) / 255);
    }
  }
}

/**
 * Pixels similar to the seed color (max channel difference ≤ tolerance, alpha
 * included). Contiguous = 4-connected flood from the seed. Returns a 0/255
 * coverage region the size of `src`.
 */
export function similarRegion(src: Region, seedX: number, seedY: number, tolerance: number, contiguous: boolean): Region {
  const { width: w, height: h, data } = src;
  const out = createRegion(src, 1);
  const sx = seedX - src.x, sy = seedY - src.y;
  if (sx < 0 || sy < 0 || sx >= w || sy >= h) return out;
  const si = (sy * w + sx) * 4;
  const seed = [data[si], data[si + 1], data[si + 2], data[si + 3]];
  const match = (p: number) => {
    const i = p * 4;
    return (
      Math.abs(data[i] - seed[0]) <= tolerance &&
      Math.abs(data[i + 1] - seed[1]) <= tolerance &&
      Math.abs(data[i + 2] - seed[2]) <= tolerance &&
      Math.abs(data[i + 3] - seed[3]) <= tolerance
    );
  };
  if (!contiguous) {
    for (let p = 0; p < w * h; p++) if (match(p)) out.data[p] = 255;
    return out;
  }
  const stack = [sy * w + sx];
  out.data[sy * w + sx] = 255;
  while (stack.length) {
    const p = stack.pop()!;
    const x = p % w, y = (p - x) / w;
    const nb = [x > 0 ? p - 1 : -1, x < w - 1 ? p + 1 : -1, y > 0 ? p - w : -1, y < h - 1 ? p + w : -1];
    for (const q of nb) {
      if (q < 0 || out.data[q]) continue;
      if (match(q)) {
        out.data[q] = 255;
        stack.push(q);
      }
    }
  }
  return out;
}

/** Soft color-range selection: full within fuzziness/2, fading to 0 at fuzziness (max RGB channel distance). */
export function colorRangeRegion(src: Region, color: readonly number[], fuzziness: number): Region {
  const out = createRegion(src, 1);
  const f = Math.max(1, fuzziness);
  for (let p = 0, i = 0; p < out.data.length; p++, i += 4) {
    if (!src.data[i + 3]) continue;
    const d = Math.max(Math.abs(src.data[i] - color[0]), Math.abs(src.data[i + 1] - color[1]), Math.abs(src.data[i + 2] - color[2]));
    out.data[p] = Math.round(Math.min(1, Math.max(0, ((f - d) / f) * 2)) * 255);
  }
  return out;
}

export type { Rect };
