import { strFromU8, strToU8, unzipSync, zipSync, type Zippable } from 'fflate';
import type { Document } from '../document';
import type { Actor, HistoryEntry } from '../history';
import { createTile, tileCoords, tileKey, TILE_SIZE, type Tile, type TileGrid } from '../tiles';
import { decodePng, encodePng } from './png';

/**
 * `.cnva` native project format: a zip containing
 *   document.json          document tree; every TileGrid replaced by {"$grid": n, ...}
 *   tiles/<n>/<tx>_<ty>.png  lossless tile data (RGBA or grayscale)
 *   thumbnail.png          optional preview (≤ 256px)
 *   history.json           history metadata (titles/actors; pixel history is not persisted)
 */

export const CNVA_FORMAT_VERSION = 1;
export const CNVA_MIME = 'application/x-canvas-ai';

interface GridRef {
  $grid: number;
  width: number;
  height: number;
  channels: 1 | 4;
  tiles: [tx: number, ty: number][];
}

export interface HistoryMetadata {
  readonly cursor: number;
  readonly entries: readonly { title: string; commandId: string; actor: Actor; timestamp: number; children?: number }[];
}

export interface CnvaContents {
  document: Document;
  thumbnail?: { width: number; height: number; pixels: Uint8ClampedArray };
  history?: HistoryMetadata;
}

function isTileGrid(value: unknown): value is TileGrid {
  return typeof value === 'object' && value !== null && (value as TileGrid).tiles instanceof Map;
}

export function historyMetadata(entries: readonly HistoryEntry[], cursor: number): HistoryMetadata {
  return {
    cursor,
    entries: entries.map((e) => ({
      title: e.title,
      commandId: e.commandId,
      actor: e.actor,
      timestamp: e.timestamp,
      ...(e.children ? { children: e.children.length } : {}),
    })),
  };
}

const yieldToEventLoop = () => new Promise<void>((r) => setTimeout(r, 0));

/** Serializes a document. Yields to the event loop periodically so the UI stays responsive. */
export async function writeCnva(contents: CnvaContents): Promise<Uint8Array> {
  const files: Zippable = {};
  const grids: TileGrid[] = [];
  const gridIndex = new Map<TileGrid, number>();

  const json = JSON.stringify(
    { format: 'cnva', version: CNVA_FORMAT_VERSION, document: contents.document },
    (_key, value) => {
      if (!isTileGrid(value)) return value;
      let n = gridIndex.get(value);
      if (n === undefined) {
        n = grids.length;
        grids.push(value);
        gridIndex.set(value, n);
      }
      const ref: GridRef = {
        $grid: n,
        width: value.width,
        height: value.height,
        channels: value.channels,
        tiles: [...value.tiles.keys()].sort((a, b) => a - b).map(tileCoords),
      };
      return ref;
    },
    1,
  );
  files['document.json'] = strToU8(json);

  let encoded = 0;
  for (const [n, grid] of grids.entries()) {
    for (const [key, tile] of grid.tiles) {
      const [tx, ty] = tileCoords(key);
      // PNG data is already deflated; store without recompressing.
      files[`tiles/${n}/${tx}_${ty}.png`] = [encodePng(tile.data, TILE_SIZE, TILE_SIZE, tile.channels), { level: 0 }];
      if (++encoded % 16 === 0) await yieldToEventLoop();
    }
  }
  if (contents.thumbnail) {
    const t = contents.thumbnail;
    files['thumbnail.png'] = [encodePng(t.pixels, t.width, t.height, 4), { level: 0 }];
  }
  if (contents.history) files['history.json'] = strToU8(JSON.stringify(contents.history));
  return zipSync(files, { level: 6 });
}

export function readCnva(bytes: Uint8Array): CnvaContents {
  let entries: Record<string, Uint8Array>;
  try {
    entries = unzipSync(bytes);
  } catch {
    throw new Error('This file is not a valid Canvas AI project (.cnva).');
  }
  const docJson = entries['document.json'];
  if (!docJson) throw new Error('Project file is missing document.json');

  const gridCache = new Map<number, TileGrid>();
  const parsed = JSON.parse(strFromU8(docJson), (_key, value) => {
    if (typeof value !== 'object' || value === null || typeof (value as GridRef).$grid !== 'number') return value;
    const ref = value as GridRef;
    const cached = gridCache.get(ref.$grid);
    if (cached) return cached;
    const tiles = new Map<number, Tile>();
    for (const [tx, ty] of ref.tiles) {
      const file = entries[`tiles/${ref.$grid}/${tx}_${ty}.png`];
      if (!file) throw new Error(`Project file is missing tile ${tx},${ty} of grid ${ref.$grid}`);
      const img = decodePng(file);
      if (img.width !== TILE_SIZE || img.height !== TILE_SIZE || img.channels !== ref.channels) {
        throw new Error(`Tile ${tx},${ty} of grid ${ref.$grid} has an unexpected format`);
      }
      tiles.set(tileKey(tx, ty), createTile(ref.channels, img.pixels));
    }
    const grid: TileGrid = { width: ref.width, height: ref.height, channels: ref.channels, tiles };
    gridCache.set(ref.$grid, grid);
    return grid;
  });
  if (parsed.format !== 'cnva') throw new Error('Not a Canvas AI project');
  if (parsed.version > CNVA_FORMAT_VERSION) {
    throw new Error(`This project was saved by a newer version of Canvas AI (format ${parsed.version}).`);
  }

  const result: CnvaContents = { document: parsed.document as Document };
  const thumb = entries['thumbnail.png'];
  if (thumb) {
    const img = decodePng(thumb);
    if (img.channels === 4) result.thumbnail = { width: img.width, height: img.height, pixels: img.pixels };
  }
  const history = entries['history.json'];
  if (history) result.history = JSON.parse(strFromU8(history)) as HistoryMetadata;
  return result;
}
