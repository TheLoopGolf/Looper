import { defineCommand, type Command } from '../command';
import { CommandError, createPixelLayer, createShapeLayer, createTextLayer, type RGBA, type ShapeGeometry, type TextStyle } from '../document';
import { PatchBuilder } from '../patch';
import { layerPixels } from '../raster/layer-pixels';
import { hasTextRasterizer } from '../raster/shapes';
import type { JSONSchema } from '../schema';
import { colorParam, layerIdParam, placementParams, requireLayer, requireUnlocked, resolveInsertion } from './common';

const nullableColor: JSONSchema = { anyOf: [colorParam, { type: 'null' }], description: 'RGBA color, or null for none.' };

const shapeParam: JSONSchema = {
  type: 'object',
  title: 'Shape',
  required: [],
  properties: {
    kind: { type: 'string', enum: ['rect', 'ellipse', 'polygon', 'line', 'path'] },
    x: { type: 'number' }, y: { type: 'number' }, width: { type: 'number' }, height: { type: 'number' }, radius: { type: 'number', minimum: 0 },
    cx: { type: 'number' }, cy: { type: 'number' }, rx: { type: 'number' }, ry: { type: 'number' },
    sides: { type: 'integer', minimum: 3, maximum: 100 }, rotation: { type: 'number' }, star: { type: 'number', minimum: 0.05, maximum: 1 },
    x1: { type: 'number' }, y1: { type: 'number' }, x2: { type: 'number' }, y2: { type: 'number' },
    points: { type: 'array', items: { type: 'number' } }, closed: { type: 'boolean' },
  },
  description:
    'rect {x,y,width,height,radius}; ellipse {cx,cy,rx,ry}; polygon {cx,cy,radius,sides,rotation,star?}; line {x1,y1,x2,y2}; path {points:[x,y,…],closed}.',
};

const REQUIRED: Record<ShapeGeometry['kind'], string[]> = {
  rect: ['x', 'y', 'width', 'height'],
  ellipse: ['cx', 'cy', 'rx', 'ry'],
  polygon: ['cx', 'cy', 'radius', 'sides'],
  line: ['x1', 'y1', 'x2', 'y2'],
  path: ['points'],
};

function normalizeShape(s: Record<string, unknown>): ShapeGeometry {
  const kind = s.kind as ShapeGeometry['kind'];
  if (!(kind in REQUIRED)) throw new CommandError('shape.kind must be rect, ellipse, polygon, line or path');
  const missing = REQUIRED[kind].filter((k) => s[k] === undefined);
  if (missing.length) throw new CommandError(`shape (${kind}) is missing ${missing.join(', ')}`);
  switch (kind) {
    case 'rect':
      return { kind, x: s.x as number, y: s.y as number, width: s.width as number, height: s.height as number, radius: (s.radius as number) ?? 0 };
    case 'ellipse':
      return { kind, cx: s.cx as number, cy: s.cy as number, rx: s.rx as number, ry: s.ry as number };
    case 'polygon':
      return { kind, cx: s.cx as number, cy: s.cy as number, radius: s.radius as number, sides: s.sides as number, rotation: (s.rotation as number) ?? 0, ...(s.star !== undefined ? { star: s.star as number } : {}) };
    case 'line':
      return { kind, x1: s.x1 as number, y1: s.y1 as number, x2: s.x2 as number, y2: s.y2 as number };
    case 'path': {
      const points = s.points as number[];
      if (points.length < 4 || points.length % 2) throw new CommandError('path points must be x,y pairs (at least two points)');
      return { kind, points, closed: (s.closed as boolean) ?? true };
    }
  }
}

