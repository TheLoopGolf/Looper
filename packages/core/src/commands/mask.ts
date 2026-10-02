import { defineCommand, type Command } from '../command';
import { CommandError, type LayerNode, type Mask } from '../document';
import { PatchBuilder } from '../patch';
import { gridBounds, invertMask, readGrid, writeGrid } from '../raster/region';
import { emptyGrid } from '../tiles';
import { layerIdParam, requireLayer, requireUnlocked } from './common';

const idOnly = { type: 'object', additionalProperties: false, required: ['layerId'], properties: { layerId: layerIdParam } } as const;

export const addMask = defineCommand<{ layerId: string; from?: 'revealAll' | 'hideAll' | 'selection' }>({
  id: 'layer.addMask',
  title: 'Add Layer Mask',
  category: 'Layer',
  agentDescription:
    'Add a layer mask (non-destructive hiding). from: "revealAll" (default; or the selection if one exists), "hideAll", or "selection". Paint the mask with paint.stroke target="mask".',
  schema: {
    type: 'object',
    additionalProperties: false,
    required: ['layerId'],
    properties: { layerId: layerIdParam, from: { type: 'string', enum: ['revealAll', 'hideAll', 'selection'], title: 'Start from' } },
  },
  execute(doc, p) {
    const loc = requireLayer(doc, p.layerId);
    requireUnlocked(loc, 'add a mask');
    if (loc.layer.mask) throw new CommandError(`Layer "${loc.layer.name}" already has a mask`);
    const from = p.from ?? (doc.selection ? 'selection' : 'revealAll');
    if (from === 'selection' && !doc.selection) throw new CommandError('Nothing is selected');
    const mask: Mask = {
      tiles: from === 'selection' ? doc.selection!.mask : emptyGrid(doc.width, doc.height, 1),
      defaultValue: from === 'revealAll' ? 255 : 0,
      enabled: true,
      linked: true,
      density: 1,
      feather: 0,
    };
    return new PatchBuilder(doc).replaceLayer({ ...loc.layer, mask } as LayerNode).build();
  },
});

export const deleteMask = defineCommand<{ layerId: string }>({
  id: 'layer.deleteMask',
  title: 'Delete Layer Mask',
  category: 'Layer',
  agentDescription: 'Remove a layer mask without applying it (the layer shows fully again).',
  destructive: true,
  schema: idOnly,
  execute(doc, p) {
    const loc = requireLayer(doc, p.layerId);
    if (!loc.layer.mask) throw new CommandError(`Layer "${loc.layer.name}" has no mask`);
    return new PatchBuilder(doc).replaceLayer({ ...loc.layer, mask: null } as LayerNode).build();
  },
});

export const applyMask = defineCommand<{ layerId: string }>({
  id: 'layer.applyMask',
  title: 'Apply Layer Mask',
  category: 'Layer',
  agentDescription: "Bake a pixel layer's mask into its transparency and remove the mask.",
  destructive: true,
  schema: idOnly,
  execute(doc, p) {
    const loc = requireLayer(doc, p.layerId);
    requireUnlocked(loc, 'apply the mask');
    const l = loc.layer;
    if (l.type !== 'pixel' && l.type !== 'ai') throw new CommandError('Only pixel layers can apply a mask; rasterize first');
    if (!l.mask) throw new CommandError(`Layer "${l.name}" has no mask`);
    const bounds = gridBounds(l.tiles);
    let tiles = l.tiles;
    if (bounds && l.mask.enabled) {
      const px = readGrid(l.tiles, bounds);
      const m = readGrid(l.mask.tiles, bounds, l.mask.defaultValue);
      const d = l.mask.density;
      for (let i = 0; i < m.data.length; i++) px.data[i * 4 + 3] = Math.round(px.data[i * 4 + 3] * (1 - d * (1 - m.data[i] / 255)));
      tiles = writeGrid(l.tiles, px);
    }
    return new PatchBuilder(doc).replaceLayer({ ...l, tiles, mask: null }).build();
  },
});

export const setMaskEnabled = defineCommand<{ layerId: string; enabled: boolean }>({
  id: 'layer.setMaskEnabled',
  title: 'Toggle Layer Mask',
  category: 'Layer',
  agentDescription: 'Temporarily disable or re-enable a layer mask.',
  schema: { type: 'object', additionalProperties: false, required: ['layerId', 'enabled'], properties: { layerId: layerIdParam, enabled: { type: 'boolean' } } },
  describe: (p) => (p.enabled ? 'Enable Layer Mask' : 'Disable Layer Mask'),
  execute(doc, p) {
    const l = requireLayer(doc, p.layerId).layer;
    if (!l.mask) throw new CommandError(`Layer "${l.name}" has no mask`);
    if (l.mask.enabled === p.enabled) return { ops: [] };
    return new PatchBuilder(doc).replaceLayer({ ...l, mask: { ...l.mask, enabled: p.enabled } } as LayerNode).build();
  },
});

export const invertLayerMask = defineCommand<{ layerId: string }>({
  id: 'layer.invertMask',
  title: 'Invert Layer Mask',
  category: 'Layer',
  agentDescription: 'Invert a layer mask: hidden areas become visible and vice versa.',
  schema: idOnly,
  execute(doc, p) {
    const loc = requireLayer(doc, p.layerId);
    requireUnlocked(loc, 'invert the mask');
    const m = loc.layer.mask;
    if (!m) throw new CommandError(`Layer "${loc.layer.name}" has no mask`);
    // invertMask treats missing tiles as 0; materialize a reveal-all default first.
    const src = m.defaultValue === 255 ? withDefaultFilled(m.tiles, doc.width, doc.height) : m.tiles;
    const inverted = invertMask(src, doc.width, doc.height);
    return new PatchBuilder(doc).replaceLayer({ ...loc.layer, mask: { ...m, tiles: inverted, defaultValue: 0 } } as LayerNode).build();
  },
});

/** Mask tiles with every missing tile replaced by a full (255) tile. */
function withDefaultFilled(tiles: Mask['tiles'], width: number, height: number) {
  const full = readGrid(tiles, { x: 0, y: 0, width, height }, 255);
  return writeGrid(emptyGrid(width, height, 1), full);
}

export const setMaskDensity = defineCommand<{ layerId: string; density: number }>({
  id: 'layer.setMaskDensity',
  title: 'Mask Density',
  category: 'Layer',
  agentDescription: 'Set how strongly a layer mask hides (0-100%); lower values let masked areas partly show.',
  schema: {
    type: 'object',
    additionalProperties: false,
    required: ['layerId', 'density'],
    properties: { layerId: layerIdParam, density: { type: 'number', minimum: 0, maximum: 100, title: 'Density (%)' } },
  },
  coalesceKey: (p) => `maskDensity:${p.layerId}`,
  execute(doc, p) {
    const l = requireLayer(doc, p.layerId).layer;
    if (!l.mask) throw new CommandError(`Layer "${l.name}" has no mask`);
    return new PatchBuilder(doc).replaceLayer({ ...l, mask: { ...l.mask, density: p.density / 100 } } as LayerNode).build();
  },
});

export const MASK_COMMANDS: Command<any>[] = [addMask, deleteMask, applyMask, setMaskEnabled, invertLayerMask, setMaskDensity];
