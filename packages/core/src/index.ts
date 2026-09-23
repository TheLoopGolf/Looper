export * from './blend-modes';
export * from './bus';
export * from './command';
export * from './commands/common';
export * from './commands/layer';
export * from './document';
export * from './history';
export * from './io/cnva';
export * from './io/png';
export * from './patch';
export * from './registry';
export * from './schema';
export * from './tiles';

import { LAYER_COMMANDS } from './commands/layer';
import { CommandRegistry } from './registry';

/** Registry pre-populated with every built-in command. */
export function createDefaultRegistry(): CommandRegistry {
  const registry = new CommandRegistry();
  registry.registerAll(LAYER_COMMANDS);
  return registry;
}
