export * from './blend-modes';
export * from './bus';
export * from './command';
export * from './commands/adjust';
export * from './commands/common';
export * from './commands/layer';
export * from './commands/mask';
export * from './commands/paint';
export * from './commands/selection';
export * from './commands/transform';
export * from './commands/vector';
export * from './composite/blend';
export * from './composite/cpu';
export * from './composite/plan';
export * from './document';
export * from './history';
export * from './io/cnva';
export * from './io/png';
export * from './patch';
export * as raster from './raster';
export { ADJUSTMENT_KINDS, ADJUSTMENTS, compileAdjustment, isAdjustmentKind, type AdjustmentKind, type CompiledAdjustment } from './raster/adjustments';
export { FILTER_KINDS, FILTERS, type FilterKind } from './raster/filters';
export { DEFAULT_BRUSH, packPoints, StrokeAccumulator, unpackPoints, type BrushSettings, type StrokePoint } from './raster/brush';
export { setTextRasterizer, type TextRasterizer } from './raster/shapes';
export * from './registry';
export * from './schema';
export * from './tiles';

import { ADJUST_COMMANDS, FILTER_COMMANDS } from './commands/adjust';
import { LAYER_COMMANDS } from './commands/layer';
import { MASK_COMMANDS } from './commands/mask';
import { PAINT_COMMANDS } from './commands/paint';
import { SELECTION_COMMANDS } from './commands/selection';
import { TRANSFORM_COMMANDS } from './commands/transform';
import { VECTOR_COMMANDS } from './commands/vector';
import { CommandRegistry } from './registry';

export const BUILTIN_COMMANDS = [
  ...LAYER_COMMANDS,
  ...MASK_COMMANDS,
  ...VECTOR_COMMANDS,
  ...SELECTION_COMMANDS,
  ...PAINT_COMMANDS,
  ...ADJUST_COMMANDS,
  ...FILTER_COMMANDS,
  ...TRANSFORM_COMMANDS,
];

/** Registry pre-populated with every built-in command. */
export function createDefaultRegistry(): CommandRegistry {
  const registry = new CommandRegistry();
  registry.registerAll(BUILTIN_COMMANDS);
  return registry;
}
