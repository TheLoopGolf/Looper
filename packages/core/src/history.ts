import { concatPatches, type Patch, type PatchOp } from './patch';

/** Who performed an action. Shown as a badge in the History panel. */
export type Actor =
  | { readonly kind: 'user' }
  | { readonly kind: 'agent'; readonly name: string; readonly runId: string }
  | { readonly kind: 'plugin'; readonly id: string; readonly name: string }
  | { readonly kind: 'macro'; readonly id: string; readonly name: string };

export const USER: Actor = { kind: 'user' };

export interface HistoryEntry {
  readonly id: number;
  readonly commandId: string;
  readonly title: string;
  readonly actor: Actor;
  readonly timestamp: number;
  /** Combined patch; for groups, the concatenation of all children. */
  readonly patch: Patch;
  readonly coalesceKey?: string;
  /** Present for grouped entries (e.g. an agent run); undone/redone as one step. */
  readonly children?: readonly HistoryEntry[];
}

/** Collapses consecutive replaceLayer ops on the same layer (used when coalescing slider drags). */
export function simplifyOps(ops: readonly PatchOp[]): PatchOp[] {
  const out: PatchOp[] = [];
  for (const op of ops) {
    const last = out[out.length - 1];
    if (last?.op === 'replaceLayer' && op.op === 'replaceLayer' && last.after === op.before) {
      out[out.length - 1] = { op: 'replaceLayer', before: last.before, after: op.after };
    } else {
      out.push(op);
    }
  }
  return out;
}

/**
 * Linear undo history. `entries.slice(0, cursor)` are applied; the rest form
 * the redo stack, which is discarded as soon as a new entry is pushed so that
 * the tiles it references can be garbage-collected.
 */
export class History {
  private _entries: HistoryEntry[] = [];
  private _cursor = 0;
  private nextId = 1;

  constructor(
    readonly maxEntries = 200,
    private readonly coalesceWindowMs = 1500,
  ) {}

  get entries(): readonly HistoryEntry[] {
    return this._entries;
  }

  get cursor(): number {
    return this._cursor;
  }

  get canUndo(): boolean {
    return this._cursor > 0;
  }

  get canRedo(): boolean {
    return this._cursor < this._entries.length;
  }

  createEntry(e: Omit<HistoryEntry, 'id'>): HistoryEntry {
    return { ...e, id: this.nextId++ };
  }

  push(entry: HistoryEntry): void {
    this._entries.length = this._cursor;
    const last = this._entries[this._entries.length - 1];
    if (
      entry.coalesceKey &&
      last?.coalesceKey === entry.coalesceKey &&
      !last.children &&
      entry.timestamp - last.timestamp <= this.coalesceWindowMs
    ) {
      this._entries[this._entries.length - 1] = {
        ...last,
        title: entry.title,
        timestamp: entry.timestamp,
        patch: { ops: simplifyOps(concatPatches([last.patch, entry.patch]).ops) },
      };
      return;
    }
    this._entries.push(entry);
    if (this._entries.length > this.maxEntries) this._entries.splice(0, this._entries.length - this.maxEntries);
    this._cursor = this._entries.length;
  }

  /** Moves the cursor back one step and returns the entry to invert. */
  stepBack(): HistoryEntry | null {
    if (!this.canUndo) return null;
    return this._entries[--this._cursor];
  }

  /** Moves the cursor forward one step and returns the entry to re-apply. */
  stepForward(): HistoryEntry | null {
    if (!this.canRedo) return null;
    return this._entries[this._cursor++];
  }

  clear(): void {
    this._entries = [];
    this._cursor = 0;
  }
}
