import { blendModeIndex, type BlendMode } from '../blend-modes';
import type { Document, LayerNode } from '../document';
import { compileAdjustment, type CompiledAdjustment } from '../raster/adjustments';
import { layerPixels } from '../raster/layer-pixels';
import type { Tile } from '../tiles';

/** Per-pixel mask for one tile: value = 1 − density·(1 − tile/255). */
export interface MaskRef {
  readonly tile: Tile;
  readonly density: number;
}

/**
 * A per-tile compositing plan: the minimal sequence of operations that
 * produces one canvas tile. Every backend (CPU, WebGL2, WebGPU) executes the
 * same plan, which keeps them structurally identical.
 */
export type PlanItem =
  | { readonly kind: 'layer'; readonly tile: Tile; readonly mode: BlendMode; readonly modeIndex: number; readonly opacity: number; readonly mask: MaskRef | null }
  | { readonly kind: 'isolated'; readonly children: Plan; readonly mode: BlendMode; readonly modeIndex: number; readonly opacity: number; readonly mask: MaskRef | null }
  | { readonly kind: 'passThrough'; readonly children: Plan; readonly opacity: number; readonly mask: MaskRef | null }
  | {
      readonly kind: 'adjust';
      readonly adjustment: CompiledAdjustment;
      readonly mode: BlendMode;
      readonly modeIndex: number;
      readonly opacity: number;
      readonly mask: MaskRef | null;
    };

export type Plan = readonly PlanItem[];

/**
 * Resolves a layer mask for a tile. Returns `false` when the layer is fully
 * hidden there, a number to fold into opacity when the mask is uniform, or a
 * MaskRef for per-pixel masking.
 */
function resolveMask(layer: LayerNode, key: number): MaskRef | number | false {
  const mask = layer.mask;
  if (!mask || !mask.enabled || mask.density <= 0) return 1;
  const tile = mask.tiles.tiles.get(key);
  if (tile) return { tile, density: mask.density };
  if (mask.defaultValue === 255) return 1;
  const v = 1 - mask.density;
  return v <= 0 ? false : v;
}

export function buildTilePlan(layers: readonly LayerNode[], key: number, size: { width: number; height: number }, hasBackdrop = false): Plan {
  const plan: PlanItem[] = [];
  for (const layer of layers) {
    if (!layer.visible || layer.opacity <= 0) continue;
    const m = resolveMask(layer, key);
    if (m === false) continue;
    const maskOpacity = typeof m === 'number' ? m : 1;
    const mask = typeof m === 'number' ? null : m;
    if (layer.type === 'group') {
      const pass = layer.blendMode === 'passThrough';
      const children = buildTilePlan(layer.children, key, size, pass && (hasBackdrop || plan.length > 0));
      if (children.length === 0) continue;
      if (pass) plan.push({ kind: 'passThrough', children, opacity: layer.opacity * maskOpacity, mask });
      else plan.push({ kind: 'isolated', children, mode: layer.blendMode, modeIndex: blendModeIndex(layer.blendMode), opacity: layer.opacity * maskOpacity, mask });
    } else if (layer.type === 'adjustment') {
      // Nothing below to adjust in this tile.
      if (!hasBackdrop && plan.length === 0) continue;
      plan.push({
        kind: 'adjust',
        adjustment: compileAdjustment(layer.adjustment.kind, layer.adjustment.params as Record<string, unknown>),
        mode: layer.blendMode,
        modeIndex: blendModeIndex(layer.blendMode),
        opacity: layer.opacity * maskOpacity,
        mask,
      });
    } else {
      const tile = layerPixels(layer, size.width, size.height)?.tiles.get(key);
      const opacity = layer.opacity * layer.fill * maskOpacity;
      if (tile && opacity > 0) plan.push({ kind: 'layer', tile, mode: layer.blendMode, modeIndex: blendModeIndex(layer.blendMode), opacity, mask });
    }
  }
  return plan;
}

export function documentTilePlan(doc: Document, key: number): Plan {
  return buildTilePlan(doc.layers, key, doc);
}
