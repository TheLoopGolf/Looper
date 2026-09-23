import type { Document } from './document';
import type { Patch } from './patch';
import type { JSONSchema } from './schema';

/** A raster image handed to commands by reference (imports, AI outputs, clipboard). */
export interface ImageResource {
  readonly width: number;
  readonly height: number;
  /** Straight-alpha RGBA8, row-major. */
  readonly pixels: Uint8ClampedArray;
}

/**
 * Binary inputs can't travel through JSON params (which agents and plugins
 * produce), so they are registered here and referenced by id.
 */
export class ResourceStore {
  private items = new Map<string, ImageResource>();
  private counter = 0;

  add(resource: ImageResource, id = `res_${++this.counter}`): string {
    this.items.set(id, resource);
    return id;
  }

  get(id: string): ImageResource | undefined {
    return this.items.get(id);
  }

  delete(id: string): void {
    this.items.delete(id);
  }
}

export interface CommandContext {
  /** Unique id for new layers/objects created by this command. */
  newId(prefix: string): string;
  readonly resources: ResourceStore;
}

export type ObjectSchema = Extract<JSONSchema, { type: 'object' }>;

export interface Command<P = Record<string, unknown>> {
  /** Stable dotted id, e.g. "layer.create". */
  readonly id: string;
  /** Short human-readable name shown in menus and the History panel. */
  readonly title: string;
  /** Menu/tool grouping, e.g. "Layer". */
  readonly category: string;
  /** What the command does, when to use it, and what it returns. This is the AI agent's documentation. */
  readonly agentDescription: string;
  readonly schema: ObjectSchema;
  /** Destructive commands (deleting data, overwriting pixels in place) need approval in "Ask for destructive" mode. */
  readonly destructive?: boolean;
  /** Optional params-specific history title, e.g. "Opacity 50%". */
  describe?(params: P, doc: Document): string;
  /**
   * Consecutive dispatches returning the same non-null key are merged into one
   * history entry (e.g. dragging an opacity slider).
   */
  coalesceKey?(params: P): string | null;
  execute(doc: Document, params: P, ctx: CommandContext): Patch;
  /** Defaults to `invertPatch`. Override only for patches that can't be inverted mechanically. */
  invert?(patch: Patch): Patch;
}

/** Identity helper that keeps param types when defining commands. */
export function defineCommand<P>(command: Command<P>): Command<P> {
  return command;
}
