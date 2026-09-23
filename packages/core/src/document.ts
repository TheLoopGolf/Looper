import type { BlendMode, GroupBlendMode } from './blend-modes';
import { emptyGrid, type TileGrid } from './tiles';

/** 2D affine transform [a, b, c, d, e, f] (same layout as DOMMatrix / canvas setTransform). */
export type Transform2D = readonly [number, number, number, number, number, number];
export const IDENTITY_TRANSFORM: Transform2D = [1, 0, 0, 1, 0, 0];

export type ColorProfile = 'sRGB' | 'DisplayP3';
export type BitDepth = 8 | 16;

export interface Mask {
  /** Single-channel grid; 255 = fully visible. Missing tiles use `defaultValue`. */
  readonly tiles: TileGrid;
  readonly defaultValue: 0 | 255;
  readonly enabled: boolean;
  readonly linked: boolean;
  readonly density: number;
  readonly feather: number;
}

export interface LayerEffect {
  readonly id: string;
  readonly kind: string;
  readonly enabled: boolean;
  readonly params: Readonly<Record<string, unknown>>;
}

interface LayerCommon {
  readonly id: string;
  readonly name: string;
  readonly visible: boolean;
  readonly locked: boolean;
  /** 0..1, applies to the layer including effects. */
  readonly opacity: number;
  /** 0..1, applies to layer content but not effects. */
  readonly fill: number;
  readonly mask: Mask | null;
  readonly clipToBelow: boolean;
  readonly transform: Transform2D;
  readonly effects: readonly LayerEffect[];
}

export interface PixelLayer extends LayerCommon {
  readonly type: 'pixel';
  readonly blendMode: BlendMode;
  readonly tiles: TileGrid;
}

export interface AdjustmentLayer extends LayerCommon {
  readonly type: 'adjustment';
  readonly blendMode: BlendMode;
  readonly adjustment: { readonly kind: string; readonly params: Readonly<Record<string, unknown>> };
}

export interface TextLayer extends LayerCommon {
  readonly type: 'text';
  readonly blendMode: BlendMode;
  readonly text: string;
  readonly style: Readonly<Record<string, unknown>>;
}

export interface ShapeLayer extends LayerCommon {
  readonly type: 'shape';
  readonly blendMode: BlendMode;
  readonly shape: Readonly<Record<string, unknown>>;
}

export interface SmartLayer extends LayerCommon {
  readonly type: 'smart';
  readonly blendMode: BlendMode;
  readonly source: { readonly kind: 'embedded' | 'linked'; readonly ref: string; readonly mimeType: string };
  readonly tiles: TileGrid;
}

export interface GroupLayer extends LayerCommon {
  readonly type: 'group';
  readonly blendMode: GroupBlendMode;
  readonly collapsed: boolean;
  /** Bottom-to-top, like `Document.layers`. */
  readonly children: readonly LayerNode[];
}

export interface AIGenerationRecord {
  readonly prompt: string;
  readonly seed: number;
  readonly createdAt: string;
  readonly params: Readonly<Record<string, unknown>>;
}

/** Pixel layer produced by an AI provider; keeps everything needed to regenerate it. */
export interface AIGeneratedLayer extends LayerCommon {
  readonly type: 'ai';
  readonly blendMode: BlendMode;
  readonly tiles: TileGrid;
  readonly prompt: string;
  readonly negativePrompt?: string;
  readonly provider: string;
  readonly model: string;
  readonly seed: number;
  readonly sourceRegion: { readonly x: number; readonly y: number; readonly width: number; readonly height: number };
  readonly params: Readonly<Record<string, unknown>>;
  readonly generationHistory: readonly AIGenerationRecord[];
}

export type LayerNode =
  | PixelLayer
  | AdjustmentLayer
  | TextLayer
  | ShapeLayer
  | SmartLayer
  | GroupLayer
  | AIGeneratedLayer;

export type LayerType = LayerNode['type'];

/** Layers that carry their own raster tiles. */
export type RasterLayer = PixelLayer | SmartLayer | AIGeneratedLayer;

export interface Guide {
  readonly id: string;
  readonly orientation: 'horizontal' | 'vertical';
  readonly position: number;
}

export interface Selection {
  /** Single-channel coverage mask, document-sized. */
  readonly mask: TileGrid;
}

export interface Document {
  readonly id: string;
  readonly name: string;
  readonly width: number;
  readonly height: number;
  readonly colorProfile: ColorProfile;
  readonly bitDepth: BitDepth;
  /** Bottom-to-top: `layers[0]` is the bottom-most layer. */
  readonly layers: readonly LayerNode[];
  readonly guides: readonly Guide[];
  readonly selection: Selection | null;
  readonly metadata: Readonly<Record<string, unknown>>;
}

/** Document fields that commands may change via `setDocProps`. */
export type DocProps = Pick<Document, 'name' | 'width' | 'height' | 'colorProfile' | 'bitDepth' | 'guides' | 'selection' | 'metadata'>;

