import { applyPatch, CommandError, type Document } from '@canvas-ai/core';
import type { ToolContext } from './types';

/** Executes a command against the current document without touching history (for live previews). */
export function previewCommand(ctx: Pick<ToolContext, 'state' | 'doc'>, commandId: string, params: Record<string, unknown>): Document | null {
  const command = ctx.state.registry.get(commandId);
  if (!command) return null;
  try {
    const tab = ctx.state.tabs.find((t) => t.id === ctx.state.activeTabId);
    const patch = command.execute(ctx.doc, params, { newId: (p) => `${p}_preview`, resources: tab!.bus.resources });
    return applyPatch(ctx.doc, patch);
  } catch (e) {
    if (e instanceof CommandError) return null;
    throw e;
  }
}

/** Coalesces expensive preview work to one run per animation frame. */
export function frameThrottle(fn: () => void): { schedule(): void; flush(): void; cancel(): void } {
  let id = 0;
  return {
    schedule() {
      if (!id) id = requestAnimationFrame(() => ((id = 0), fn()));
    },
    flush() {
      if (id) cancelAnimationFrame(id);
      id = 0;
      fn();
    },
    cancel() {
      if (id) cancelAnimationFrame(id);
      id = 0;
    },
  };
}
