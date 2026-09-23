import type { Command } from './command';
import type { JSONSchema } from './schema';

/** Tool definition in the shape most tool-calling LLM APIs accept. */
export interface AgentTool {
  /** Tool-safe name (`layer.create` → `layer_create`). */
  readonly name: string;
  readonly commandId: string;
  readonly description: string;
  readonly input_schema: JSONSchema;
  readonly destructive: boolean;
}

/** Glob-style match used for plugin `allowedCommands` ("layer.*", "adjust.curves", "*"). */
export function matchesCommandPattern(id: string, pattern: string): boolean {
  if (pattern === '*') return true;
  if (pattern.endsWith('.*')) return id.startsWith(pattern.slice(0, -1));
  return id === pattern;
}

export function toolNameForCommand(id: string): string {
  return id.replace(/[^a-zA-Z0-9_-]/g, '_');
}

export class CommandRegistry {
  private commands = new Map<string, Command<any>>();

  register(command: Command<any>): () => void {
    if (this.commands.has(command.id)) throw new Error(`Command already registered: ${command.id}`);
    if (!command.agentDescription?.trim()) throw new Error(`Command ${command.id} must have an agentDescription`);
    if (command.schema.type !== 'object') throw new Error(`Command ${command.id} schema must be an object`);
    this.commands.set(command.id, command);
    return () => this.commands.delete(command.id);
  }

  registerAll(commands: readonly Command<any>[]): void {
    for (const c of commands) this.register(c);
  }

  get(id: string): Command<any> | undefined {
    return this.commands.get(id);
  }

  list(patterns?: readonly string[]): Command<any>[] {
    const all = [...this.commands.values()];
    if (!patterns) return all;
    return all.filter((c) => patterns.some((p) => matchesCommandPattern(c.id, p)));
  }

  /** Generates the agent's tool list from command metadata. */
  toAgentTools(patterns?: readonly string[]): AgentTool[] {
    return this.list(patterns).map((c) => ({
      name: toolNameForCommand(c.id),
      commandId: c.id,
      description: c.destructive
        ? `${c.agentDescription} (Destructive: may require user approval.)`
        : c.agentDescription,
      input_schema: c.schema,
      destructive: !!c.destructive,
    }));
  }
}
