import type { RGBA, ShapeGeometry, ShapeLayer, TextLayer } from '../document';
import { emptyGrid, type TileGrid } from '../tiles';
import { createRegion, ellipsePath, polygonPath, rasterizePaths, rectPath, strokePaths, writeGrid, type Path, type Region } from './region';

/** Outline of a shape as a closed (or open, for lines/open paths) point list. */
export function shapeOutline(shape: ShapeGeometry): { points: Path; closed: boolean } {
  switch (shape.kind) {
    case 'rect': {
      const x = Math.min(shape.x, shape.x + shape.width);
      const y = Math.min(shape.y, shape.y + shape.height);
      return { points: rectPath(x, y, Math.abs(shape.width), Math.abs(shape.height), shape.radius), closed: true };
    }
    case 'ellipse':
      return { points: ellipsePath(shape.cx, shape.cy, Math.abs(shape.rx), Math.abs(shape.ry)), closed: true };
    case 'polygon':
      return { points: polygonPath(shape.cx, shape.cy, shape.radius, shape.sides, shape.rotation, shape.star ?? 1), closed: true };
    case 'line':
      return { points: [shape.x1, shape.y1, shape.x2, shape.y2], closed: false };
    case 'path':
      return { points: shape.points, closed: shape.closed };
  }
}

/** Composites `color` with per-pixel coverage over `dst` (normal mode, straight alpha). */
export function paintCoverage(dst: Region, cov: Region, color: RGBA): void {
  const ca = color[3] / 255;
  for (let y = 0; y < cov.height; y++) {
    for (let x = 0; x < cov.width; x++) {
      const c = cov.data[y * cov.width + x];
      if (!c) continue;
      const dx = cov.x + x - dst.x, dy = cov.y + y - dst.y;
      if (dx < 0 || dy < 0 || dx >= dst.width || dy >= dst.height) continue;
      const i = (dy * dst.width + dx) * 4;
      overPixel(dst.data, i, color[0], color[1], color[2], (c / 255) * ca);
    }
  }
}

/** Straight-alpha "source over" of one pixel; the same arithmetic the compositor uses for Normal. */
export function overPixel(d: Uint8ClampedArray, i: number, r: number, g: number, b: number, as: number): void {
  if (as <= 0) return;
  const ab = d[i + 3] / 255;
  const ao = as + ab * (1 - as);
  d[i] = Math.round((as * r + ab * d[i] * (1 - as)) / ao);
  d[i + 1] = Math.round((as * g + ab * d[i + 1] * (1 - as)) / ao);
  d[i + 2] = Math.round((as * b + ab * d[i + 2] * (1 - as)) / ao);
  d[i + 3] = Math.round(ao * 255);
}

export function rasterizeShape(layer: Pick<ShapeLayer, 'shape' | 'fillColor' | 'strokeColor' | 'strokeWidth'>, width: number, height: number): TileGrid {
  const clip = { x: 0, y: 0, width, height };
  const { points, closed } = shapeOutline(layer.shape);
  const fillCov = layer.fillColor && closed ? rasterizePaths([points], clip, 'nonzero') : null;
  const strokeCov = layer.strokeColor && layer.strokeWidth > 0 ? rasterizePaths(strokePaths(points, closed, layer.strokeWidth), clip, 'nonzero') : null;
  const parts = [fillCov, strokeCov].filter((r): r is Region => !!r);
  if (!parts.length) return emptyGrid(width, height, 4);
  const x0 = Math.min(...parts.map((r) => r.x)), y0 = Math.min(...parts.map((r) => r.y));
  const x1 = Math.max(...parts.map((r) => r.x + r.width)), y1 = Math.max(...parts.map((r) => r.y + r.height));
  const rgba = createRegion({ x: x0, y: y0, width: x1 - x0, height: y1 - y0 }, 4);
  if (fillCov) paintCoverage(rgba, fillCov, layer.fillColor!);
  if (strokeCov) paintCoverage(rgba, strokeCov, layer.strokeColor!);
  return writeGrid(emptyGrid(width, height, 4), rgba);
}

// ---------------------------------------------------------------------------
// Text: rasterized by the host (fonts are a platform service). Core only defines the hook.
// ---------------------------------------------------------------------------

export type TextRasterizer = (layer: TextLayer, width: number, height: number) => Region | null;

let textRasterizer: TextRasterizer | null = null;

/** Registers the platform text rasterizer (the UI installs an OffscreenCanvas-based one). */
export function setTextRasterizer(r: TextRasterizer | null): void {
  textRasterizer = r;
}

export function rasterizeText(layer: TextLayer, width: number, height: number): TileGrid {
  const region = textRasterizer?.(layer, width, height) ?? null;
  return region ? writeGrid(emptyGrid(width, height, 4), region) : emptyGrid(width, height, 4);
}

export const hasTextRasterizer = () => textRasterizer !== null;
