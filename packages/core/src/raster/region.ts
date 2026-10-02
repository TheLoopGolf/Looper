/**
 * Region-level raster helpers shared by selections, masks, paint and filters.
 * Everything here is pure and deterministic (no Math.random, no platform APIs).
 */
import {
  createTile,
  emptyGrid,
  intersectRect,
  isTileEmpty,
  tileCoords,
  TILE_SIZE,
  tilesInRect,
  withTiles,
  type Channels,
  type Rect,
  type Tile,
  type TileGrid,
} from '../tiles';

/** A rectangular buffer positioned in document space. */
export interface Region {
  readonly x: number;
  readonly y: number;
  readonly width: number;
  readonly height: number;
  readonly channels: Channels;
  readonly data: Uint8ClampedArray;
}

export function createRegion(rect: Rect, channels: Channels, fill = 0): Region {
  const data = new Uint8ClampedArray(rect.width * rect.height * channels);
  if (fill) data.fill(fill);
  return { ...rect, channels, data };
}

/** Reads a rect from a grid; missing tiles read as `fill` (e.g. 255 for reveal-all masks). */
export function readGrid(grid: TileGrid, rect: Rect, fill = 0): Region {
  const { channels } = grid;
  const out = createRegion(rect, channels, fill);
  for (const key of tilesInRect(grid.width, grid.height, rect)) {
    const tile = grid.tiles.get(key);
    if (!tile) continue;
    const [tx, ty] = tileCoords(key);
    const tr = { x: tx * TILE_SIZE, y: ty * TILE_SIZE, width: TILE_SIZE, height: TILE_SIZE };
    const r = intersectRect(tr, rect)!;
    for (let y = r.y; y < r.y + r.height; y++) {
      const src = ((y - tr.y) * TILE_SIZE + (r.x - tr.x)) * channels;
      out.data.set(tile.data.subarray(src, src + r.width * channels), ((y - rect.y) * rect.width + (r.x - rect.x)) * channels);
    }
  }
  return out;
}

/**
 * Writes a region into a grid (copy-on-write per touched tile). Tiles that end
 * up equal to `emptyValue` everywhere are dropped (sparse storage).
 */
export function writeGrid(grid: TileGrid, region: Region, emptyValue = 0): TileGrid {
  const { channels } = grid;
  if (region.channels !== channels) throw new Error('Region/grid channel mismatch');
  const bounds = intersectRect(region, { x: 0, y: 0, width: grid.width, height: grid.height });
  if (!bounds) return grid;
  const updates: [number, Tile | null][] = [];
  for (const key of tilesInRect(grid.width, grid.height, bounds)) {
    const [tx, ty] = tileCoords(key);
    const prev = grid.tiles.get(key);
    const data = prev ? new Uint8ClampedArray(prev.data) : new Uint8ClampedArray(TILE_SIZE * TILE_SIZE * channels);
    if (!prev && emptyValue) data.fill(emptyValue);
    const tr = { x: tx * TILE_SIZE, y: ty * TILE_SIZE, width: TILE_SIZE, height: TILE_SIZE };
    const r = intersectRect(tr, bounds)!;
    for (let y = r.y; y < r.y + r.height; y++) {
      const src = ((y - region.y) * region.width + (r.x - region.x)) * channels;
      data.set(region.data.subarray(src, src + r.width * channels), ((y - tr.y) * TILE_SIZE + (r.x - tr.x)) * channels);
    }
    updates.push([key, isUniform(data, channels, emptyValue) ? null : createTile(channels, data)]);
  }
  return withTiles(grid, updates);
}

function isUniform(data: Uint8ClampedArray, channels: Channels, value: number): boolean {
  if (channels === 4 && value === 0) {
    for (let i = 3; i < data.length; i += 4) if (data[i] !== 0) return false;
    return true;
  }
  for (let i = 0; i < data.length; i++) if (data[i] !== value) return false;
  return true;
}

/** Bounding rect of a grid's stored tiles (tile-granular), clipped to the grid. */
export function gridBounds(grid: TileGrid): Rect | null {
  let x0 = Infinity, y0 = Infinity, x1 = -Infinity, y1 = -Infinity;
  for (const key of grid.tiles.keys()) {
    const [tx, ty] = tileCoords(key);
    x0 = Math.min(x0, tx * TILE_SIZE);
    y0 = Math.min(y0, ty * TILE_SIZE);
    x1 = Math.max(x1, (tx + 1) * TILE_SIZE);
    y1 = Math.max(y1, (ty + 1) * TILE_SIZE);
  }
  if (x0 === Infinity) return null;
  return intersectRect({ x: x0, y: y0, width: x1 - x0, height: y1 - y0 }, { x: 0, y: 0, width: grid.width, height: grid.height });
}

