import { defineCommand, type Command } from '../command';
import { renderDocumentCPU } from '../composite/cpu';
import { CommandError, type Document, type LayerNode, type RGBA } from '../document';
import { PatchBuilder } from '../patch';
import { DEFAULT_BRUSH, StrokeAccumulator, unpackPoints, type BrushSettings } from '../raster/brush';
import { applyStroke, fillRegion, gradientRegion, similarRegion, type GradientKind, type GradientStop, type PaintMode } from '../raster/paint';
import { readGrid, writeGrid, type Region } from '../raster/region';
import type { JSONSchema } from '../schema';
import type { Rect } from '../tiles';
import { colorParam, editRect, layerIdParam, requireLayer, requirePixelLayer, requireUnlocked, selectionCoverage } from './common';

const brushSchema: JSONSchema = {
  type: 'object',
  title: 'Brush',
  additionalProperties: false,
  properties: {
    size: { type: 'number', minimum: 1, maximum: 5000, title: 'Size (px)' },
    hardness: { type: 'number', minimum: 0, maximum: 1, title: 'Hardness' },
    opacity: { type: 'number', minimum: 0, maximum: 1, title: 'Opacity' },
    flow: { type: 'number', minimum: 0, maximum: 1, title: 'Flow' },
    spacing: { type: 'number', minimum: 0.01, maximum: 2, title: 'Spacing' },
    pressureSize: { type: 'boolean', title: 'Pressure controls size' },
    pressureOpacity: { type: 'boolean', title: 'Pressure controls opacity' },
  },
};

export type StrokeTarget = 'pixels' | 'mask' | 'selection';

export interface StrokeParams {
  layerId?: string;
  target?: StrokeTarget;
  mode?: PaintMode;
  color?: number[];
  brush?: Partial<BrushSettings>;
  points: number[];
}

/** Grid a stroke paints into, with its missing-tile value. */
export function strokeSource(doc: Document, p: Pick<StrokeParams, 'layerId' | 'target'>): { layer: LayerNode | null; grid: Parameters<typeof applyStroke>[0]; defaultValue: number } {
  const target = p.target ?? 'pixels';
  if (target === 'selection') {
    return { layer: null, grid: doc.selection?.mask ?? { width: doc.width, height: doc.height, channels: 1, tiles: new Map() }, defaultValue: 0 };
  }
  if (!p.layerId) throw new CommandError('layerId is required');
  if (target === 'mask') {
    const loc = requireLayer(doc, p.layerId);
    requireUnlocked(loc, 'paint the mask');
    if (!loc.layer.mask) throw new CommandError(`Layer "${loc.layer.name}" has no mask; add one with layer.addMask`);
    return { layer: loc.layer, grid: loc.layer.mask.tiles, defaultValue: loc.layer.mask.defaultValue };
  }
  const layer = requirePixelLayer(doc, p.layerId, 'paint');
  return { layer, grid: layer.tiles, defaultValue: 0 };
}

