import { ResourceStore, type CommandContext } from './command';
import { CommandError, type Document } from './document';
import { History, USER, type Actor, type HistoryEntry } from './history';
import { applyPatch, concatPatches, invertPatch, type Patch } from './patch';
import type { CommandRegistry } from './registry';
import { validate } from './schema';

export type BusEvent =
  | { readonly type: 'dispatch'; readonly entry: HistoryEntry }
  | { readonly type: 'undo' | 'redo'; readonly entry: HistoryEntry }
  | { readonly type: 'reset' }
  | { readonly type: 'group-start' | 'group-end' | 'group-cancel'; readonly title: string };

export interface DispatchOptions {
  readonly actor?: Actor;
}

interface OpenGroup {
  readonly title: string;
  readonly actor: Actor;
  readonly children: HistoryEntry[];
  depth: number;
}

let sessionCounter = 0;

/**
 * The single path for document mutations. UI, shortcuts, macros, plugins and
 * AI agents all call `dispatch`, so every change is validated, undoable and
 * attributed in the History panel.
 */
export class CommandBus {
  readonly history: History;
  readonly resources = new ResourceStore();
  private _document: Document;
  private listeners = new Set<(event: BusEvent, doc: Document) => void>();
  private group: OpenGroup | null = null;
  private idCounter = 0;
  private readonly idPrefix: string;
  private readonly now: () => number;

  constructor(
    readonly registry: CommandRegistry,
    document: Document,
    opts: { maxHistory?: number; idPrefix?: string; now?: () => number } = {},
  ) {
    this._document = document;
    this.history = new History(opts.maxHistory ?? 200);
    this.idPrefix = opts.idPrefix ?? `${(++sessionCounter).toString(36)}${Math.random().toString(36).slice(2, 6)}`;
    this.now = opts.now ?? (() => Date.now());
  }

  get document(): Document {
    return this._document;
  }

  get inGroup(): boolean {
    return this.group !== null;
  }

  subscribe(listener: (event: BusEvent, doc: Document) => void): () => void {
    this.listeners.add(listener);
    return () => this.listeners.delete(listener);
  }

  private emit(event: BusEvent): void {
    for (const l of this.listeners) l(event, this._document);
  }

  private context(): CommandContext {
    return {
      newId: (prefix) => `${prefix}_${this.idPrefix}${(++this.idCounter).toString(36)}`,
      resources: this.resources,
    };
  }

  /** Validates params and executes a command. Throws `CommandError` with a readable message on bad input. */
  dispatch<R = unknown>(commandId: string, params: Record<string, unknown> = {}, opts: DispatchOptions = {}): R {
    const command = this.registry.get(commandId);
    if (!command) throw new CommandError(`Unknown command: ${commandId}`);
    const errors = validate(command.schema, params);
    if (errors.length) throw new CommandError(`Invalid parameters for ${commandId}: ${errors.join('; ')}`);

    const before = this._document;
    const patch = command.execute(before, params, this.context());
    if (patch.ops.length === 0) return patch.result as R;
    this._document = applyPatch(before, patch);

    const actor = opts.actor ?? this.group?.actor ?? USER;
    const entry = this.history.createEntry({
      commandId,
      title: command.describe?.(params, before) ?? command.title,
      actor,
      timestamp: this.now(),
      patch: { ops: patch.ops },
      coalesceKey: this.group ? undefined : (command.coalesceKey?.(params) ?? undefined),
    });
    if (this.group) this.group.children.push(entry);
    else this.history.push(entry);
    this.emit({ type: 'dispatch', entry });
    return patch.result as R;
  }

  /** Test-only/advanced: whether a command would accept these params. */
  validateParams(commandId: string, params: Record<string, unknown>): string[] {
    const command = this.registry.get(commandId);
    if (!command) return [`Unknown command: ${commandId}`];
    return validate(command.schema, params);
  }

  undo(): boolean {
    this.assertNoGroup('undo');
    const entry = this.history.stepBack();
    if (!entry) return false;
    this._document = applyPatch(this._document, this.inverse(entry));
    this.emit({ type: 'undo', entry });
    return true;
  }

  redo(): boolean {
    this.assertNoGroup('redo');
    const entry = this.history.stepForward();
    if (!entry) return false;
    this._document = applyPatch(this._document, entry.patch);
    this.emit({ type: 'redo', entry });
    return true;
  }

  /** Undo/redo until `cursor` entries are applied (History panel click). */
  jumpTo(cursor: number): void {
    while (this.history.cursor > cursor && this.undo());
    while (this.history.cursor < cursor && this.redo());
  }

  /**
   * Starts a grouped history entry (used for agent runs and macros). Commands
   * dispatched until `endGroup` are applied immediately but recorded as one
   * undo step. Nested begin/end pairs merge into the outermost group.
   */
  beginGroup(title: string, actor: Actor = USER): void {
    if (this.group) {
      this.group.depth++;
      return;
    }
    this.group = { title, actor, children: [], depth: 1 };
    this.emit({ type: 'group-start', title });
  }

  endGroup(): HistoryEntry | null {
    const group = this.group;
    if (!group) throw new Error('endGroup called without beginGroup');
    if (--group.depth > 0) return null;
    this.group = null;
    this.emit({ type: 'group-end', title: group.title });
    if (group.children.length === 0) return null;
    const entry = this.history.createEntry({
      commandId: 'group',
      title: group.title,
      actor: group.actor,
      timestamp: this.now(),
      patch: concatPatches(group.children.map((c) => c.patch)),
      children: group.children,
    });
    this.history.push(entry);
    this.emit({ type: 'dispatch', entry });
    return entry;
  }

  /** Reverts everything done since `beginGroup` and records nothing (e.g. an agent run was stopped). */
  cancelGroup(): void {
    const group = this.group;
    if (!group) throw new Error('cancelGroup called without beginGroup');
    this.group = null;
    const patch = concatPatches(group.children.map((c) => c.patch));
    this._document = applyPatch(this._document, invertPatch(patch));
    this.emit({ type: 'group-cancel', title: group.title });
  }

  /** Replaces the document without history (e.g. after loading). */
  reset(document: Document): void {
    this.assertNoGroup('reset');
    this._document = document;
    this.history.clear();
    this.emit({ type: 'reset' });
  }

  private inverse(entry: HistoryEntry): Patch {
    const command = entry.children ? undefined : this.registry.get(entry.commandId);
    return command?.invert ? command.invert(entry.patch) : invertPatch(entry.patch);
  }

  private assertNoGroup(action: string): void {
    if (this.group) throw new Error(`Cannot ${action} while a grouped action ("${this.group.title}") is running`);
  }
}
