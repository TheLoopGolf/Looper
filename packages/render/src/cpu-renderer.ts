import { TILE_SIZE, tileCoords, type Document } from '@canvas-ai/core';
import { executePlanCPU, type Plan } from '@canvas-ai/core';
import { CHECKER_SIZE, deviceRect, TiledRenderer, type RenderBackend } from './renderer';
import type { ViewState } from './view';

type TileCanvas = OffscreenCanvas | HTMLCanvasElement;

function makeCanvas(w: number, h: number): TileCanvas {
  if (typeof OffscreenCanvas !== 'undefined') return new OffscreenCanvas(w, h);
  const c = document.createElement('canvas');
  c.width = w;
  c.height = h;
  return c;
}

const css = (c: [number, number, number]) => `rgb(${c.map((v) => Math.round(v * 255)).join(',')})`;

/** Fallback renderer: CPU compositing, Canvas 2D presentation. */
export class CpuRenderer extends TiledRenderer {
  readonly backend: RenderBackend = 'cpu';
  private ctx: CanvasRenderingContext2D;
  private tiles = new Map<number, TileCanvas>();
  private checker: CanvasPattern | null = null;

  constructor(private canvas: HTMLCanvasElement) {
    super();
    this.stats.backend = 'cpu';
    this.frameBudgetMs = 12;
    const ctx = canvas.getContext('2d', { alpha: false });
    if (!ctx) throw new Error('Canvas 2D is not available');
    this.ctx = ctx;
  }

  protected compositeDisplayTile(key: number, plan: Plan): void {
    const pixels = executePlanCPU(plan, new Uint8ClampedArray(TILE_SIZE * TILE_SIZE * 4));
    let c = this.tiles.get(key);
    if (!c) {
      c = makeCanvas(TILE_SIZE, TILE_SIZE);
      this.tiles.set(key, c);
    }
    (c.getContext('2d') as CanvasRenderingContext2D).putImageData(new ImageData(pixels as Uint8ClampedArray<ArrayBuffer>, TILE_SIZE, TILE_SIZE), 0, 0);
  }

  protected async compositeToBuffer(plan: Plan): Promise<Uint8ClampedArray> {
    return executePlanCPU(plan, new Uint8ClampedArray(TILE_SIZE * TILE_SIZE * 4));
  }

  protected releaseDisplayTile(key: number): void {
    this.tiles.delete(key);
  }

  protected onResize(w: number, h: number): void {
    this.canvas.width = w;
    this.canvas.height = h;
  }

  private checkerPattern(): CanvasPattern {
    if (!this.checker) {
      const c = makeCanvas(CHECKER_SIZE * 2, CHECKER_SIZE * 2);
      const g = c.getContext('2d') as CanvasRenderingContext2D;
      g.fillStyle = css(this.colors.checkerLight);
      g.fillRect(0, 0, CHECKER_SIZE * 2, CHECKER_SIZE * 2);
      g.fillStyle = css(this.colors.checkerDark);
      g.fillRect(CHECKER_SIZE, 0, CHECKER_SIZE, CHECKER_SIZE);
      g.fillRect(0, CHECKER_SIZE, CHECKER_SIZE, CHECKER_SIZE);
      this.checker = this.ctx.createPattern(c as CanvasImageSource, 'repeat')!;
    }
    return this.checker;
  }

  protected draw(doc: Document | null, view: ViewState, keys: number[]): void {
    const { ctx, dpr } = this;
    ctx.setTransform(1, 0, 0, 1, 0, 0);
    ctx.fillStyle = css(this.colors.workspace);
    ctx.fillRect(0, 0, this.canvas.width, this.canvas.height);
    if (!doc) return;
    const r = deviceRect(doc, view, dpr);
    // Checkerboard in screen space so it doesn't scale with zoom.
    ctx.fillStyle = this.checkerPattern();
    ctx.fillRect(r.x, r.y, r.width, r.height);
    ctx.imageSmoothingEnabled = view.zoom < 1;
    ctx.imageSmoothingQuality = 'high';
    ctx.setTransform(r.sx, 0, 0, r.sy, r.x, r.y);
    for (const key of keys) {
      const c = this.tiles.get(key);
      if (!c) continue;
      const [tx, ty] = tileCoords(key);
      const w = Math.min(TILE_SIZE, doc.width - tx * TILE_SIZE);
      const h = Math.min(TILE_SIZE, doc.height - ty * TILE_SIZE);
      ctx.drawImage(c as CanvasImageSource, 0, 0, w, h, tx * TILE_SIZE, ty * TILE_SIZE, w, h);
    }
  }

  dispose(): void {
    this.tiles.clear();
  }
}