export const paintStroke = defineCommand<StrokeParams>({
  id: 'paint.stroke',
  title: 'Brush Stroke',
  category: 'Paint',
  agentDescription:
    'Paint (or erase with mode="erase") a brush stroke along points given as a flat [x, y, pressure, …] list (pressure 0-1). ' +
    'target: "pixels" (default), "mask" (paints the layer mask with the color\'s grey value) or "selection" (quick mask). Limited to the selection.',
  destructive: true,
  schema: {
    type: 'object',
    additionalProperties: false,
    required: ['points'],
    properties: {
      layerId: layerIdParam,
      target: { type: 'string', enum: ['pixels', 'mask', 'selection'], default: 'pixels', title: 'Target' },
      mode: { type: 'string', enum: ['paint', 'erase'], default: 'paint', title: 'Mode' },
      color: colorParam,
      brush: brushSchema,
      points: { type: 'array', minItems: 3, items: { type: 'number' }, title: 'Points' },
    },
  },
  describe: (p) => (p.mode === 'erase' ? 'Eraser' : p.target === 'mask' ? 'Paint Mask' : p.target === 'selection' ? 'Quick Mask' : 'Brush Stroke'),
  execute(doc, p) {
    const target = p.target ?? 'pixels';
    const { layer, grid, defaultValue } = strokeSource(doc, p);
    const acc = new StrokeAccumulator({ ...DEFAULT_BRUSH, ...p.brush }, doc.width, doc.height);
    const keys = acc.addPoints(unpackPoints(p.points));
    const out = applyStroke(grid, acc, keys, {
      mode: p.mode ?? 'paint',
      color: (p.color ?? [0, 0, 0, 255]) as unknown as RGBA,
      selection: target === 'selection' ? null : (doc.selection?.mask ?? null),
      defaultValue,
      // Quick mask: painting adds to the selection, erasing removes from it.
      value: target === 'selection' ? 255 : undefined,
    });
    const b = new PatchBuilder(doc);
    if (target === 'selection') b.setDocProps({ selection: out.tiles.size ? { mask: out } : null });
    else if (target === 'mask') b.replaceLayer({ ...layer!, mask: { ...layer!.mask!, tiles: out } } as LayerNode);
    else b.replaceLayer({ ...layer!, tiles: out } as LayerNode);
    return b.build();
  },
});

/** Runs an RGBA region edit on a pixel layer within the selection's bounds. */
function editPixels(doc: Document, layerId: string, action: string, fn: (region: Region, coverage: Region | null, rect: Rect) => void): PatchBuilder {
  const layer = requirePixelLayer(doc, layerId, action);
  const rect = editRect(doc);
  const b = new PatchBuilder(doc);
  if (!rect) return b;
  const region = readGrid(layer.tiles, rect);
  fn(region, selectionCoverage(doc, rect), rect);
  return b.replaceLayer({ ...layer, tiles: writeGrid(layer.tiles, region) });
}

export const fillPixels = defineCommand<{ layerId: string; color: number[]; opacity?: number }>({
  id: 'pixels.fill',
  title: 'Fill',
  category: 'Edit',
  agentDescription: 'Fill the selection (or the whole layer if nothing is selected) with a color, composited over existing pixels. opacity 0-100.',
  destructive: true,
  schema: {
    type: 'object',
    additionalProperties: false,
    required: ['layerId', 'color'],
    properties: { layerId: layerIdParam, color: colorParam, opacity: { type: 'number', minimum: 0, maximum: 100, default: 100, title: 'Opacity (%)' } },
  },
  execute: (doc, p) => editPixels(doc, p.layerId, 'fill', (r, cov) => fillRegion(r, cov, p.color as unknown as RGBA, (p.opacity ?? 100) / 100)).build(),
});

export const clearPixels = defineCommand<{ layerId: string }>({
  id: 'pixels.clear',
  title: 'Clear',
  category: 'Edit',
  agentDescription: 'Delete the selected pixels of a layer (make them transparent). Without a selection clears the whole layer.',
  destructive: true,
  schema: { type: 'object', additionalProperties: false, required: ['layerId'], properties: { layerId: layerIdParam } },
  execute: (doc, p) =>
    editPixels(doc, p.layerId, 'clear', (r, cov) => {
      for (let i = 0, k = 0; i < r.data.length; i += 4, k++) r.data[i + 3] = Math.round(r.data[i + 3] * (1 - (cov ? cov.data[k] / 255 : 1)));
    }).build(),
});

