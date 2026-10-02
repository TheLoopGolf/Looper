import { defineCommand, type Command } from '../command';
import { renderDocumentCPU } from '../composite/cpu';
import { CommandError, type Document } from '../document';
import { PatchBuilder } from '../patch';
import { layerPixels } from '../raster/layer-pixels';
import {
  blurCoverage,
  combineMasks,
  ellipsePath,
  fullMaskGrid,
  gridBounds,
  invertMask,
  morph,
  padRect,
  rasterizePaths,
  readGrid,
  regionToMask,
  type CombineMode,
  type Region,
} from '../raster/region';
import { colorRangeRegion, similarRegion } from '../raster/paint';
import type { JSONSchema } from '../schema';
import type { TileGrid } from '../tiles';
import { colorParam, layerIdParam, modeParam, requireLayer } from './common';

const docRect = (doc: Document) => ({ x: 0, y: 0, width: doc.width, height: doc.height });

/** Applies a new selection area with a combine mode (and optional feather). Empty results clear the selection. */
function setSelection(doc: Document, region: Region | null, mode: CombineMode, feather = 0): PatchBuilder {
  let r = region;
  if (r && feather > 0) {
    const padded = padRect(r, Math.ceil(feather * 1.5) + 2, docRect(doc))!;
    const grown = readGrid(regionToMask(r, doc.width, doc.height), padded);
    r = blurCoverage(grown, feather / 2);
  }
  const next = combineMasks(doc.selection?.mask ?? null, regionToMask(r, doc.width, doc.height), mode);
  return new PatchBuilder(doc).setDocProps({ selection: next.tiles.size ? { mask: next } : null });
}

const featherParam: JSONSchema = { type: 'number', minimum: 0, maximum: 250, default: 0, title: 'Feather (px)', description: 'Soften the edge by this radius.' };
const aaParam: JSONSchema = { type: 'boolean', default: true, title: 'Anti-alias' };

type Shape = { x: number; y: number; width: number; height: number; mode?: CombineMode; feather?: number; antiAlias?: boolean };
const rectSchema = {
  x: { type: 'number', title: 'X' },
  y: { type: 'number', title: 'Y' },
  width: { type: 'number', minimum: 0, title: 'Width' },
  height: { type: 'number', minimum: 0, title: 'Height' },
  mode: modeParam,
  feather: featherParam,
  antiAlias: aaParam,
} as const satisfies Record<string, JSONSchema>;

export const selectRect = defineCommand<Shape>({
  id: 'selection.rect',
  title: 'Rectangular Marquee',
  category: 'Select',
  agentDescription: 'Select a rectangle (document pixels). mode: replace/add/subtract/intersect. Optional feather softens the edge.',
  schema: { type: 'object', additionalProperties: false, required: ['x', 'y', 'width', 'height'], properties: rectSchema },
  execute(doc, p) {
    const path = [p.x, p.y, p.x + p.width, p.y, p.x + p.width, p.y + p.height, p.x, p.y + p.height];
    const r = p.width > 0 && p.height > 0 ? rasterizePaths([path], docRect(doc), 'nonzero', p.antiAlias ?? true) : null;
    return setSelection(doc, r, p.mode ?? 'replace', p.feather).build();
  },
});

export const selectEllipse = defineCommand<Shape>({
  id: 'selection.ellipse',
  title: 'Elliptical Marquee',
  category: 'Select',
  agentDescription: 'Select the ellipse inscribed in a rectangle (document pixels). mode: replace/add/subtract/intersect.',
  schema: { type: 'object', additionalProperties: false, required: ['x', 'y', 'width', 'height'], properties: rectSchema },
  execute(doc, p) {
    const path = ellipsePath(p.x + p.width / 2, p.y + p.height / 2, p.width / 2, p.height / 2);
    const r = p.width > 0 && p.height > 0 ? rasterizePaths([path], docRect(doc), 'nonzero', p.antiAlias ?? true) : null;
    return setSelection(doc, r, p.mode ?? 'replace', p.feather).build();
  },
});

