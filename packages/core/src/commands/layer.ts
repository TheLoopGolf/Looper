import { BLEND_MODE_IDS, type BlendMode, type GroupBlendMode } from '../blend-modes';
import { defineCommand } from '../command';
import {
  CommandError,
  createGroupLayer,
  createPixelLayer,
  findLayer,
  walkLayers,
  type LayerNode,
} from '../document';
import { PatchBuilder } from '../patch';
import { blitImage, emptyGrid, fillRect } from '../tiles';
import { colorParam, layerIdParam, placementParams, requireLayer, requireUnlocked, resolveInsertion } from './common';

type Placement = { above?: string; below?: string; parentId?: string | null; index?: number };

function nextLayerName(layers: readonly LayerNode[], base: string): string {
  const names = new Set([...walkLayers(layers)].map((l) => l.name));
  for (let i = 1; ; i++) if (!names.has(`${base} ${i}`)) return `${base} ${i}`;
}

export const createLayer = defineCommand<Placement & { name?: string; kind?: 'pixel' | 'group'; color?: number[] }>({
  id: 'layer.create',
  title: 'New Layer',
  category: 'Layer',
  agentDescription:
    'Create a new empty pixel layer (or an empty group with kind="group"). Optionally fill it with a solid color. ' +
    'By default it goes on top of the document; use "above"/"below" to position it relative to another layer. Returns { layerId }.',
  schema: {
    type: 'object',
    additionalProperties: false,
    properties: {
      name: { type: 'string', maxLength: 255, description: 'Layer name. Defaults to "Layer N".' },
      kind: { type: 'string', enum: ['pixel', 'group'], description: 'Layer kind, default "pixel".' },
      color: colorParam,
      ...placementParams,
    },
  },
  describe: (p) => (p.kind === 'group' ? 'New Group' : 'New Layer'),
  execute(doc, p, ctx) {
    const { parentId, index } = resolveInsertion(doc, p);
    const isGroup = p.kind === 'group';
    const id = ctx.newId(isGroup ? 'group' : 'layer');
    const name = p.name ?? nextLayerName(doc.layers, isGroup ? 'Group' : 'Layer');
    let layer: LayerNode = isGroup ? createGroupLayer(id, name) : createPixelLayer(doc, id, name);
    if (p.color && layer.type === 'pixel') {
      layer = { ...layer, tiles: fillRect(layer.tiles, { x: 0, y: 0, width: doc.width, height: doc.height }, p.color) };
    }
    return new PatchBuilder(doc).insertLayer(parentId, index, layer).build({ layerId: id });
  },
});

export const deleteLayer = defineCommand<{ layerId: string }>({
  id: 'layer.delete',
  title: 'Delete Layer',
  category: 'Layer',
  agentDescription: 'Delete a layer (and all children if it is a group). Prefer hiding a layer unless the user asked to delete it.',
  destructive: true,
  schema: { type: 'object', additionalProperties: false, required: ['layerId'], properties: { layerId: layerIdParam } },
  execute(doc, p) {
    requireUnlocked(requireLayer(doc, p.layerId), 'delete');
    return new PatchBuilder(doc).removeLayer(p.layerId).build();
  },
});

function cloneWithNewIds(layer: LayerNode, newId: (prefix: string) => string): LayerNode {
  if (layer.type === 'group') {
    return { ...layer, id: newId('group'), children: layer.children.map((c) => cloneWithNewIds(c, newId)) };
  }
  // Tiles are immutable and copy-on-write, so the copy can share them.
  return { ...layer, id: newId('layer') };
}

export const duplicateLayer = defineCommand<{ layerId: string; name?: string }>({
  id: 'layer.duplicate',
  title: 'Duplicate Layer',
  category: 'Layer',
  agentDescription:
    'Duplicate a layer (or group) and place the copy directly above the original. Useful before risky edits so the original is preserved. Returns { layerId }.',
  schema: {
    type: 'object',
    additionalProperties: false,
    required: ['layerId'],
    properties: { layerId: layerIdParam, name: { type: 'string', maxLength: 255, description: 'Name for the copy.' } },
  },
  execute(doc, p, ctx) {
    const loc = requireLayer(doc, p.layerId);
    const copy = { ...cloneWithNewIds(loc.layer, ctx.newId), name: p.name ?? `${loc.layer.name} copy` } as LayerNode;
    return new PatchBuilder(doc).insertLayer(loc.parentId, loc.index + 1, copy).build({ layerId: copy.id });
  },
});

