import { documentTilePlan, TILE_SIZE, tileCoords, tilesInRect, type Document, type Plan, type Rect } from '@canvas-ai/core';
import { allTileKeys, diffDocuments } from './dirty';
import { visibleTileKeys, type ViewState } from './view';

export type RenderBackend = 'webgpu' | 'webgl2' | 'cpu';

export interface RenderStats {
  backend: RenderBackend;
  /** CPU time spent in the last `render` call. */
  frameMs: number;
  tilesCompositedLastFrame: number;
  tilesCompositedTotal: number;
  pendingTiles: number;
  cachedLayerTiles: number;
  displayTiles: number;
}

export interface Renderer {
  readonly backend: RenderBackend;
  readonly stats: RenderStats;
  /** Per-frame compositing budget in ms; tiles beyond it are deferred to later frames. */
  frameBudgetMs: number;
  maxTilesPerFrame: number;
  /** Workspace and transparency checkerboard colors. */
  colors: RenderColors;
  /** Updates the document; only tiles whose composite changed are recomposited. */
  setDocument(doc: Document): void;
  /** Resizes the drawing buffer (CSS size × devicePixelRatio). */
  resize(cssWidth: number, cssHeight: number, dpr: number): void;
  /**
   * Composites pending tiles (visible first, within a time budget) and draws
   * the view. Returns true if work remains and another frame should be scheduled.
   */
  render(view: ViewState): boolean;
  /** Flattened straight-alpha RGBA8 pixels of a document region, from this backend (for export and golden tests). */
  readComposite(rect?: Rect): Promise<Uint8ClampedArray>;
  /** Called if the GPU device/context is lost for good; the host should recreate with another backend. */
  onLost?: (reason: string) => void;
  dispose(): void;
}

export interface RenderColors {
  /** Workspace background around the document, RGB 0..1. */
  workspace: [number, number, number];
  checkerLight: [number, number, number];
  checkerDark: [number, number, number];
}

export const DEFAULT_COLORS: RenderColors = {
  workspace: [0.11, 0.11, 0.13],
  checkerLight: [0.8, 0.8, 0.8],
  checkerDark: [0.62, 0.62, 0.62],
};

export const CHECKER_SIZE = 8;

/**
 * Document placement in device pixels, with the outer edges snapped to whole
 * pixels so the edge rows are fully covered (no checkerboard/workspace bleed).
 * Tile positions are `x + docX * sx`.
 */
export function deviceRect(doc: { width: number; height: number }, view: ViewState, dpr: number) {
  const x0 = Math.round(view.panX * dpr);
  const y0 = Math.round(view.panY * dpr);
  const x1 = Math.round((view.panX + doc.width * view.zoom) * dpr);
  const y1 = Math.round((view.panY + doc.height * view.zoom) * dpr);
  return { x: x0, y: y0, width: Math.max(1, x1 - x0), height: Math.max(1, y1 - y0), sx: Math.max(1, x1 - x0) / doc.width, sy: Math.max(1, y1 - y0) / doc.height };
}

/**
 * Backend-independent tile scheduling: dirty tracking, time budgeting and
 * visible-first ordering. Backends implement tile compositing and drawing.
 */
export abstract class TiledRenderer implements Renderer {
  abstract readonly backend: RenderBackend;
  protected doc: Document | null = null;
  protected cssWidth = 1;
  protected cssHeight = 1;
  protected dpr = 1;
  colors: RenderColors = DEFAULT_COLORS;
  onLost?: (reason: string) => void;
  /** Per-frame compositing budget in ms (submission time for GPU backends). */
  frameBudgetMs = 8;
  maxTilesPerFrame = 48;
  private pending = new Set<number>();
  /** Canvas tiles that currently have non-empty cached composites. */
  protected readonly displayKeys = new Set<number>();
  readonly stats: RenderStats;

  constructor() {
    this.stats = {
      backend: 'cpu',
      frameMs: 0,
      tilesCompositedLastFrame: 0,
      tilesCompositedTotal: 0,
      pendingTiles: 0,
      cachedLayerTiles: 0,
      displayTiles: 0,
    };
  }