export const selectPolygon = defineCommand<{ points: number[]; mode?: CombineMode; feather?: number; antiAlias?: boolean }>({
  id: 'selection.polygon',
  title: 'Lasso',
  category: 'Select',
  agentDescription: 'Select a freehand or polygonal area given as a flat list of document coordinates [x0, y0, x1, y1, …] (closed automatically).',
  schema: {
    type: 'object',
    additionalProperties: false,
    required: ['points'],
    properties: { points: { type: 'array', minItems: 6, items: { type: 'number' }, title: 'Points' }, mode: modeParam, feather: featherParam, antiAlias: aaParam },
  },
  execute(doc, p) {
    if (p.points.length % 2) throw new CommandError('points must contain x,y pairs');
    const r = rasterizePaths([p.points], docRect(doc), 'nonzero', p.antiAlias ?? true);
    return setSelection(doc, r, p.mode ?? 'replace', p.feather).build();
  },
});

/** RGBA pixels to sample: one layer, or the flattened image. */
function samplePixels(doc: Document, layerId: string | undefined, sampleMerged: boolean): Region {
  const rect = docRect(doc);
  if (sampleMerged || !layerId) return { ...rect, channels: 4, data: renderDocumentCPU(doc) };
  const layer = requireLayer(doc, layerId).layer;
  const grid = layerPixels(layer, doc.width, doc.height);
  if (!grid) throw new CommandError(`Layer "${layer.name}" has no pixels to sample; use sampleMerged`);
  return readGrid(grid, rect);
}

export const magicWand = defineCommand<{ x: number; y: number; tolerance?: number; contiguous?: boolean; layerId?: string; sampleMerged?: boolean; mode?: CombineMode }>({
  id: 'selection.magicWand',
  title: 'Magic Wand',
  category: 'Select',
  agentDescription:
    'Select pixels similar in color to the pixel at (x, y). tolerance 0-255 (default 32); contiguous=true only grows from the click point. Samples layerId, or the flattened image with sampleMerged.',
  schema: {
    type: 'object',
    additionalProperties: false,
    required: ['x', 'y'],
    properties: {
      x: { type: 'integer', title: 'X' },
      y: { type: 'integer', title: 'Y' },
      tolerance: { type: 'integer', minimum: 0, maximum: 255, default: 32, title: 'Tolerance' },
      contiguous: { type: 'boolean', default: true, title: 'Contiguous' },
      layerId: layerIdParam,
      sampleMerged: { type: 'boolean', default: false, title: 'Sample all layers' },
      mode: modeParam,
    },
  },
  execute(doc, p) {
    const src = samplePixels(doc, p.layerId, p.sampleMerged ?? false);
    const r = similarRegion(src, p.x, p.y, p.tolerance ?? 32, p.contiguous ?? true);
    return setSelection(doc, r, p.mode ?? 'replace').build();
  },
});

export const colorRange = defineCommand<{ color: number[]; fuzziness?: number; layerId?: string; sampleMerged?: boolean; mode?: CombineMode }>({
  id: 'selection.colorRange',
  title: 'Color Range',
  category: 'Select',
  agentDescription: 'Select everywhere a color appears, with soft falloff controlled by fuzziness (0-255, default 40). Samples layerId or the flattened image.',
  schema: {
    type: 'object',
    additionalProperties: false,
    required: ['color'],
    properties: {
      color: colorParam,
      fuzziness: { type: 'integer', minimum: 0, maximum: 255, default: 40, title: 'Fuzziness' },
      layerId: layerIdParam,
      sampleMerged: { type: 'boolean', default: true, title: 'Sample all layers' },
      mode: modeParam,
    },
  },
  execute(doc, p) {
    const src = samplePixels(doc, p.layerId, p.sampleMerged ?? true);
    return setSelection(doc, colorRangeRegion(src, p.color, p.fuzziness ?? 40), p.mode ?? 'replace').build();
  },
});