export const createShape = defineCommand<{ shape: Record<string, unknown>; fillColor?: number[] | null; strokeColor?: number[] | null; strokeWidth?: number; name?: string } & Record<string, unknown>>({
  id: 'layer.createShape',
  title: 'New Shape',
  category: 'Layer',
  agentDescription: 'Add a vector shape layer (stays sharp and editable). fillColor and strokeColor are RGBA colors or null. Returns { layerId }.',
  schema: {
    type: 'object',
    additionalProperties: false,
    required: ['shape'],
    properties: {
      shape: shapeParam,
      fillColor: nullableColor,
      strokeColor: nullableColor,
      strokeWidth: { type: 'number', minimum: 0, maximum: 1000, default: 0, title: 'Stroke width (px)' },
      name: { type: 'string', maxLength: 255 },
      ...placementParams,
    },
  },
  execute(doc, p, ctx) {
    const shape = normalizeShape(p.shape);
    const id = ctx.newId('shape');
    const label = { rect: 'Rectangle', ellipse: 'Ellipse', polygon: 'Polygon', line: 'Line', path: 'Path' }[shape.kind];
    const layer = createShapeLayer(id, p.name ?? label, {
      shape,
      fillColor: p.fillColor === undefined ? [0, 0, 0, 255] : (p.fillColor as RGBA | null),
      strokeColor: (p.strokeColor as RGBA | null) ?? null,
      strokeWidth: p.strokeWidth ?? (p.strokeColor ? 2 : 0),
    });
    const { parentId, index } = resolveInsertion(doc, p as never);
    return new PatchBuilder(doc).insertLayer(parentId, index, layer).build({ layerId: id });
  },
});

export const updateShape = defineCommand<{ layerId: string; shape?: Record<string, unknown>; fillColor?: number[] | null; strokeColor?: number[] | null; strokeWidth?: number }>({
  id: 'layer.updateShape',
  title: 'Edit Shape',
  category: 'Layer',
  agentDescription: "Change a shape layer's geometry, fill, stroke or stroke width.",
  schema: {
    type: 'object',
    additionalProperties: false,
    required: ['layerId'],
    properties: { layerId: layerIdParam, shape: shapeParam, fillColor: nullableColor, strokeColor: nullableColor, strokeWidth: { type: 'number', minimum: 0, maximum: 1000, title: 'Stroke width (px)' } },
  },
  coalesceKey: (p) => `shape:${p.layerId}`,
  execute(doc, p) {
    const loc = requireLayer(doc, p.layerId);
    requireUnlocked(loc, 'edit the shape');
    const l = loc.layer;
    if (l.type !== 'shape') throw new CommandError(`Layer "${l.name}" is not a shape layer`);
    const next = {
      ...l,
      ...(p.shape ? { shape: normalizeShape({ ...l.shape, ...p.shape }) } : {}),
      ...(p.fillColor !== undefined ? { fillColor: p.fillColor as RGBA | null } : {}),
      ...(p.strokeColor !== undefined ? { strokeColor: p.strokeColor as RGBA | null } : {}),
      ...(p.strokeWidth !== undefined ? { strokeWidth: p.strokeWidth } : {}),
    };
    return new PatchBuilder(doc).replaceLayer(next).build();
  },
});

const styleParam: JSONSchema = {
  type: 'object',
  title: 'Text style',
  additionalProperties: false,
  properties: {
    fontFamily: { type: 'string', title: 'Font' },
    fontSize: { type: 'number', minimum: 1, maximum: 2000, title: 'Size (px)' },
    fontWeight: { type: 'integer', minimum: 100, maximum: 900, title: 'Weight' },
    italic: { type: 'boolean', title: 'Italic' },
    color: colorParam,
    align: { type: 'string', enum: ['left', 'center', 'right'], title: 'Align' },
    lineHeight: { type: 'number', minimum: 0.5, maximum: 5, title: 'Line height' },
    letterSpacing: { type: 'number', minimum: -50, maximum: 500, title: 'Tracking (px)' },
  },
};