/** Tight pixel bounds of non-zero coverage (alpha for RGBA) in a region. */
export function tightBounds(region: Region): Rect | null {
  const { width, height, channels, data } = region;
  let x0 = width, y0 = height, x1 = -1, y1 = -1;
  const off = channels === 4 ? 3 : 0;
  for (let y = 0; y < height; y++) {
    for (let x = 0; x < width; x++) {
      if (data[(y * width + x) * channels + off]) {
        if (x < x0) x0 = x;
        if (x > x1) x1 = x;
        if (y < y0) y0 = y;
        if (y > y1) y1 = y;
      }
    }
  }
  if (x1 < 0) return null;
  return { x: region.x + x0, y: region.y + y0, width: x1 - x0 + 1, height: y1 - y0 + 1 };
}

export function padRect(r: Rect, pad: number, clip?: Rect): Rect | null {
  const p = { x: r.x - pad, y: r.y - pad, width: r.width + 2 * pad, height: r.height + 2 * pad };
  return clip ? intersectRect(p, clip) : p;
}

// ---------------------------------------------------------------------------
// Polygon rasterization (anti-aliased, analytic horizontal coverage, 5 vertical samples)
// ---------------------------------------------------------------------------

/** Closed polygon as a flat [x0, y0, x1, y1, …] list. */
export type Path = readonly number[];
export type FillRule = 'nonzero' | 'evenodd';

const SUB = 5;

/** Rasterizes closed paths into a 1-channel coverage region clipped to `clip`. Returns null if nothing is covered. */
export function rasterizePaths(paths: readonly Path[], clip: Rect, rule: FillRule = 'nonzero', antiAlias = true): Region | null {
  let minX = Infinity, minY = Infinity, maxX = -Infinity, maxY = -Infinity;
  const edges: { x0: number; y0: number; x1: number; y1: number; dir: number }[] = [];
  for (const p of paths) {
    const n = p.length / 2;
    if (n < 3) continue;
    for (let i = 0; i < n; i++) {
      const ax = p[i * 2], ay = p[i * 2 + 1];
      const bx = p[((i + 1) % n) * 2], by = p[((i + 1) % n) * 2 + 1];
      minX = Math.min(minX, ax); maxX = Math.max(maxX, ax);
      minY = Math.min(minY, ay); maxY = Math.max(maxY, ay);
      if (ay === by) continue;
      edges.push(ay < by ? { x0: ax, y0: ay, x1: bx, y1: by, dir: 1 } : { x0: bx, y0: by, x1: ax, y1: ay, dir: -1 });
    }
  }
  if (!edges.length) return null;
  const box = intersectRect(
    { x: Math.floor(minX), y: Math.floor(minY), width: Math.ceil(maxX) - Math.floor(minX) + 1, height: Math.ceil(maxY) - Math.floor(minY) + 1 },
    clip,
  );
  if (!box) return null;
  edges.sort((a, b) => a.y0 - b.y0);
  const out = createRegion(box, 1);
  const acc = new Float32Array(box.width + 1);
  const xs: number[] = [];
  const ds: number[] = [];
  let active: typeof edges = [];
  let next = 0;
  for (let row = 0; row < box.height; row++) {
    acc.fill(0);
    for (let s = 0; s < SUB; s++) {
      const sy = box.y + row + (s + 0.5) / SUB;
      while (next < edges.length && edges[next].y0 <= sy) active.push(edges[next++]);
      active = active.filter((e) => e.y1 > sy);
      xs.length = 0;
      ds.length = 0;
      for (const e of active) {
        if (e.y0 > sy) continue;
        xs.push(e.x0 + ((sy - e.y0) * (e.x1 - e.x0)) / (e.y1 - e.y0));
        ds.push(e.dir);
      }
      const order = xs.map((_, i) => i).sort((a, b) => xs[a] - xs[b]);
      let wind = 0;
      for (let k = 0; k < order.length - 1; k++) {
        wind += ds[order[k]];
        const inside = rule === 'nonzero' ? wind !== 0 : (wind & 1) !== 0;
        if (!inside) continue;
        const xa = Math.max(xs[order[k]] - box.x, 0);
        const xb = Math.min(xs[order[k + 1]] - box.x, box.width);
        if (xb <= xa) continue;
        const ia = Math.floor(xa), ib = Math.floor(xb);
        if (ia === ib) acc[ia] += xb - xa;
        else {
          acc[ia] += ia + 1 - xa;
          for (let i = ia + 1; i < ib; i++) acc[i] += 1;
          acc[ib] += xb - ib;
        }
      }
    }
    const o = row * box.width;
    for (let i = 0; i < box.width; i++) {
      const c = Math.min(1, acc[i] / SUB);
      out.data[o + i] = antiAlias ? Math.round(c * 255) : c >= 0.5 ? 255 : 0;
    }
  }
  return out;
}

