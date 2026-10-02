import { CommandError, findLayer, getChildren, type Document, type LayerLocation } from '../document';
import type { JSONSchema } from '../schema';
import type { AIGeneratedLayer, PixelLayer } from '../document';
import { gridBounds, readGrid, type Region } from '../raster/region';
import type { Rect } from '../tiles';

export const layerIdParam: JSONSchema = {
  type: 'string',
  minLength: 1,
  description: 'Id of the target layer, as listed in the document structure.',
};

export const colorParam: JSONSchema = {
  type: 'array',
  minItems: 4,
  maxItems: 4,
  items: { type: 'integer', minimum: 0, maximum: 255 },
  description: 'RGBA color as [r, g, b, a], each 0-255 (a=255 is opaque).',
};

export function requireLayer(doc: Document, layerId: string): LayerLocation {
  const loc = findLayer(doc, layerId);
  if (!loc) throw new CommandError(`Layer not found: ${layerId}`);
  return loc;
}

export function requireUnlocked(loc: LayerLocation, action: string): void {
  if (loc.layer.locked) throw new CommandError(`Cannot ${action}: layer "${loc.layer.name}" is locked`);
  const locked = loc.ancestors.find((g) => g.locked);
  if (locked) throw new CommandError(`Cannot ${action}: group "${locked.name}" is locked`);
}

/**
 * Resolves where a new/moved layer goes. Precedence: `above` → `below` →
 * explicit `parentId`/`index` → top of the document root.
 */
export function resolveInsertion(
  doc: Document,
  where: { above?: string; below?: string; parentId?: string | null; index?: number },
): { parentId: string | null; index: number } {
  if (where.above) {
    const loc = requireLayer(doc, where.above);
    return { parentId: loc.parentId, index: loc.index + 1 };
  }
  if (where.below) {
    const loc = requireLayer(doc, where.below);
    return { parentId: loc.parentId, index: loc.index };
  }
  const parentId = where.parentId ?? null;
  const len = getChildren(doc, parentId).length;
  const index = where.index ?? len;
  if (index < 0 || index > len) throw new CommandError(`Index ${index} is out of range (0-${len})`);
  return { parentId, index };
}

export const placementParams: Record<string, JSONSchema> = {
  above: { type: 'string', description: 'Place directly above this layer id (same parent).' },
  below: { type: 'string', description: 'Place directly below this layer id (same parent).' },
  parentId: {
    anyOf: [{ type: 'string' }, { type: 'null' }],
    description: 'Group id to place into; null or omitted for the document root.',
  },
  index: { type: 'integer', minimum: 0, description: 'Position within the parent, 0 = bottom. Defaults to the top.' },
};

// ---------------------------------------------------------------------------
// Pixel-editing helpers
// ---------------------------------------------------------------------------


export const modeParam: JSONSchema = {
  type: 'string',
  enum: ['replace', 'add', 'subtract', 'intersect'],
  default: 'replace',
  title: 'Mode',
  description: 'How the new area combines with the current selection.',
};

/** The editable pixel layer at `layerId` (pixel or AI layers), unlocked. */
export function requirePixelLayer(doc: Document, layerId: string, action: string): PixelLayer | AIGeneratedLayer {
  const loc = requireLayer(doc, layerId);
  requireUnlocked(loc, action);
  const l = loc.layer;
  if (l.type !== 'pixel' && l.type !== 'ai') {
    const hint = l.type === 'text' || l.type === 'shape' ? ' Rasterize it first (layer.rasterize).' : '';
    throw new CommandError(`Cannot ${action}: "${l.name}" is not a pixel layer.${hint}`);
  }
  return l;
}

/** Area an edit applies to: the selection's bounds, or the whole document. */
export function editRect(doc: Document): Rect | null {
  if (!doc.selection) return { x: 0, y: 0, width: doc.width, height: doc.height };
  return gridBounds(doc.selection.mask);
}

/** Selection coverage for a rect (null when nothing is selected = everything editable). */
export function selectionCoverage(doc: Document, rect: Rect): Region | null {
  return doc.selection ? readGrid(doc.selection.mask, rect, 0) : null;
}