  setDocument(doc: Document): void {
    const dirty = diffDocuments(this.doc, doc);
    if (dirty.all) {
      for (const key of this.displayKeys) this.releaseDisplayTile(key);
      this.displayKeys.clear();
      this.pending = new Set(allTileKeys(doc));
    } else {
      for (const key of dirty.tiles) this.pending.add(key);
    }
    this.doc = doc;
    this.stats.pendingTiles = this.pending.size;
  }

  /** Marks everything for recomposite (e.g. after a lost GPU context). */
  protected invalidateAll(): void {
    this.displayKeys.clear();
    if (this.doc) this.pending = new Set(allTileKeys(this.doc));
  }

  resize(cssWidth: number, cssHeight: number, dpr: number): void {
    this.cssWidth = Math.max(1, cssWidth);
    this.cssHeight = Math.max(1, cssHeight);
    this.dpr = dpr;
    this.onResize(Math.round(this.cssWidth * dpr), Math.round(this.cssHeight * dpr));
  }

  render(view: ViewState): boolean {
    const start = performance.now();
    const doc = this.doc;
    let composited = 0;
    if (doc) {
      const visible = visibleTileKeys(doc, view, this.cssWidth, this.cssHeight);
      const order = [...visible.filter((k) => this.pending.has(k)), ...this.pending];
      for (const key of order) {
        if (!this.pending.has(key)) continue;
        if (composited > 0 && (performance.now() - start > this.frameBudgetMs || composited >= this.maxTilesPerFrame)) break;
        this.pending.delete(key);
        const plan = documentTilePlan(doc, key);
        if (plan.length === 0) {
          if (this.displayKeys.delete(key)) this.releaseDisplayTile(key);
        } else {
          this.compositeDisplayTile(key, plan);
          this.displayKeys.add(key);
        }
        composited++;
      }
      this.draw(doc, view, visible.filter((k) => this.displayKeys.has(k)));
    } else {
      this.draw(null, view, []);
    }
    this.stats.frameMs = performance.now() - start;
    this.stats.tilesCompositedLastFrame = composited;
    this.stats.tilesCompositedTotal += composited;
    this.stats.pendingTiles = this.pending.size;
    this.stats.displayTiles = this.displayKeys.size;
    this.afterFrame();
    return this.pending.size > 0;
  }

  async readComposite(rect?: Rect): Promise<Uint8ClampedArray> {
    const doc = this.doc;
    if (!doc) throw new Error('No document');
    const r = rect ?? { x: 0, y: 0, width: doc.width, height: doc.height };
    const out = new Uint8ClampedArray(r.width * r.height * 4);
    for (const key of tilesInRect(doc.width, doc.height, r)) {
      const plan = documentTilePlan(doc, key);
      if (plan.length === 0) continue;
      const tile = await this.compositeToBuffer(plan);
      const [tx, ty] = tileCoords(key);
      const x0 = Math.max(r.x, tx * TILE_SIZE);
      const x1 = Math.min(r.x + r.width, (tx + 1) * TILE_SIZE);
      const y0 = Math.max(r.y, ty * TILE_SIZE);
      const y1 = Math.min(r.y + r.height, (ty + 1) * TILE_SIZE);
      for (let y = y0; y < y1; y++) {
        const src = ((y - ty * TILE_SIZE) * TILE_SIZE + (x0 - tx * TILE_SIZE)) * 4;
        out.set(tile.subarray(src, src + (x1 - x0) * 4), ((y - r.y) * r.width + (x0 - r.x)) * 4);
      }
    }
    return out;
  }

  /** Executes a plan and caches the result as the display tile for `key`. */
  protected abstract compositeDisplayTile(key: number, plan: Plan): void;
  /** Executes a plan and returns the straight RGBA8 result without caching. */
  protected abstract compositeToBuffer(plan: Plan): Promise<Uint8ClampedArray>;
  protected abstract releaseDisplayTile(key: number): void;
  /** Draws workspace, checkerboard and the given cached display tiles. */
  protected abstract draw(doc: Document | null, view: ViewState, keys: number[]): void;
  protected abstract onResize(pixelWidth: number, pixelHeight: number): void;
  protected afterFrame(): void {}
  abstract dispose(): void;
}
