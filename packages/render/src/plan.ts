import { blendModeIndex, type BlendMode, type Document, type LayerNode, type Tile } from '@canvas-ai/core';

/**
 * A per-tile compositing plan: the minimal sequence of operations that
 * produces one canvas tile. Every backend (CPU, WebGL2, WebGPU) executes the
 * same plan, which keeps them structurally identical.
 */
export type PlanItem =
  | { readonly kind: 'layer'; readonly tile: Tile; readonly mode: BlendMode; readonly modeIndex: number; readonly opacity: number }
  | { readonly kind: 'isolated'; readonly children: Plan; readonly mode: BlendMode; readonly modeIndex: number; readonly opacity: number }
  | { readonly kind: 'passThrough'; readonly children: Plan; readonly opacity: number };

export type Plan = readonly PlanItem[];

export function buildTilePlan(layers: readonly LayerNode[], key: number): Plan {
  const plan: PlanItem[] = [];
  for (const layer of layers) {
    if (!layer.visible || layer.opacity <= 0) continue;
    switch (layer.type) {
      case 'pixel':
      case 'smart':
      case 'ai': {
        const tile = layer.tiles.tiles.get(key);
        const opacity = layer.opacity * layer.fill;
        if (tile && opacity > 0) plan.push({ kind: 'layer', tile, mode: layer.blendMode, modeIndex: blendModeIndex(layer.blendMode), opacity });
        break;
      }
      case 'group': {
        const children = buildTilePlan(layer.children, key);
        if (children.length === 0) break;
        if (layer.blendMode === 'passThrough') plan.push({ kind: 'passThrough', children, opacity: layer.opacity });
        else plan.push({ kind: 'isolated', children, mode: layer.blendMode, modeIndex: blendModeIndex(layer.blendMode), opacity: layer.opacity });
        break;
      }
      default:
        // Adjustment, text, shape layers render from M2 on.
        break;
    }
  }
  return plan;
}

export function documentTilePlan(doc: Document, key: number): Plan {
  return buildTilePlan(doc.layers, key);
}