export const floodFill = defineCommand<{ layerId: string; x: number; y: number; color: number[]; tolerance?: number; contiguous?: boolean; sampleMerged?: boolean; opacity?: number }>({
  id: 'pixels.floodFill',
  title: 'Paint Bucket',
  category: 'Edit',
  agentDescription: 'Fill the area of similar color around (x, y) with a color (paint bucket). tolerance 0-255, contiguous by default; limited to the selection.',
  destructive: true,
  schema: {
    type: 'object',
    additionalProperties: false,
    required: ['layerId', 'x', 'y', 'color'],
    properties: {
      layerId: layerIdParam,
      x: { type: 'integer' },
      y: { type: 'integer' },
      color: colorParam,
      tolerance: { type: 'integer', minimum: 0, maximum: 255, default: 32, title: 'Tolerance' },
      contiguous: { type: 'boolean', default: true, title: 'Contiguous' },
      sampleMerged: { type: 'boolean', default: false, title: 'Sample all layers' },
      opacity: { type: 'number', minimum: 0, maximum: 100, default: 100, title: 'Opacity (%)' },
    },
  },
  execute(doc, p) {
    const layer = requirePixelLayer(doc, p.layerId, 'fill');
    const full = { x: 0, y: 0, width: doc.width, height: doc.height };
    const sample: Region = p.sampleMerged ? { ...full, channels: 4, data: renderDocumentCPU(doc) } : readGrid(layer.tiles, full);
    const area = similarRegion(sample, p.x, p.y, p.tolerance ?? 32, p.contiguous ?? true);
    const sel = selectionCoverage(doc, full);
    if (sel) for (let i = 0; i < area.data.length; i++) area.data[i] = Math.round((area.data[i] * sel.data[i]) / 255);
    const region = readGrid(layer.tiles, full);
    fillRegion(region, area, p.color as unknown as RGBA, (p.opacity ?? 100) / 100);
    return new PatchBuilder(doc).replaceLayer({ ...layer, tiles: writeGrid(layer.tiles, region) }).build();
  },
});

export const gradientFill = defineCommand<{
  layerId: string;
  kind?: GradientKind;
  x0: number;
  y0: number;
  x1: number;
  y1: number;
  stops?: { offset: number; color: number[] }[];
  colorA?: number[];
  colorB?: number[];
  opacity?: number;
}>({
  id: 'pixels.gradient',
  title: 'Gradient',
  category: 'Edit',
  agentDescription:
    'Draw a gradient from (x0, y0) to (x1, y1) over the selection (or layer). kind: linear, radial, angle, reflected, diamond. ' +
    'Colors from colorA→colorB or explicit stops [{offset 0-1, color}]. opacity 0-100.',
  destructive: true,
  schema: {
    type: 'object',
    additionalProperties: false,
    required: ['layerId', 'x0', 'y0', 'x1', 'y1'],
    properties: {
      layerId: layerIdParam,
      kind: { type: 'string', enum: ['linear', 'radial', 'angle', 'reflected', 'diamond'], default: 'linear', title: 'Type' },
      x0: { type: 'number' },
      y0: { type: 'number' },
      x1: { type: 'number' },
      y1: { type: 'number' },
      stops: { type: 'array', minItems: 2, items: { type: 'object', required: ['offset', 'color'], properties: { offset: { type: 'number', minimum: 0, maximum: 1 }, color: colorParam } } },
      colorA: colorParam,
      colorB: colorParam,
      opacity: { type: 'number', minimum: 0, maximum: 100, default: 100, title: 'Opacity (%)' },
    },
  },
  execute(doc, p) {
    const stops: GradientStop[] = (p.stops ?? [
      { offset: 0, color: p.colorA ?? [0, 0, 0, 255] },
      { offset: 1, color: p.colorB ?? [255, 255, 255, 255] },
    ]).map((s) => ({ offset: s.offset, color: s.color as unknown as RGBA }));
    return editPixels(doc, p.layerId, 'draw a gradient', (r, cov) =>
      gradientRegion(r, cov, { kind: p.kind ?? 'linear', x0: p.x0, y0: p.y0, x1: p.x1, y1: p.y1, stops, opacity: (p.opacity ?? 100) / 100 }),
    ).build();
  },
});

export const PAINT_COMMANDS: Command<any>[] = [paintStroke, fillPixels, clearPixels, floodFill, gradientFill];

