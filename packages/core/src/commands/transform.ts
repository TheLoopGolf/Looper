import { defineCommand, type Command } from '../command';
import { CommandError, type Document, type LayerNode, type Mask, type ShapeGeometry } from '../document';
import { PatchBuilder } from '../patch';
import { apply, invert, multiply, resampleGrid, rotate, translate, type Affine } from '../raster/transform';
import { gridBounds, readGrid } from '../raster/region';
import type { TileGrid } from '../tiles';
import { layerIdParam, requireLayer, requireUnlocked } from './common';

function transformShape(s: ShapeGeometry, m: Affine): ShapeGeometry {
  const scale = Math.sqrt(Math.abs(m[0] * m[3] - m[1] * m[2]));
  const isAxisAligned = m[1] === 0 && m[2] === 0;
  switch (s.kind) {
    case 'rect': {
      if (isAxisAligned) {
        const [x, y] = apply(m, s.x, s.y);
        return { ...s, x, y, width: s.width * m[0], height: s.height * m[3], radius: s.radius * scale };
      }
      const pts = [apply(m, s.x, s.y), apply(m, s.x + s.width, s.y), apply(m, s.x + s.width, s.y + s.height), apply(m, s.x, s.y + s.height)];
      return { kind: 'path', points: pts.flat(), closed: true };
    }
    case 'ellipse': {
      if (isAxisAligned) {
        const [cx, cy] = apply(m, s.cx, s.cy);
        return { ...s, cx, cy, rx: s.rx * Math.abs(m[0]), ry: s.ry * Math.abs(m[3]) };
      }
      const pts: number[] = [];
      for (let i = 0; i < 128; i++) {
        const a = (i / 128) * Math.PI * 2;
        pts.push(...apply(m, s.cx + Math.cos(a) * s.rx, s.cy + Math.sin(a) * s.ry));
      }
      return { kind: 'path', points: pts, closed: true };
    }
    case 'polygon': {
      const [cx, cy] = apply(m, s.cx, s.cy);
      const rot = (Math.atan2(m[1], m[0]) * 180) / Math.PI;
      return { ...s, cx, cy, radius: s.radius * scale, rotation: s.rotation + rot };
    }
    case 'line': {
      const [x1, y1] = apply(m, s.x1, s.y1);
      const [x2, y2] = apply(m, s.x2, s.y2);
      return { ...s, x1, y1, x2, y2 };
    }
    case 'path': {
      const points: number[] = [];
      for (let i = 0; i < s.points.length; i += 2) points.push(...apply(m, s.points[i], s.points[i + 1]));
      return { ...s, points };
    }
  }
}

/** Transforms one layer's content (pixels resampled, vectors exact). Groups recurse. */
function transformLayer(layer: LayerNode, m: Affine, outW: number, outH: number, interp: 'bilinear' | 'nearest'): LayerNode {
  // Masks always follow their layer (unlinked masks are a later refinement).
  const mask = (mk: Mask | null): Mask | null => mk && { ...mk, tiles: resampleGrid(mk.tiles, m, outW, outH, mk.defaultValue, interp) };
  switch (layer.type) {
    case 'pixel':
    case 'ai':
    case 'smart':
      return { ...layer, tiles: resampleGrid(layer.tiles, m, outW, outH, 0, interp), mask: mask(layer.mask) } as LayerNode;
    case 'shape': {
      const scale = Math.sqrt(Math.abs(m[0] * m[3] - m[1] * m[2]));
      return { ...layer, shape: transformShape(layer.shape, m), strokeWidth: layer.strokeWidth * scale, mask: mask(layer.mask) };
    }
    case 'text': {
      const [x, y] = apply(m, layer.x, layer.y);
      const scale = Math.sqrt(Math.abs(m[0] * m[3] - m[1] * m[2]));
      return {
        ...layer,
        x,
        y,
        boxWidth: layer.boxWidth && layer.boxWidth * scale,
        style: { ...layer.style, fontSize: layer.style.fontSize * scale, letterSpacing: layer.style.letterSpacing * scale },
        mask: mask(layer.mask),
      };
    }
    case 'group':
      return { ...layer, children: layer.children.map((c) => transformLayer(c, m, outW, outH, interp)), mask: mask(layer.mask) };
    case 'adjustment':
      return { ...layer, mask: mask(layer.mask) };
  }
}