export const MAX_DOCUMENT_SIZE = 30000;

export function createDocument(opts: {
  id: string;
  name?: string;
  width: number;
  height: number;
  colorProfile?: ColorProfile;
  bitDepth?: BitDepth;
  layers?: LayerNode[];
}): Document {
  const { width, height } = opts;
  if (!Number.isInteger(width) || !Number.isInteger(height) || width < 1 || height < 1) {
    throw new Error('Document size must be positive integers');
  }
  if (width > MAX_DOCUMENT_SIZE || height > MAX_DOCUMENT_SIZE) {
    throw new Error(`Document size is limited to ${MAX_DOCUMENT_SIZE}px per side`);
  }
  return {
    id: opts.id,
    name: opts.name ?? 'Untitled',
    width,
    height,
    colorProfile: opts.colorProfile ?? 'sRGB',
    bitDepth: opts.bitDepth ?? 8,
    layers: opts.layers ?? [],
    guides: [],
    selection: null,
    metadata: {},
  };
}

function common(id: string, name: string): LayerCommon {
  return {
    id,
    name,
    visible: true,
    locked: false,
    opacity: 1,
    fill: 1,
    mask: null,
    clipToBelow: false,
    transform: IDENTITY_TRANSFORM,
    effects: [],
  };
}

export function createPixelLayer(
  doc: Pick<Document, 'width' | 'height'>,
  id: string,
  name: string,
  tiles?: TileGrid,
): PixelLayer {
  return { ...common(id, name), type: 'pixel', blendMode: 'normal', tiles: tiles ?? emptyGrid(doc.width, doc.height, 4) };
}

export function createGroupLayer(id: string, name: string, children: LayerNode[] = []): GroupLayer {
  return { ...common(id, name), type: 'group', blendMode: 'passThrough', collapsed: false, children };
}

export function isRasterLayer(layer: LayerNode): layer is RasterLayer {
  return layer.type === 'pixel' || layer.type === 'smart' || layer.type === 'ai';
}

// ---------------------------------------------------------------------------
// Tree helpers. The layer tree is addressed by (parentId, index) where
// parentId === null means the document root.
// ---------------------------------------------------------------------------

export interface LayerLocation {
  readonly layer: LayerNode;
  readonly parentId: string | null;
  readonly index: number;
  /** Ancestor groups, outermost first. */
  readonly ancestors: readonly GroupLayer[];
}

export function findLayer(doc: Pick<Document, 'layers'>, id: string): LayerLocation | null {
  const visit = (list: readonly LayerNode[], parent: GroupLayer | null, ancestors: GroupLayer[]): LayerLocation | null => {
    for (let i = 0; i < list.length; i++) {
      const layer = list[i];
      if (layer.id === id) return { layer, parentId: parent?.id ?? null, index: i, ancestors };
      if (layer.type === 'group') {
        const found = visit(layer.children, layer, [...ancestors, layer]);
        if (found) return found;
      }
    }
    return null;
  };
  return visit(doc.layers, null, []);
}

export function getLayer(doc: Pick<Document, 'layers'>, id: string): LayerNode {
  const loc = findLayer(doc, id);
  if (!loc) throw new CommandError(`Layer not found: ${id}`);
  return loc.layer;
}

/** Children list of a container (null = document root). */
export function getChildren(doc: Pick<Document, 'layers'>, parentId: string | null): readonly LayerNode[] {
  if (parentId === null) return doc.layers;
  const parent = getLayer(doc, parentId);
  if (parent.type !== 'group') throw new CommandError(`Layer ${parentId} is not a group`);
  return parent.children;
}

/** Depth-first walk, bottom-to-top within each container. */
export function* walkLayers(list: readonly LayerNode[]): Generator<LayerNode> {
  for (const layer of list) {
    yield layer;
    if (layer.type === 'group') yield* walkLayers(layer.children);
  }
}

/** Replaces the children of a container, returning new layer arrays up the tree (structural sharing). */
export function updateChildren(
  doc: Document,
  parentId: string | null,
  update: (children: readonly LayerNode[]) => readonly LayerNode[],
): Document {
  if (parentId === null) return { ...doc, layers: update(doc.layers) };
  const mapList = (list: readonly LayerNode[]): readonly LayerNode[] | null => {
    for (let i = 0; i < list.length; i++) {
      const layer = list[i];
      if (layer.type !== 'group') continue;
      let next: GroupLayer | null = null;
      if (layer.id === parentId) {
        next = { ...layer, children: update(layer.children) };
      } else {
        const children = mapList(layer.children);
        if (children) next = { ...layer, children };
      }
      if (next) {
        const copy = list.slice();
        copy[i] = next;
        return copy;
      }
    }
    return null;
  };
  const layers = mapList(doc.layers);
  if (!layers) throw new CommandError(`Group not found: ${parentId}`);
  return { ...doc, layers };
}

/** Error thrown for invalid command input; the message is shown to users and agents. */
export class CommandError extends Error {
  override name = 'CommandError';
}