export const createText = defineCommand<{ text: string; x: number; y: number; boxWidth?: number | null; style?: Partial<TextStyle>; name?: string } & Record<string, unknown>>({
  id: 'layer.createText',
  title: 'New Text',
  category: 'Layer',
  agentDescription: 'Add an editable text layer with its top-left at (x, y). boxWidth wraps lines into a paragraph. Returns { layerId }.',
  schema: {
    type: 'object',
    additionalProperties: false,
    required: ['text', 'x', 'y'],
    properties: {
      text: { type: 'string', minLength: 1, maxLength: 10000, title: 'Text' },
      x: { type: 'number' },
      y: { type: 'number' },
      boxWidth: { anyOf: [{ type: 'number', minimum: 1 }, { type: 'null' }], title: 'Paragraph width' },
      style: styleParam,
      name: { type: 'string', maxLength: 255 },
      ...placementParams,
    },
  },
  execute(doc, p, ctx) {
    const id = ctx.newId('text');
    const layer = createTextLayer(id, p.name ?? p.text.split('\n')[0].slice(0, 40), { text: p.text, x: p.x, y: p.y, boxWidth: p.boxWidth ?? null, style: p.style });
    const { parentId, index } = resolveInsertion(doc, p as never);
    return new PatchBuilder(doc).insertLayer(parentId, index, layer).build({ layerId: id });
  },
});

export const updateText = defineCommand<{ layerId: string; text?: string; x?: number; y?: number; boxWidth?: number | null; style?: Partial<TextStyle> }>({
  id: 'layer.updateText',
  title: 'Edit Text',
  category: 'Layer',
  agentDescription: "Change a text layer's text, position, paragraph width or style (style is merged).",
  schema: {
    type: 'object',
    additionalProperties: false,
    required: ['layerId'],
    properties: {
      layerId: layerIdParam,
      text: { type: 'string', minLength: 1, maxLength: 10000 },
      x: { type: 'number' },
      y: { type: 'number' },
      boxWidth: { anyOf: [{ type: 'number', minimum: 1 }, { type: 'null' }] },
      style: styleParam,
    },
  },
  coalesceKey: (p) => (p.text === undefined ? `text:${p.layerId}` : null),
  execute(doc, p) {
    const loc = requireLayer(doc, p.layerId);
    requireUnlocked(loc, 'edit text');
    const l = loc.layer;
    if (l.type !== 'text') throw new CommandError(`Layer "${l.name}" is not a text layer`);
    const next = {
      ...l,
      ...(p.text !== undefined ? { text: p.text } : {}),
      ...(p.x !== undefined ? { x: p.x } : {}),
      ...(p.y !== undefined ? { y: p.y } : {}),
      ...(p.boxWidth !== undefined ? { boxWidth: p.boxWidth } : {}),
      ...(p.style ? { style: { ...l.style, ...p.style } } : {}),
    };
    return new PatchBuilder(doc).replaceLayer(next).build();
  },
});

export const rasterizeLayer = defineCommand<{ layerId: string }>({
  id: 'layer.rasterize',
  title: 'Rasterize Layer',
  category: 'Layer',
  agentDescription: 'Convert a text or shape layer to plain pixels so it can be painted or filtered. It stops being editable as text/vector.',
  destructive: true,
  schema: { type: 'object', additionalProperties: false, required: ['layerId'], properties: { layerId: layerIdParam } },
  execute(doc, p) {
    const loc = requireLayer(doc, p.layerId);
    requireUnlocked(loc, 'rasterize');
    const l = loc.layer;
    if (l.type !== 'text' && l.type !== 'shape') throw new CommandError(`Layer "${l.name}" is already pixels or can't be rasterized`);
    if (l.type === 'text' && !hasTextRasterizer()) throw new CommandError('Text rendering is not available in this environment');
    const tiles = layerPixels(l, doc.width, doc.height)!;
    const base = createPixelLayer(doc, l.id, l.name, tiles);
    const pixel = { ...base, visible: l.visible, locked: l.locked, opacity: l.opacity, fill: l.fill, blendMode: l.blendMode, mask: l.mask, clipToBelow: l.clipToBelow, effects: l.effects };
    return new PatchBuilder(doc).replaceLayer(pixel).build();
  },
});

export const VECTOR_COMMANDS: Command<any>[] = [createShape, updateShape, createText, updateText, rasterizeLayer];