/** Orients a path counter-clockwise (positive area in y-down space) so nonzero unions never cancel. */
export function orient(path: Path): Path {
  let area = 0;
  const n = path.length / 2;
  for (let i = 0; i < n; i++) {
    const j = (i + 1) % n;
    area += path[i * 2] * path[j * 2 + 1] - path[j * 2] * path[i * 2 + 1];
  }
  if (area >= 0) return path;
  const out: number[] = [];
  for (let i = n - 1; i >= 0; i--) out.push(path[i * 2], path[i * 2 + 1]);
  return out;
}

export function rectPath(x: number, y: number, w: number, h: number, radius = 0): Path {
  const r = Math.max(0, Math.min(radius, Math.abs(w) / 2, Math.abs(h) / 2));
  if (r <= 0) return [x, y, x + w, y, x + w, y + h, x, y + h];
  const pts: number[] = [];
  const corner = (cx: number, cy: number, start: number) => {
    const steps = Math.max(4, Math.ceil(r / 2));
    for (let i = 0; i <= steps; i++) {
      const a = start + (i / steps) * (Math.PI / 2);
      pts.push(cx + Math.cos(a) * r, cy + Math.sin(a) * r);
    }
  };
  corner(x + w - r, y + r, -Math.PI / 2);
  corner(x + w - r, y + h - r, 0);
  corner(x + r, y + h - r, Math.PI / 2);
  corner(x + r, y + r, Math.PI);
  return pts;
}

export function ellipsePath(cx: number, cy: number, rx: number, ry: number): Path {
  const perimeter = Math.PI * (Math.abs(rx) + Math.abs(ry));
  const n = Math.min(4096, Math.max(24, Math.ceil(perimeter / 2)));
  const pts: number[] = [];
  for (let i = 0; i < n; i++) {
    const a = (i / n) * Math.PI * 2;
    pts.push(cx + Math.cos(a) * rx, cy + Math.sin(a) * ry);
  }
  return pts;
}

export function polygonPath(cx: number, cy: number, radius: number, sides: number, rotationDeg = 0, star = 1): Path {
  const pts: number[] = [];
  const n = Math.max(3, Math.round(sides));
  const rot = (rotationDeg * Math.PI) / 180 - Math.PI / 2;
  const count = star < 1 ? n * 2 : n;
  for (let i = 0; i < count; i++) {
    const a = rot + (i / count) * Math.PI * 2;
    const r = star < 1 && i % 2 === 1 ? radius * star : radius;
    pts.push(cx + Math.cos(a) * r, cy + Math.sin(a) * r);
  }
  return pts;
}

/** Outline of a polyline/polygon of the given width, as consistently oriented quads + round joins (union with nonzero). */
export function strokePaths(points: Path, closed: boolean, width: number): Path[] {
  const out: Path[] = [];
  const n = points.length / 2;
  if (n < 2 || width <= 0) return out;
  const hw = width / 2;
  const segs = closed ? n : n - 1;
  for (let i = 0; i < segs; i++) {
    const ax = points[i * 2], ay = points[i * 2 + 1];
    const bx = points[((i + 1) % n) * 2], by = points[((i + 1) % n) * 2 + 1];
    const len = Math.hypot(bx - ax, by - ay);
    if (len === 0) continue;
    const nx = (-(by - ay) / len) * hw, ny = ((bx - ax) / len) * hw;
    out.push(orient([ax + nx, ay + ny, bx + nx, by + ny, bx - nx, by - ny, ax - nx, ay - ny]));
  }
  // Round joins (and caps for open paths) keep corners closed.
  if (hw >= 1) for (let i = 0; i < n; i++) out.push(orient(ellipsePath(points[i * 2], points[i * 2 + 1], hw, hw)));
  return out;
}

