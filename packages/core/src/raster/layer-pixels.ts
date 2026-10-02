import type { LayerNode } from '../document';
import type { TileGrid } from '../tiles';
import { hasTextRasterizer, rasterizeShape, rasterizeText } from './shapes';

const cache = new WeakMap<LayerNode, TileGrid>();

/**
 * The raster content of a layer: stored tiles for pixel/smart/AI layers,
 * rasterized (and cached by layer identity) for shape and text layers.
 * Returns null for layers without pixels (groups, adjustments).
 */
export function layerPixels(layer: LayerNode, width: number, height: number): TileGrid | null {
  switch (layer.type) {
    case 'pixel':
    case 'smart':
    case 'ai':
      return layer.tiles;
    case 'shape':
    case 'text': {
      const hit = cache.get(layer);
      if (hit && hit.width === width && hit.height === height) return hit;
      const grid = layer.type === 'shape' ? rasterizeShape(layer, width, height) : rasterizeText(layer, width, height);
      // Text rendered before the platform rasterizer exists would be empty; don't cache that.
      if (layer.type === 'shape' || hasTextRasterizer()) cache.set(layer, grid);
      return grid;
    }
    default:
      return null;
  }
}
