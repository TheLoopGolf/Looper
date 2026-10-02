import { defineCommand, type Command } from '../command';
import { CommandError, createAdjustmentLayer, type LayerNode, type Mask } from '../document';
import { PatchBuilder } from '../patch';
import { ADJUSTMENT_KINDS, ADJUSTMENTS, adjustPixel, compileAdjustment, isAdjustmentKind } from '../raster/adjustments';
import { FILTER_KINDS, FILTERS } from '../raster/filters';
import { gridBounds, padRect, readGrid, writeGrid } from '../raster/region';
import type { JSONSchema } from '../schema';
import { intersectRect } from '../tiles';
import { editRect, layerIdParam, placementParams, requireLayer, requirePixelLayer, resolveInsertion, selectionCoverage } from './common';

const kindParam: JSONSchema = { type: 'string', enum: ADJUSTMENT_KINDS, title: 'Adjustment', description: ADJUSTMENT_KINDS.map((k) => `${k}: ${ADJUSTMENTS[k as keyof typeof ADJUSTMENTS].description}`).join(' ') };
const paramsParam: JSONSchema = {
  type: 'object',
  title: 'Settings',
  properties: {},
  description: 'Adjustment settings; see the adjustment kinds. Omitted settings use defaults.',
};

function checkParams(kind: string, params: Record<string, unknown>): void {
  if (!isAdjustmentKind(kind)) throw new CommandError(`Unknown adjustment: ${kind}`);
  const schema = ADJUSTMENTS[kind].schema as Record<string, JSONSchema>;
  for (const key of Object.keys(params)) if (!(key in schema)) throw new CommandError(`Unknown setting "${key}" for ${kind}`);
}

export const createAdjustment = defineCommand<{ kind: string; params?: Record<string, unknown>; name?: string } & Record<string, unknown>>({
  id: 'layer.createAdjustment',
  title: 'New Adjustment Layer',
  category: 'Layer',
  agentDescription:
    'Add a non-destructive adjustment layer that changes everything below it (preferred over adjust.apply). ' +
    'If there is a selection it becomes the layer mask. Returns { layerId }.',
  schema: {
    type: 'object',
    additionalProperties: false,
    required: ['kind'],
    properties: { kind: kindParam, params: paramsParam, name: { type: 'string', maxLength: 255 }, ...placementParams },
  },
  describe: (p) => (isAdjustmentKind(p.kind) ? ADJUSTMENTS[p.kind].label : 'New Adjustment Layer'),
  execute(doc, p, ctx) {
    const params = { ...(p.params ?? {}) };
    checkParams(p.kind, params);
    const def = ADJUSTMENTS[p.kind as keyof typeof ADJUSTMENTS];
    const id = ctx.newId('adjust');
    let layer: LayerNode = createAdjustmentLayer(id, p.name ?? def.label, p.kind, { ...def.defaults, ...params });
    if (doc.selection) {
      const mask: Mask = { tiles: doc.selection.mask, defaultValue: 0, enabled: true, linked: true, density: 1, feather: 0 };
      layer = { ...layer, mask };
    }
    const { parentId, index } = resolveInsertion(doc, p as { above?: string; below?: string; parentId?: string | null; index?: number });
    return new PatchBuilder(doc).insertLayer(parentId, index, layer).build({ layerId: id });
  },
});

export const setAdjustment = defineCommand<{ layerId: string; params: Record<string, unknown> }>({
  id: 'layer.setAdjustment',
  title: 'Edit Adjustment',
  category: 'Layer',
  agentDescription: "Change an adjustment layer's settings (merged into the current settings).",
  schema: { type: 'object', additionalProperties: false, required: ['layerId', 'params'], properties: { layerId: layerIdParam, params: paramsParam } },
  coalesceKey: (p) => `adjust:${p.layerId}:${Object.keys(p.params).sort().join(',')}`,
  execute(doc, p) {
    const l = requireLayer(doc, p.layerId).layer;
    if (l.type !== 'adjustment') throw new CommandError(`Layer "${l.name}" is not an adjustment layer`);
    checkParams(l.adjustment.kind, p.params);
    return new PatchBuilder(doc).replaceLayer({ ...l, adjustment: { ...l.adjustment, params: { ...l.adjustment.params, ...p.params } } }).build();
  },
});