export const selectAll = defineCommand<Record<string, never>>({
  id: 'selection.all',
  title: 'Select All',
  category: 'Select',
  agentDescription: 'Select the whole canvas.',
  schema: { type: 'object', additionalProperties: false, properties: {} },
  execute: (doc) => new PatchBuilder(doc).setDocProps({ selection: { mask: fullMaskGrid(doc.width, doc.height) } }).build(),
});

export const deselect = defineCommand<Record<string, never>>({
  id: 'selection.deselect',
  title: 'Deselect',
  category: 'Select',
  agentDescription: 'Clear the selection so edits apply to the whole layer.',
  schema: { type: 'object', additionalProperties: false, properties: {} },
  execute: (doc) => (doc.selection ? new PatchBuilder(doc).setDocProps({ selection: null }).build() : { ops: [] }),
});

export const invertSelection = defineCommand<Record<string, never>>({
  id: 'selection.invert',
  title: 'Inverse',
  category: 'Select',
  agentDescription: 'Invert the selection (selected becomes unselected and vice versa).',
  schema: { type: 'object', additionalProperties: false, properties: {} },
  execute(doc) {
    const inv = invertMask(doc.selection?.mask ?? null, doc.width, doc.height);
    return new PatchBuilder(doc).setDocProps({ selection: inv.tiles.size ? { mask: inv } : null }).build();
  },
});

function modifySelection(doc: Document, pad: number, fn: (r: Region) => Region): PatchBuilder {
  const sel = doc.selection;
  if (!sel) throw new CommandError('Nothing is selected');
  const bounds = gridBounds(sel.mask)!;
  const rect = padRect(bounds, pad, docRect(doc))!;
  const out = fn(readGrid(sel.mask, rect));
  const mask = combineMasks(null, regionToMask(out, doc.width, doc.height), 'replace');
  // Keep tiles outside the processed rect (unchanged) — the rect covers every stored tile plus padding.
  return new PatchBuilder(doc).setDocProps({ selection: mask.tiles.size ? { mask } : null });
}

export const featherSelection = defineCommand<{ radius: number }>({
  id: 'selection.feather',
  title: 'Feather Selection',
  category: 'Select',
  agentDescription: 'Soften the selection edge by a radius in pixels (gaussian).',
  schema: { type: 'object', additionalProperties: false, required: ['radius'], properties: { radius: { type: 'number', minimum: 0.1, maximum: 250, default: 5, title: 'Radius (px)' } } },
  execute: (doc, p) => modifySelection(doc, Math.ceil(p.radius * 1.5) + 2, (r) => blurCoverage(r, p.radius / 2)).build(),
});

export const expandSelection = defineCommand<{ pixels: number }>({
  id: 'selection.expand',
  title: 'Expand Selection',
  category: 'Select',
  agentDescription: 'Grow the selection outward by a number of pixels (round corners).',
  schema: { type: 'object', additionalProperties: false, required: ['pixels'], properties: { pixels: { type: 'integer', minimum: 1, maximum: 500, default: 5, title: 'Expand by (px)' } } },
  execute: (doc, p) => modifySelection(doc, p.pixels + 1, (r) => morph(r, p.pixels)).build(),
});

export const contractSelection = defineCommand<{ pixels: number }>({
  id: 'selection.contract',
  title: 'Contract Selection',
  category: 'Select',
  agentDescription: 'Shrink the selection inward by a number of pixels.',
  schema: { type: 'object', additionalProperties: false, required: ['pixels'], properties: { pixels: { type: 'integer', minimum: 1, maximum: 500, default: 5, title: 'Contract by (px)' } } },
  execute: (doc, p) => modifySelection(doc, 1, (r) => morph(r, -p.pixels)).build(),
});