export const moveLayer = defineCommand<Placement & { layerId: string }>({
  id: 'layer.move',
  title: 'Move Layer',
  category: 'Layer',
  agentDescription:
    'Reorder a layer in the stack or move it into/out of a group. Use "above"/"below" with another layer id, or "parentId" + "index" (0 = bottom).',
  schema: {
    type: 'object',
    additionalProperties: false,
    required: ['layerId'],
    properties: { layerId: layerIdParam, ...placementParams },
  },
  execute(doc, p) {
    const loc = requireLayer(doc, p.layerId);
    requireUnlocked(loc, 'move');
    if (p.above === p.layerId || p.below === p.layerId) return new PatchBuilder(doc).build();
    const b = new PatchBuilder(doc).removeLayer(p.layerId);
    // After removal the moved layer's subtree is gone, so targeting it (a cycle) fails to resolve.
    let target: { parentId: string | null; index: number };
    try {
      target = resolveInsertion(b.doc, p);
    } catch (e) {
      if (p.parentId === p.layerId || (p.parentId && !findLayer(b.doc, p.parentId) && findLayer(doc, p.parentId))) {
        throw new CommandError('Cannot move a group into itself');
      }
      throw e;
    }
    if (target.parentId === loc.parentId && target.index === loc.index) return new PatchBuilder(doc).build();
    return b.insertLayer(target.parentId, target.index, loc.layer).build();
  },
});


export const renameLayer = defineCommand<{ layerId: string; name: string }>({
  id: 'layer.rename',
  title: 'Rename Layer',
  category: 'Layer',
  agentDescription: 'Rename a layer. Use short descriptive names (e.g. "Sky replacement").',
  schema: {
    type: 'object',
    additionalProperties: false,
    required: ['layerId', 'name'],
    properties: { layerId: layerIdParam, name: { type: 'string', minLength: 1, maxLength: 255 } },
  },
  execute(doc, p) {
    requireLayer(doc, p.layerId);
    return new PatchBuilder(doc).updateLayer(p.layerId, (l) => (l.name === p.name ? l : { ...l, name: p.name })).build();
  },
});

export const setVisible = defineCommand<{ layerId: string; visible: boolean }>({
  id: 'layer.setVisible',
  title: 'Toggle Visibility',
  category: 'Layer',
  agentDescription: 'Show or hide a layer. Hidden layers are not rendered or exported but keep their content.',
  schema: {
    type: 'object',
    additionalProperties: false,
    required: ['layerId', 'visible'],
    properties: { layerId: layerIdParam, visible: { type: 'boolean' } },
  },
  describe: (p) => (p.visible ? 'Show Layer' : 'Hide Layer'),
  execute(doc, p) {
    requireLayer(doc, p.layerId);
    return new PatchBuilder(doc)
      .updateLayer(p.layerId, (l) => (l.visible === p.visible ? l : { ...l, visible: p.visible }))
      .build();
  },
});

export const setLocked = defineCommand<{ layerId: string; locked: boolean }>({
  id: 'layer.setLocked',
  title: 'Lock Layer',
  category: 'Layer',
  agentDescription: 'Lock or unlock a layer. Locked layers cannot be painted on, moved or deleted.',
  schema: {
    type: 'object',
    additionalProperties: false,
    required: ['layerId', 'locked'],
    properties: { layerId: layerIdParam, locked: { type: 'boolean' } },
  },
  describe: (p) => (p.locked ? 'Lock Layer' : 'Unlock Layer'),
  execute(doc, p) {
    requireLayer(doc, p.layerId);
    return new PatchBuilder(doc).updateLayer(p.layerId, (l) => (l.locked === p.locked ? l : { ...l, locked: p.locked })).build();
  },
});