// ---------------------------------------------------------------------------
// Blur (3-pass box ≈ gaussian), generic over channel count
// ---------------------------------------------------------------------------

/** Box sizes whose triple convolution approximates a gaussian of `sigma` (Kovesi). */
export function boxesForGauss(sigma: number, n = 3): number[] {
  const wIdeal = Math.sqrt((12 * sigma * sigma) / n + 1);
  let wl = Math.floor(wIdeal);
  if (wl % 2 === 0) wl--;
  const wu = wl + 2;
  const mIdeal = (12 * sigma * sigma - n * wl * wl - 4 * n * wl - 3 * n) / (-4 * wl - 4);
  const m = Math.round(mIdeal);
  return Array.from({ length: n }, (_, i) => (i < m ? wl : wu));
}

function boxPass(src: Float32Array, dst: Float32Array, w: number, h: number, ch: number, r: number, horizontal: boolean): void {
  const len = horizontal ? w : h;
  const lines = horizontal ? h : w;
  const stride = horizontal ? ch : w * ch;
  const lineStride = horizontal ? w * ch : ch;
  const norm = 1 / (2 * r + 1);
  for (let l = 0; l < lines; l++) {
    const base = l * lineStride;
    for (let c = 0; c < ch; c++) {
      const at = (i: number) => src[base + Math.min(len - 1, Math.max(0, i)) * stride + c];
      let sum = 0;
      for (let i = -r; i <= r; i++) sum += at(i);
      for (let i = 0; i < len; i++) {
        dst[base + i * stride + c] = sum * norm;
        sum += at(i + r + 1) - at(i - r);
      }
    }
  }
}

/** Gaussian-like blur of float data in place (edges clamp). */
export function blurFloat(data: Float32Array, w: number, h: number, ch: number, sigma: number): void {
  if (sigma <= 0) return;
  const tmp = new Float32Array(data.length);
  for (const size of boxesForGauss(sigma)) {
    const r = (size - 1) / 2;
    boxPass(data, tmp, w, h, ch, r, true);
    boxPass(tmp, data, w, h, ch, r, false);
  }
}

/** Gaussian blur of a 1-channel coverage region; the region must already include padding. */
export function blurCoverage(region: Region, sigma: number): Region {
  const f = Float32Array.from(region.data);
  blurFloat(f, region.width, region.height, 1, sigma);
  const data = new Uint8ClampedArray(f.length);
  for (let i = 0; i < f.length; i++) data[i] = Math.round(f[i]);
  return { ...region, data };
}

// ---------------------------------------------------------------------------
// Morphology via exact Euclidean distance transform (Felzenszwalb & Huttenlocher)
// ---------------------------------------------------------------------------

function edt1d(f: Float64Array, n: number, d: Float64Array, v: Int32Array, z: Float64Array): void {
  let k = 0;
  v[0] = 0;
  z[0] = -Infinity;
  z[1] = Infinity;
  for (let q = 1; q < n; q++) {
    let s = (f[q] + q * q - (f[v[k]] + v[k] * v[k])) / (2 * q - 2 * v[k]);
    while (s <= z[k]) {
      k--;
      s = (f[q] + q * q - (f[v[k]] + v[k] * v[k])) / (2 * q - 2 * v[k]);
    }
    k++;
    v[k] = q;
    z[k] = s;
    z[k + 1] = Infinity;
  }
  k = 0;
  for (let q = 0; q < n; q++) {
    while (z[k + 1] < q) k++;
    d[q] = (q - v[k]) * (q - v[k]) + f[v[k]];
  }
}