export const translateLayer = defineCommand<{ layerId: string; dx: number; dy: number }>({
  id: 'layer.translate',
  title: 'Move',
  category: 'Edit',
  agentDescription: 'Move a layer (or group) by whole pixels. Content moved outside the canvas is clipped.',
  schema: {
    type: 'object',
    additionalProperties: false,
    required: ['layerId', 'dx', 'dy'],
    properties: { layerId: layerIdParam, dx: { type: 'integer', title: 'Δx' }, dy: { type: 'integer', title: 'Δy' } },
  },
  execute(doc, p) {
    const loc = requireLayer(doc, p.layerId);
    requireUnlocked(loc, 'move');
    if (p.dx === 0 && p.dy === 0) return { ops: [] };
    return new PatchBuilder(doc).replaceLayer(transformLayer(loc.layer, translate(p.dx, p.dy), doc.width, doc.height, 'nearest')).build();
  },
});

export const transformLayerCommand = defineCommand<{ layerId: string; matrix: number[]; interpolation?: 'bilinear' | 'nearest' }>({
  id: 'layer.transform',
  title: 'Free Transform',
  category: 'Edit',
  agentDescription:
    'Scale/rotate/skew a layer with an affine matrix [a, b, c, d, e, f] in document pixels (x\' = a·x + c·y + e, y\' = b·x + d·y + f). ' +
    'Pixels are resampled (bilinear); text and shapes stay editable.',
  schema: {
    type: 'object',
    additionalProperties: false,
    required: ['layerId', 'matrix'],
    properties: {
      layerId: layerIdParam,
      matrix: { type: 'array', minItems: 6, maxItems: 6, items: { type: 'number' }, title: 'Matrix' },
      interpolation: { type: 'string', enum: ['bilinear', 'nearest'], default: 'bilinear', title: 'Interpolation' },
    },
  },
  execute(doc, p) {
    const loc = requireLayer(doc, p.layerId);
    requireUnlocked(loc, 'transform');
    const m = p.matrix as unknown as Affine;
    invert(m); // throws a readable error for degenerate matrices
    return new PatchBuilder(doc).replaceLayer(transformLayer(loc.layer, m, doc.width, doc.height, p.interpolation ?? 'bilinear')).build();
  },
});

export const flipLayer = defineCommand<{ layerId: string; axis: 'horizontal' | 'vertical' }>({
  id: 'layer.flip',
  title: 'Flip',
  category: 'Edit',
  agentDescription: 'Mirror a layer horizontally or vertically about the canvas center.',
  schema: {
    type: 'object',
    additionalProperties: false,
    required: ['layerId', 'axis'],
    properties: { layerId: layerIdParam, axis: { type: 'string', enum: ['horizontal', 'vertical'], title: 'Axis' } },
  },
  describe: (p) => (p.axis === 'horizontal' ? 'Flip Horizontal' : 'Flip Vertical'),
  execute(doc, p) {
    const loc = requireLayer(doc, p.layerId);
    requireUnlocked(loc, 'flip');
    const m: Affine = p.axis === 'horizontal' ? [-1, 0, 0, 1, doc.width, 0] : [1, 0, 0, -1, 0, doc.height];
    return new PatchBuilder(doc).replaceLayer(transformLayer(loc.layer, m, doc.width, doc.height, 'nearest')).build();
  },
});

/** Crops/rotates the whole document: every layer, mask, selection and channel. */
function reframe(doc: Document, m: Affine, width: number, height: number): PatchBuilder {
  const b = new PatchBuilder(doc);
  for (const layer of [...doc.layers]) b.replaceLayer(transformLayer(layer, m, width, height, 'bilinear'));
  const regrid = (g: TileGrid) => resampleGrid(g, m, width, height, 0, 'bilinear');
  b.setDocProps({
    width,
    height,
    selection: null,
    channels: doc.channels.map((c) => ({ ...c, mask: regrid(c.mask) })),
    guides: [],
  });
  return b;
}