export const applyAdjustment = defineCommand<{ layerId: string; kind: string; params?: Record<string, unknown> }>({
  id: 'adjust.apply',
  title: 'Apply Adjustment',
  category: 'Image',
  agentDescription: 'Destructively apply an adjustment to a pixel layer within the selection. Prefer layer.createAdjustment unless the user wants pixels changed.',
  destructive: true,
  schema: { type: 'object', additionalProperties: false, required: ['layerId', 'kind'], properties: { layerId: layerIdParam, kind: kindParam, params: paramsParam } },
  describe: (p) => (isAdjustmentKind(p.kind) ? ADJUSTMENTS[p.kind].label : 'Apply Adjustment'),
  execute(doc, p) {
    const params = p.params ?? {};
    checkParams(p.kind, params);
    const layer = requirePixelLayer(doc, p.layerId, 'adjust');
    const rect = editRect(doc) && intersectRect(editRect(doc)!, gridBounds(layer.tiles) ?? { x: 0, y: 0, width: 0, height: 0 });
    const b = new PatchBuilder(doc);
    if (!rect) return b.build();
    const c = compileAdjustment(p.kind, { ...params });
    const region = readGrid(layer.tiles, rect);
    const cov = selectionCoverage(doc, rect);
    for (let i = 0, k = 0; i < region.data.length; i += 4, k++) {
      if (!region.data[i + 3]) continue;
      const f = cov ? cov.data[k] / 255 : 1;
      if (f <= 0) continue;
      const adj = adjustPixel(c, region.data[i], region.data[i + 1], region.data[i + 2]);
      for (let ch = 0; ch < 3; ch++) region.data[i + ch] = Math.round(region.data[i + ch] + (adj[ch] * 255 - region.data[i + ch]) * f);
    }
    return b.replaceLayer({ ...layer, tiles: writeGrid(layer.tiles, region) }).build();
  },
});

/** One command per filter: filter.gaussianBlur, filter.motionBlur, … */
export const FILTER_COMMANDS: Command<any>[] = FILTER_KINDS.map((kind) => {
  const def = FILTERS[kind];
  return defineCommand<{ layerId: string } & Record<string, unknown>>({
    id: `filter.${kind}`,
    title: def.label,
    category: 'Filter',
    agentDescription: `${def.description} Applies to a pixel layer within the selection (destructive; duplicate the layer first to keep the original).`,
    destructive: true,
    schema: {
      type: 'object',
      additionalProperties: false,
      required: ['layerId'],
      properties: { layerId: layerIdParam, ...(def.schema as Record<string, JSONSchema>) },
    },
    execute(doc, p) {
      const layer = requirePixelLayer(doc, p.layerId, `apply ${def.label}`);
      const params = { ...def.defaults, ...p } as Record<string, unknown>;
      const pad = (def.pad as (p: Record<string, unknown>) => number)(params);
      const full = { x: 0, y: 0, width: doc.width, height: doc.height };
      const contentBounds = gridBounds(layer.tiles);
      const target = editRect(doc);
      const b = new PatchBuilder(doc);
      if (!contentBounds || !target) return b.build();
      // Output area: the selection, limited to where content (or content spread by the filter) can be.
      const out = intersectRect(target, padRect(contentBounds, pad, full)!);
      if (!out) return b.build();
      const work = padRect(out, pad, full)!;
      const src = readGrid(layer.tiles, work);
      const filtered = (def.apply as (r: typeof src, p: Record<string, unknown>) => typeof src)(src, params);
      const orig = readGrid(layer.tiles, out);
      const cov = selectionCoverage(doc, out);
      for (let y = 0; y < out.height; y++) {
        for (let x = 0; x < out.width; x++) {
          const o = (y * out.width + x) * 4;
          const s = ((y + out.y - work.y) * work.width + (x + out.x - work.x)) * 4;
          const f = cov ? cov.data[y * out.width + x] / 255 : 1;
          for (let c = 0; c < 4; c++) orig.data[o + c] = f >= 1 ? filtered.data[s + c] : Math.round(orig.data[o + c] + (filtered.data[s + c] - orig.data[o + c]) * f);
        }
      }
      return b.replaceLayer({ ...layer, tiles: writeGrid(layer.tiles, orig) }).build();
    },
  });
});

export const ADJUST_COMMANDS: Command<any>[] = [createAdjustment, setAdjustment, applyAdjustment];