export const setOpacity = defineCommand<{ layerId: string; opacity: number }>({
  id: 'layer.setOpacity',
  title: 'Layer Opacity',
  category: 'Layer',
  agentDescription: 'Set layer opacity in percent (0-100). Affects the layer and its effects.',
  schema: {
    type: 'object',
    additionalProperties: false,
    required: ['layerId', 'opacity'],
    properties: { layerId: layerIdParam, opacity: { type: 'number', minimum: 0, maximum: 100 } },
  },
  describe: (p) => `Opacity ${Math.round(p.opacity)}%`,
  coalesceKey: (p) => `opacity:${p.layerId}`,
  execute(doc, p) {
    requireLayer(doc, p.layerId);
    const opacity = p.opacity / 100;
    return new PatchBuilder(doc).updateLayer(p.layerId, (l) => (l.opacity === opacity ? l : { ...l, opacity })).build();
  },
});

export const setFill = defineCommand<{ layerId: string; fill: number }>({
  id: 'layer.setFill',
  title: 'Fill Opacity',
  category: 'Layer',
  agentDescription: 'Set layer fill opacity in percent (0-100). Like opacity, but layer effects (shadows, strokes) stay fully visible.',
  schema: {
    type: 'object',
    additionalProperties: false,
    required: ['layerId', 'fill'],
    properties: { layerId: layerIdParam, fill: { type: 'number', minimum: 0, maximum: 100 } },
  },
  describe: (p) => `Fill ${Math.round(p.fill)}%`,
  coalesceKey: (p) => `fill:${p.layerId}`,
  execute(doc, p) {
    requireLayer(doc, p.layerId);
    const fill = p.fill / 100;
    return new PatchBuilder(doc).updateLayer(p.layerId, (l) => (l.fill === fill ? l : { ...l, fill })).build();
  },
});

export const setBlendMode = defineCommand<{ layerId: string; blendMode: GroupBlendMode }>({
  id: 'layer.setBlendMode',
  title: 'Blend Mode',
  category: 'Layer',
  agentDescription:
    'Set how a layer blends with the layers below it (e.g. multiply darkens, screen lightens, overlay adds contrast, color/luminosity for color work). ' +
    '"passThrough" is only valid for groups.',
  schema: {
    type: 'object',
    additionalProperties: false,
    required: ['layerId', 'blendMode'],
    properties: { layerId: layerIdParam, blendMode: { type: 'string', enum: [...BLEND_MODE_IDS, 'passThrough'] } },
  },
  describe: (p) => `Blend Mode: ${p.blendMode}`,
  execute(doc, p) {
    const { layer } = requireLayer(doc, p.layerId);
    if (p.blendMode === 'passThrough' && layer.type !== 'group') {
      throw new CommandError('"passThrough" can only be used on groups');
    }
    return new PatchBuilder(doc)
      .updateLayer(p.layerId, (l) => (l.blendMode === p.blendMode ? l : ({ ...l, blendMode: p.blendMode as BlendMode } as LayerNode)))
      .build();
  },
});

export const groupLayers = defineCommand<{ layerIds: string[]; name?: string }>({
  id: 'layer.group',
  title: 'Group Layers',
  category: 'Layer',
  agentDescription:
    'Put layers that share the same parent into a new group, placed where the top-most of them was. Keeps their order. Returns { groupId }.',
  schema: {
    type: 'object',
    additionalProperties: false,
    required: ['layerIds'],
    properties: {
      layerIds: { type: 'array', minItems: 1, items: { type: 'string' }, description: 'Layers to group.' },
      name: { type: 'string', maxLength: 255 },
    },
  },
  execute(doc, p, ctx) {
    const locs = [...new Set(p.layerIds)].map((id) => requireLayer(doc, id));
    const parentId = locs[0].parentId;
    if (locs.some((l) => l.parentId !== parentId)) throw new CommandError('All grouped layers must have the same parent');
    locs.sort((a, b) => a.index - b.index);
    const b = new PatchBuilder(doc);
    for (const loc of [...locs].reverse()) b.removeLayer(loc.layer.id);
    const insertAt = locs[locs.length - 1].index - (locs.length - 1);
    const id = ctx.newId('group');
    const group = createGroupLayer(id, p.name ?? nextLayerName(doc.layers, 'Group'), locs.map((l) => l.layer));
    return b.insertLayer(parentId, insertAt, group).build({ groupId: id });
  },
});