export const cropDocument = defineCommand<{ x: number; y: number; width: number; height: number; angle?: number }>({
  id: 'document.crop',
  title: 'Crop',
  category: 'Image',
  agentDescription:
    'Crop the canvas to a rectangle (document pixels). angle (degrees) straightens: the image is rotated about the crop center by -angle before cropping. Clears the selection.',
  destructive: true,
  schema: {
    type: 'object',
    additionalProperties: false,
    required: ['x', 'y', 'width', 'height'],
    properties: {
      x: { type: 'integer', title: 'X' },
      y: { type: 'integer', title: 'Y' },
      width: { type: 'integer', minimum: 1, maximum: 30000, title: 'Width' },
      height: { type: 'integer', minimum: 1, maximum: 30000, title: 'Height' },
      angle: { type: 'number', minimum: -45, maximum: 45, default: 0, title: 'Straighten (°)' },
    },
  },
  execute(doc, p) {
    const angle = p.angle ?? 0;
    let m: Affine = translate(-p.x, -p.y);
    if (angle) {
      const cx = p.x + p.width / 2, cy = p.y + p.height / 2;
      // Rotate the image by -angle around the crop center, then crop.
      m = multiply(translate(p.width / 2, p.height / 2), multiply(rotate(-angle), translate(-cx, -cy)));
    }
    return reframe(doc, m, p.width, p.height).build();
  },
});

export const cropToSelection = defineCommand<Record<string, never>>({
  id: 'document.cropToSelection',
  title: 'Crop to Selection',
  category: 'Image',
  agentDescription: "Crop the canvas to the selection's bounding box.",
  destructive: true,
  schema: { type: 'object', additionalProperties: false, properties: {} },
  execute(doc) {
    if (!doc.selection) throw new CommandError('Nothing is selected');
    // gridBounds is tile-granular; find exact pixel bounds.
    const coarse = gridBounds(doc.selection.mask)!;
    const r = readGrid(doc.selection.mask, coarse);
    let x0 = Infinity, y0 = Infinity, x1 = -1, y1 = -1;
    for (let y = 0; y < r.height; y++) for (let x = 0; x < r.width; x++) if (r.data[y * r.width + x]) {
      x0 = Math.min(x0, x); x1 = Math.max(x1, x); y0 = Math.min(y0, y); y1 = Math.max(y1, y);
    }
    if (x1 < 0) throw new CommandError('Nothing is selected');
    const width = x1 - x0 + 1, height = y1 - y0 + 1;
    return reframe(doc, translate(-(coarse.x + x0), -(coarse.y + y0)), width, height).build();
  },
});

export const resizeCanvas = defineCommand<{ width: number; height: number; anchor?: string }>({
  id: 'document.resizeCanvas',
  title: 'Canvas Size',
  category: 'Image',
  agentDescription: 'Change the canvas size without scaling content. anchor: one of tl, t, tr, l, c, r, bl, b, br (default c).',
  destructive: true,
  schema: {
    type: 'object',
    additionalProperties: false,
    required: ['width', 'height'],
    properties: {
      width: { type: 'integer', minimum: 1, maximum: 30000, title: 'Width' },
      height: { type: 'integer', minimum: 1, maximum: 30000, title: 'Height' },
      anchor: { type: 'string', enum: ['tl', 't', 'tr', 'l', 'c', 'r', 'bl', 'b', 'br'], default: 'c', title: 'Anchor' },
    },
  },
  execute(doc, p) {
    const a = p.anchor ?? 'c';
    const fx = a.includes('l') ? 0 : a.includes('r') ? 1 : 0.5;
    const fy = a.startsWith('t') ? 0 : a.startsWith('b') ? 1 : 0.5;
    const dx = Math.round((p.width - doc.width) * fx), dy = Math.round((p.height - doc.height) * fy);
    return reframe(doc, translate(dx, dy), p.width, p.height).build();
  },
});

export const resizeImage = defineCommand<{ width: number; height: number }>({
  id: 'document.resizeImage',
  title: 'Image Size',
  category: 'Image',
  agentDescription: 'Scale the whole image (all layers) to a new pixel size.',
  destructive: true,
  schema: {
    type: 'object',
    additionalProperties: false,
    required: ['width', 'height'],
    properties: { width: { type: 'integer', minimum: 1, maximum: 30000, title: 'Width' }, height: { type: 'integer', minimum: 1, maximum: 30000, title: 'Height' } },
  },
  execute(doc, p) {
    return reframe(doc, [p.width / doc.width, 0, 0, p.height / doc.height, 0, 0], p.width, p.height).build();
  },
});

export const TRANSFORM_COMMANDS: Command<any>[] = [translateLayer, transformLayerCommand, flipLayer, cropDocument, cropToSelection, resizeCanvas, resizeImage];

