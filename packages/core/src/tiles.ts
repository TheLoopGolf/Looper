/**
 * Tile store primitives.
 *
 * Pixel data is split into fixed 256×256 tiles. A `TileGrid` is an immutable,
 * sparse map from tile coordinates to `Tile`s: missing tiles are fully
 * transparent (or zero for masks). All "writes" return a new grid that shares
 * every untouched tile with the old one (copy-on-write), so history snapshots
 * and render diffing are cheap: a tile changed iff its object identity changed.
 */

export const TILE_SIZE = 256;

export type Channels = 1 | 4;

export interface Tile {
  /** Row-major pixel data, TILE_SIZE × TILE_SIZE × channels. RGBA is straight (non-premultiplied) alpha. */
  readonly data: Uint8ClampedArray;
  readonly channels: Channels;
}

export interface TileGrid {
  /** Pixel extent covered by the grid (normally the document size). */
  readonly width: number;
  readonly height: number;
  readonly channels: Channels;
  /** Sparse storage keyed by `tileKey(tx, ty)`. Treat as read-only. */
  readonly tiles: ReadonlyMap<number, Tile>;
}

/** Packs tile coordinates into one number key (supports up to 65535 tiles per axis). */
export function tileKey(tx: number, ty: number): number {
  return ty * 65536 + tx;
}

export function tileCoords(key: number): [tx: number, ty: number] {
  return [key % 65536, Math.floor(key / 65536)];
}

export function tileCols(width: number): number {
  return Math.ceil(width / TILE_SIZE);
}

export function tileRows(height: number): number {
  return Math.ceil(height / TILE_SIZE);
}

export function createTile(channels: Channels = 4, data?: Uint8ClampedArray): Tile {
  const size = TILE_SIZE * TILE_SIZE * channels;
  if (data && data.length !== size) throw new Error(`Tile data must have ${size} bytes, got ${data.length}`);
  return { data: data ?? new Uint8ClampedArray(size), channels };
}

export function emptyGrid(width: number, height: number, channels: Channels = 4): TileGrid {
  return { width, height, channels, tiles: new Map() };
}

export function getTile(grid: TileGrid, tx: number, ty: number): Tile | undefined {
  return grid.tiles.get(tileKey(tx, ty));
}

/** Returns a new grid with the given tiles replaced (`null` removes a tile). */
export function withTiles(grid: TileGrid, updates: Iterable<[key: number, tile: Tile | null]>): TileGrid {
  const next = new Map(grid.tiles);
  for (const [key, tile] of updates) {
    if (tile) {
      if (tile.channels !== grid.channels) throw new Error('Tile channel count does not match grid');
      next.set(key, tile);
    } else {
      next.delete(key);
    }
  }
  return { ...grid, tiles: next };
}

/** True when every byte of the tile is zero (fully transparent / empty mask). */
export function isTileEmpty(tile: Tile): boolean {
  const d = tile.data;
  if (tile.channels === 4) {
    for (let i = 3; i < d.length; i += 4) if (d[i] !== 0) return false;
    return true;
  }
  for (let i = 0; i < d.length; i++) if (d[i] !== 0) return false;
  return true;
}

export interface Rect {
  x: number;
  y: number;
  width: number;
  height: number;
}

export function intersectRect(a: Rect, b: Rect): Rect | null {
  const x0 = Math.max(a.x, b.x);
  const y0 = Math.max(a.y, b.y);
  const x1 = Math.min(a.x + a.width, b.x + b.width);
  const y1 = Math.min(a.y + a.height, b.y + b.height);
  if (x1 <= x0 || y1 <= y0) return null;
  return { x: x0, y: y0, width: x1 - x0, height: y1 - y0 };
}

/** Keys of all tiles (in a grid of the given size) that intersect `rect`. */
export function tilesInRect(width: number, height: number, rect: Rect): number[] {
  const r = intersectRect(rect, { x: 0, y: 0, width, height });
  if (!r) return [];
  const keys: number[] = [];
  const tx0 = Math.floor(r.x / TILE_SIZE);
  const ty0 = Math.floor(r.y / TILE_SIZE);
  const tx1 = Math.floor((r.x + r.width - 1) / TILE_SIZE);
  const ty1 = Math.floor((r.y + r.height - 1) / TILE_SIZE);
  for (let ty = ty0; ty <= ty1; ty++) for (let tx = tx0; tx <= tx1; tx++) keys.push(tileKey(tx, ty));
  return keys;
}