export const selectionFromLayer = defineCommand<{ layerId: string; mode?: CombineMode }>({
  id: 'selection.fromLayer',
  title: 'Load Layer Transparency',
  category: 'Select',
  agentDescription: "Select a layer's opaque pixels (its alpha channel), e.g. to select a cut-out object.",
  schema: { type: 'object', additionalProperties: false, required: ['layerId'], properties: { layerId: layerIdParam, mode: modeParam } },
  execute(doc, p) {
    const layer = requireLayer(doc, p.layerId).layer;
    const grid = layerPixels(layer, doc.width, doc.height);
    if (!grid) throw new CommandError(`Layer "${layer.name}" has no pixels`);
    const bounds = gridBounds(grid);
    let region: Region | null = null;
    if (bounds) {
      const rgba = readGrid(grid, bounds);
      const data = new Uint8ClampedArray(bounds.width * bounds.height);
      for (let i = 0; i < data.length; i++) data[i] = rgba.data[i * 4 + 3];
      region = { ...bounds, channels: 1, data };
    }
    return setSelection(doc, region, p.mode ?? 'replace').build();
  },
});

export const saveSelection = defineCommand<{ name?: string }>({
  id: 'selection.save',
  title: 'Save Selection',
  category: 'Select',
  agentDescription: 'Save the current selection as a named channel to reload later. Returns { channelId }.',
  schema: { type: 'object', additionalProperties: false, properties: { name: { type: 'string', maxLength: 255, title: 'Name' } } },
  execute(doc, p, ctx) {
    if (!doc.selection) throw new CommandError('Nothing is selected');
    const id = ctx.newId('channel');
    const name = p.name ?? `Alpha ${doc.channels.length + 1}`;
    return new PatchBuilder(doc).setDocProps({ channels: [...doc.channels, { id, name, mask: doc.selection.mask }] }).build({ channelId: id });
  },
});

export const loadSelection = defineCommand<{ channelId: string; mode?: CombineMode; invert?: boolean }>({
  id: 'selection.load',
  title: 'Load Selection',
  category: 'Select',
  agentDescription: 'Load a saved selection channel, combining it with the current selection by mode.',
  schema: {
    type: 'object',
    additionalProperties: false,
    required: ['channelId'],
    properties: { channelId: { type: 'string', title: 'Channel' }, mode: modeParam, invert: { type: 'boolean', default: false, title: 'Invert' } },
  },
  execute(doc, p) {
    const ch = doc.channels.find((c) => c.id === p.channelId);
    if (!ch) throw new CommandError(`Channel not found: ${p.channelId}`);
    const src: TileGrid = p.invert ? invertMask(ch.mask, doc.width, doc.height) : ch.mask;
    const next = combineMasks(doc.selection?.mask ?? null, src, p.mode ?? 'replace');
    return new PatchBuilder(doc).setDocProps({ selection: next.tiles.size ? { mask: next } : null }).build();
  },
});

export const deleteChannel = defineCommand<{ channelId: string }>({
  id: 'channel.delete',
  title: 'Delete Channel',
  category: 'Select',
  agentDescription: 'Delete a saved selection channel.',
  destructive: true,
  schema: { type: 'object', additionalProperties: false, required: ['channelId'], properties: { channelId: { type: 'string' } } },
  execute(doc, p) {
    if (!doc.channels.some((c) => c.id === p.channelId)) throw new CommandError(`Channel not found: ${p.channelId}`);
    return new PatchBuilder(doc).setDocProps({ channels: doc.channels.filter((c) => c.id !== p.channelId) }).build();
  },
});

export const SELECTION_COMMANDS: Command<any>[] = [
  selectRect,
  selectEllipse,
  selectPolygon,
  magicWand,
  colorRange,
  selectAll,
  deselect,
  invertSelection,
  featherSelection,
  expandSelection,
  contractSelection,
  selectionFromLayer,
  saveSelection,
  loadSelection,
  deleteChannel,
];

