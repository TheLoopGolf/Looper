import type { Actor } from '@canvas-ai/core';

/** Approval policy for agent-issued commands (spec §5.2). */
export type ApprovalMode = 'always-ask' | 'ask-destructive' | 'autopilot';

export interface AgentDefinition {
  readonly id: string;
  readonly name: string;
  readonly systemPrompt: string;
  /** Command patterns the agent may call (same syntax as plugin `allowedCommands`). */
  readonly allowedCommands: readonly string[];
  readonly defaultModels?: Partial<Record<'chat' | 'inpaint' | 'segment', string>>;
  /** Max verify→iterate rounds (default 2). */
  readonly maxVerifyIterations?: number;
}

export function agentActor(agent: Pick<AgentDefinition, 'name'>, runId: string): Actor {
  return { kind: 'agent', name: agent.name, runId };
}