/** Splits a full RGBA (or single-channel) image into a sparse grid, skipping empty tiles. */
export function gridFromPixels(
  pixels: Uint8ClampedArray | Uint8Array,
  width: number,
  height: number,
  channels: Channels = 4,
): TileGrid {
  if (pixels.length !== width * height * channels) throw new Error('Pixel buffer size mismatch');
  const updates: [number, Tile][] = [];
  const cols = tileCols(width);
  const rows = tileRows(height);
  const rowBytes = TILE_SIZE * channels;
  for (let ty = 0; ty < rows; ty++) {
    for (let tx = 0; tx < cols; tx++) {
      const tile = createTile(channels);
      const x0 = tx * TILE_SIZE;
      const y0 = ty * TILE_SIZE;
      const w = Math.min(TILE_SIZE, width - x0);
      const h = Math.min(TILE_SIZE, height - y0);
      for (let y = 0; y < h; y++) {
        const src = ((y0 + y) * width + x0) * channels;
        tile.data.set(pixels.subarray(src, src + w * channels), y * rowBytes);
      }
      if (!isTileEmpty(tile)) updates.push([tileKey(tx, ty), tile]);
    }
  }
  return withTiles(emptyGrid(width, height, channels), updates);
}

/** Reads a rectangular region out of a grid into a tightly packed buffer. */
export function readRegion(grid: TileGrid, rect: Rect): Uint8ClampedArray {
  const { channels } = grid;
  const out = new Uint8ClampedArray(rect.width * rect.height * channels);
  for (const key of tilesInRect(grid.width, grid.height, rect)) {
    const tile = grid.tiles.get(key);
    if (!tile) continue;
    const [tx, ty] = tileCoords(key);
    const tileRect = { x: tx * TILE_SIZE, y: ty * TILE_SIZE, width: TILE_SIZE, height: TILE_SIZE };
    const r = intersectRect(tileRect, rect)!;
    for (let y = r.y; y < r.y + r.height; y++) {
      const src = ((y - tileRect.y) * TILE_SIZE + (r.x - tileRect.x)) * channels;
      const dst = ((y - rect.y) * rect.width + (r.x - rect.x)) * channels;
      out.set(tile.data.subarray(src, src + r.width * channels), dst);
    }
  }
  return out;
}

/** Returns a new grid where every pixel in `rect` is set to `value` (copy-on-write per touched tile). */
export function fillRect(grid: TileGrid, rect: Rect, value: ArrayLike<number>): TileGrid {
  const { channels } = grid;
  if (value.length !== channels) throw new Error('Fill value must match channel count');
  const updates: [number, Tile | null][] = [];
  for (const key of tilesInRect(grid.width, grid.height, rect)) {
    const [tx, ty] = tileCoords(key);
    const prev = grid.tiles.get(key);
    const tile = createTile(channels, prev ? new Uint8ClampedArray(prev.data) : undefined);
    const tileRect = { x: tx * TILE_SIZE, y: ty * TILE_SIZE, width: TILE_SIZE, height: TILE_SIZE };
    const r = intersectRect(tileRect, rect)!;
    for (let y = r.y; y < r.y + r.height; y++) {
      let i = ((y - tileRect.y) * TILE_SIZE + (r.x - tileRect.x)) * channels;
      for (let x = 0; x < r.width; x++) for (let c = 0; c < channels; c++) tile.data[i++] = value[c];
    }
    updates.push([key, isTileEmpty(tile) ? null : tile]);
  }
  return withTiles(grid, updates);
}

/** Returns a new grid with an RGBA/mask image copied in at (ox, oy), replacing existing pixels. Clipped to the grid. */
export function blitImage(
  grid: TileGrid,
  pixels: Uint8ClampedArray | Uint8Array,
  width: number,
  height: number,
  ox: number,
  oy: number,
): TileGrid {
  const { channels } = grid;
  if (pixels.length !== width * height * channels) throw new Error('Pixel buffer size mismatch');
  const dest = { x: ox, y: oy, width, height };
  const updates: [number, Tile | null][] = [];
  for (const key of tilesInRect(grid.width, grid.height, dest)) {
    const [tx, ty] = tileCoords(key);
    const prev = grid.tiles.get(key);
    const tile = createTile(channels, prev ? new Uint8ClampedArray(prev.data) : undefined);
    const tileRect = { x: tx * TILE_SIZE, y: ty * TILE_SIZE, width: TILE_SIZE, height: TILE_SIZE };
    const r = intersectRect(tileRect, dest)!;
    for (let y = r.y; y < r.y + r.height; y++) {
      const src = ((y - oy) * width + (r.x - ox)) * channels;
      tile.data.set(pixels.subarray(src, src + r.width * channels), ((y - tileRect.y) * TILE_SIZE + (r.x - tileRect.x)) * channels);
    }
    updates.push([key, isTileEmpty(tile) ? null : tile]);
  }
  return withTiles(grid, updates);
}