/** Squared distance from each pixel to the nearest pixel where `inside(i)` is true. */
export function squaredDistance(w: number, h: number, inside: (i: number) => boolean): Float64Array {
  const INF = 1e20;
  const grid = new Float64Array(w * h);
  for (let i = 0; i < w * h; i++) grid[i] = inside(i) ? 0 : INF;
  const n = Math.max(w, h);
  const f = new Float64Array(n), d = new Float64Array(n), z = new Float64Array(n + 1);
  const v = new Int32Array(n);
  for (let x = 0; x < w; x++) {
    for (let y = 0; y < h; y++) f[y] = grid[y * w + x];
    edt1d(f, h, d, v, z);
    for (let y = 0; y < h; y++) grid[y * w + x] = d[y];
  }
  for (let y = 0; y < h; y++) {
    for (let x = 0; x < w; x++) f[x] = grid[y * w + x];
    edt1d(f, w, d, v, z);
    for (let x = 0; x < w; x++) grid[y * w + x] = d[x];
  }
  return grid;
}

/** Grows (r > 0) or shrinks (r < 0) a coverage region by |r| pixels with circular structuring element. */
export function morph(region: Region, r: number): Region {
  const { width: w, height: h, data: src } = region;
  const data = new Uint8ClampedArray(src.length);
  if (r > 0) {
    const dist = squaredDistance(w, h, (i) => src[i] >= 128);
    for (let i = 0; i < data.length; i++) data[i] = dist[i] <= r * r ? 255 : 0;
  } else {
    const rr = -r;
    const dist = squaredDistance(w, h, (i) => src[i] < 128);
    for (let i = 0; i < data.length; i++) data[i] = dist[i] > rr * rr ? 255 : 0;
  }
  return { ...region, data };
}

// ---------------------------------------------------------------------------
// Grid-level coverage combination
// ---------------------------------------------------------------------------

export type CombineMode = 'replace' | 'add' | 'subtract' | 'intersect';

/** Shared, immutable fully-on mask tile (cheap "select all"). */
export const FULL_MASK_TILE: Tile = createTile(1, new Uint8ClampedArray(TILE_SIZE * TILE_SIZE).fill(255));

export function fullMaskGrid(width: number, height: number): TileGrid {
  const keys = tilesInRect(width, height, { x: 0, y: 0, width, height });
  return withTiles(emptyGrid(width, height, 1), keys.map((k) => [k, FULL_MASK_TILE]));
}

export function combineMasks(a: TileGrid | null, b: TileGrid, mode: CombineMode): TileGrid {
  if (mode === 'replace' || !a) {
    if (mode === 'subtract' || mode === 'intersect') return emptyGrid(b.width, b.height, 1);
    return b;
  }
  const keys = new Set([...a.tiles.keys(), ...b.tiles.keys()]);
  const updates: [number, Tile | null][] = [];
  for (const key of keys) {
    const ta = a.tiles.get(key);
    const tb = b.tiles.get(key);
    let out: Tile | null;
    if (mode === 'add') {
      if (!ta || !tb) out = ta ?? tb ?? null;
      else out = mapTile(ta, tb, (x, y) => Math.max(x, y));
    } else if (mode === 'subtract') {
      if (!ta) out = null;
      else if (!tb) out = ta;
      else out = mapTile(ta, tb, (x, y) => Math.round((x * (255 - y)) / 255));
    } else {
      out = ta && tb ? mapTile(ta, tb, (x, y) => Math.min(x, y)) : null;
    }
    updates.push([key, out && !isTileEmpty(out) ? out : null]);
  }
  return withTiles(emptyGrid(b.width, b.height, 1), updates);
}

function mapTile(a: Tile, b: Tile, f: (x: number, y: number) => number): Tile {
  const data = new Uint8ClampedArray(a.data.length);
  for (let i = 0; i < data.length; i++) data[i] = f(a.data[i], b.data[i]);
  return createTile(1, data);
}

export function invertMask(grid: TileGrid | null, width: number, height: number): TileGrid {
  const keys = tilesInRect(width, height, { x: 0, y: 0, width, height });
  const updates: [number, Tile | null][] = keys.map((key) => {
    const t = grid?.tiles.get(key);
    if (!t) return [key, FULL_MASK_TILE];
    const data = new Uint8ClampedArray(t.data.length);
    for (let i = 0; i < data.length; i++) data[i] = 255 - t.data[i];
    const tile = createTile(1, data);
    return [key, isTileEmpty(tile) ? null : tile];
  });
  return withTiles(emptyGrid(width, height, 1), updates);
}

/** Coverage region → document-sized 1-channel grid. */
export function regionToMask(region: Region | null, width: number, height: number): TileGrid {
  const grid = emptyGrid(width, height, 1);
  return region ? writeGrid(grid, region) : grid;
}
