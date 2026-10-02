import { raster, tileCols, tileKey, tileRows, type Document, type LayerNode } from '@canvas-ai/core';

/** Canvas tiles whose composite may have changed between two document versions. */
export interface DirtyTiles {
  readonly all: boolean;
  readonly tiles: ReadonlySet<number>;
}

/** Tiles whose composite this layer can affect. Adjustment layers affect everything below them. */
function addOccupied(layer: LayerNode, out: Set<number>, doc: Document): void {
  if (layer.type === 'group') {
    for (const c of layer.children) addOccupied(c, out, doc);
  } else if (layer.type === 'adjustment') {
    for (const k of allTileKeys(doc)) out.add(k);
  } else {
    const grid = raster.layerPixels(layer, doc.width, doc.height);
    if (grid) for (const k of grid.tiles.keys()) out.add(k);
  }
}

/** Every render-relevant property except content (tiles/children), which is diffed separately. */
function sameProps(a: LayerNode, b: LayerNode): boolean {
  // Text and shape content is part of their props (their pixels are derived).
  if (a.type !== b.type) return false;
  for (const key of Object.keys(a) as (keyof LayerNode)[]) {
    if (key === 'name' || key === 'locked' || key === ('tiles' as keyof LayerNode) || key === ('children' as keyof LayerNode)) continue;
    if (key === ('collapsed' as keyof LayerNode)) continue;
    if (a[key] !== b[key]) return false;
  }
  return true;
}

function flatten(layers: readonly LayerNode[], out: Map<string, { layer: LayerNode; order: number; parent: string | null }>, parent: string | null): void {
  for (const layer of layers) {
    out.set(layer.id, { layer, order: out.size, parent });
    if (layer.type === 'group') flatten(layer.children, out, layer.id);
  }
}

export function diffDocuments(prev: Document | null, next: Document): DirtyTiles {
  if (!prev || prev.width !== next.width || prev.height !== next.height) return { all: true, tiles: new Set() };
  if (prev.layers === next.layers) return { all: false, tiles: new Set() };
  const dirty = new Set<number>();
  const a = new Map<string, { layer: LayerNode; order: number; parent: string | null }>();
  const b = new Map<string, { layer: LayerNode; order: number; parent: string | null }>();
  flatten(prev.layers, a, null);
  flatten(next.layers, b, null);

  // Relative order of surviving layers: a layer whose position among survivors changed dirties its own content.
  const rankA = new Map([...a.keys()].filter((id) => b.has(id)).map((id, i) => [id, i]));
  const rankB = new Map([...b.keys()].filter((id) => a.has(id)).map((id, i) => [id, i]));

  for (const [id, pa] of a) {
    const pb = b.get(id);
    if (!pb) {
      addOccupied(pa.layer, dirty, prev); // removed
      continue;
    }
    const moved = pa.parent !== pb.parent || rankB.get(id) !== rankA.get(id);
    const la = pa.layer;
    const lb = pb.layer;
    if (moved || !sameProps(la, lb)) {
      addOccupied(la, dirty, prev);
      addOccupied(lb, dirty, next);
      continue;
    }
    if (la === lb || la.type === 'group') continue; // group children are diffed individually
    const ta = 'tiles' in la ? la.tiles : null;
    const tb = 'tiles' in lb ? lb.tiles : null;
    if (ta && tb && ta !== tb) {
      for (const [k, t] of ta.tiles) if (tb.tiles.get(k) !== t) dirty.add(k);
      for (const k of tb.tiles.keys()) if (!ta.tiles.has(k)) dirty.add(k);
    }
  }
  for (const [id, pb] of b) if (!a.has(id)) addOccupied(pb.layer, dirty, next); // added
  return { all: false, tiles: dirty };
}

export function allTileKeys(doc: Pick<Document, 'width' | 'height'>): number[] {
  const keys: number[] = [];
  for (let ty = 0; ty < tileRows(doc.height); ty++) for (let tx = 0; tx < tileCols(doc.width); tx++) keys.push(tileKey(tx, ty));
  return keys;
}