export const ungroupLayers = defineCommand<{ groupId: string }>({
  id: 'layer.ungroup',
  title: 'Ungroup Layers',
  category: 'Layer',
  agentDescription: "Remove a group but keep its layers, placing them where the group was. The group's own opacity/blend mode are dropped.",
  schema: { type: 'object', additionalProperties: false, required: ['groupId'], properties: { groupId: layerIdParam } },
  execute(doc, p) {
    const loc = requireLayer(doc, p.groupId);
    if (loc.layer.type !== 'group') throw new CommandError(`Layer "${loc.layer.name}" is not a group`);
    const b = new PatchBuilder(doc).removeLayer(p.groupId);
    loc.layer.children.forEach((child, i) => b.insertLayer(loc.parentId, loc.index + i, child));
    return b.build();
  },
});

export const fillRectCommand = defineCommand<{ layerId: string; x: number; y: number; width: number; height: number; color: number[] }>({
  id: 'pixels.fillRect',
  title: 'Fill Rectangle',
  category: 'Edit',
  agentDescription:
    'Overwrite a rectangle of a pixel layer with a solid RGBA color (alpha 0 clears). Coordinates are document pixels. ' +
    'Prefer creating a new layer and filling that, so the original pixels stay editable.',
  destructive: true,
  schema: {
    type: 'object',
    additionalProperties: false,
    required: ['layerId', 'x', 'y', 'width', 'height', 'color'],
    properties: {
      layerId: layerIdParam,
      x: { type: 'integer' },
      y: { type: 'integer' },
      width: { type: 'integer', minimum: 1 },
      height: { type: 'integer', minimum: 1 },
      color: colorParam,
    },
  },
  execute(doc, p) {
    const loc = requireLayer(doc, p.layerId);
    requireUnlocked(loc, 'edit pixels');
    const layer = loc.layer;
    if (layer.type !== 'pixel' && layer.type !== 'ai') throw new CommandError(`Layer "${layer.name}" is not a pixel layer`);
    const tiles = fillRect(layer.tiles, p, p.color);
    return new PatchBuilder(doc).replaceLayer({ ...layer, tiles }).build();
  },
});

export const placeImage = defineCommand<Placement & { resourceId: string; name?: string; x?: number; y?: number }>({
  id: 'layer.placeImage',
  title: 'Place Image',
  category: 'Layer',
  agentDescription:
    'Add an image (from a resource id returned by import or AI tools) as a new pixel layer at document position x,y (default 0,0). Returns { layerId }.',
  schema: {
    type: 'object',
    additionalProperties: false,
    required: ['resourceId'],
    properties: {
      resourceId: { type: 'string', minLength: 1 },
      name: { type: 'string', maxLength: 255 },
      x: { type: 'integer' },
      y: { type: 'integer' },
      ...placementParams,
    },
  },
  execute(doc, p, ctx) {
    const img = ctx.resources.get(p.resourceId);
    if (!img) throw new CommandError(`Unknown image resource: ${p.resourceId}`);
    const { parentId, index } = resolveInsertion(doc, p);
    const id = ctx.newId('layer');
    const tiles = blitImage(emptyGrid(doc.width, doc.height, 4), img.pixels, img.width, img.height, p.x ?? 0, p.y ?? 0);
    const layer = createPixelLayer(doc, id, p.name ?? nextLayerName(doc.layers, 'Layer'), tiles);
    return new PatchBuilder(doc).insertLayer(parentId, index, layer).build({ layerId: id });
  },
});

export const LAYER_COMMANDS = [
  createLayer,
  deleteLayer,
  duplicateLayer,
  moveLayer,
  renameLayer,
  setVisible,
  setLocked,
  setOpacity,
  setFill,
  setBlendMode,
  groupLayers,
  ungroupLayers,
  fillRectCommand,
  placeImage,
];
