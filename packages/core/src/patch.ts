import {
  CommandError,
  findLayer,
  getChildren,
  updateChildren,
  type DocProps,
  type Document,
  type LayerNode,
} from './document';

/**
 * A Patch is a small, invertible description of a document change. Layers are
 * immutable, so ops hold references to whole layer objects (including their
 * copy-on-write tile grids) instead of deep copies.
 */
export type PatchOp =
  | { readonly op: 'insertLayer'; readonly parentId: string | null; readonly index: number; readonly layer: LayerNode }
  | { readonly op: 'removeLayer'; readonly parentId: string | null; readonly index: number; readonly layer: LayerNode }
  | { readonly op: 'replaceLayer'; readonly before: LayerNode; readonly after: LayerNode }
  | { readonly op: 'setDocProps'; readonly before: Partial<DocProps>; readonly after: Partial<DocProps> };

export interface Patch {
  readonly ops: readonly PatchOp[];
  /** Command output (e.g. the id of a created layer). Not part of undo. */
  readonly result?: unknown;
}

export const EMPTY_PATCH: Patch = { ops: [] };

export function applyOp(doc: Document, op: PatchOp): Document {
  switch (op.op) {
    case 'insertLayer': {
      if (findLayer(doc, op.layer.id)) throw new CommandError(`Layer id already exists: ${op.layer.id}`);
      const len = getChildren(doc, op.parentId).length;
      if (op.index < 0 || op.index > len) throw new CommandError(`Insert index out of range: ${op.index}`);
      return updateChildren(doc, op.parentId, (c) => [...c.slice(0, op.index), op.layer, ...c.slice(op.index)]);
    }
    case 'removeLayer': {
      const children = getChildren(doc, op.parentId);
      if (children[op.index]?.id !== op.layer.id) throw new CommandError(`Layer ${op.layer.id} is not at the expected position`);
      return updateChildren(doc, op.parentId, (c) => [...c.slice(0, op.index), ...c.slice(op.index + 1)]);
    }
    case 'replaceLayer': {
      if (op.before.id !== op.after.id) throw new CommandError('replaceLayer cannot change a layer id');
      const loc = findLayer(doc, op.before.id);
      if (!loc) throw new CommandError(`Layer not found: ${op.before.id}`);
      return updateChildren(doc, loc.parentId, (c) => {
        const copy = c.slice();
        copy[loc.index] = op.after;
        return copy;
      });
    }
    case 'setDocProps':
      return { ...doc, ...op.after };
  }
}

export function applyPatch(doc: Document, patch: Patch): Document {
  let next = doc;
  for (const op of patch.ops) next = applyOp(next, op);
  return next;
}

export function invertOp(op: PatchOp): PatchOp {
  switch (op.op) {
    case 'insertLayer':
      return { ...op, op: 'removeLayer' };
    case 'removeLayer':
      return { ...op, op: 'insertLayer' };
    case 'replaceLayer':
      return { op: 'replaceLayer', before: op.after, after: op.before };
    case 'setDocProps':
      return { op: 'setDocProps', before: op.after, after: op.before };
  }
}

export function invertPatch(patch: Patch): Patch {
  return { ops: patch.ops.map(invertOp).reverse() };
}

export function concatPatches(patches: readonly Patch[]): Patch {
  return { ops: patches.flatMap((p) => p.ops) };
}

// ---------------------------------------------------------------------------
// Builder used by command implementations: applies ops as it records them so a
// multi-step command can read its own intermediate state.
// ---------------------------------------------------------------------------

export class PatchBuilder {
  private ops: PatchOp[] = [];
  constructor(private current: Document) {}

  get doc(): Document {
    return this.current;
  }

  push(op: PatchOp): this {
    this.current = applyOp(this.current, op);
    this.ops.push(op);
    return this;
  }

  insertLayer(parentId: string | null, index: number, layer: LayerNode): this {
    return this.push({ op: 'insertLayer', parentId, index, layer });
  }

  removeLayer(id: string): this {
    const loc = findLayer(this.current, id);
    if (!loc) throw new CommandError(`Layer not found: ${id}`);
    return this.push({ op: 'removeLayer', parentId: loc.parentId, index: loc.index, layer: loc.layer });
  }

  replaceLayer(after: LayerNode): this {
    const loc = findLayer(this.current, after.id);
    if (!loc) throw new CommandError(`Layer not found: ${after.id}`);
    if (loc.layer === after) return this;
    return this.push({ op: 'replaceLayer', before: loc.layer, after });
  }

  updateLayer<L extends LayerNode>(id: string, update: (layer: L) => L): this {
    const loc = findLayer(this.current, id);
    if (!loc) throw new CommandError(`Layer not found: ${id}`);
    return this.replaceLayer(update(loc.layer as L));
  }

  setDocProps(after: Partial<DocProps>): this {
    const before: Partial<DocProps> = {};
    for (const key of Object.keys(after) as (keyof DocProps)[]) (before as Record<string, unknown>)[key] = this.current[key];
    return this.push({ op: 'setDocProps', before, after });
  }

  build(result?: unknown): Patch {
    return result === undefined ? { ops: this.ops } : { ops: this.ops, result };
  }
}
