/**
 * Layer blend modes. The ids are stable (stored in `.cnva` files and used by
 * the AI agent), and their order is the order shown in the UI.
 * `group` is used to draw separators in menus.
 */
export const BLEND_MODES = [
  { id: 'normal', label: 'Normal', group: 'normal' },
  { id: 'darken', label: 'Darken', group: 'darken' },
  { id: 'multiply', label: 'Multiply', group: 'darken' },
  { id: 'colorBurn', label: 'Color Burn', group: 'darken' },
  { id: 'linearBurn', label: 'Linear Burn', group: 'darken' },
  { id: 'darkerColor', label: 'Darker Color', group: 'darken' },
  { id: 'lighten', label: 'Lighten', group: 'lighten' },
  { id: 'screen', label: 'Screen', group: 'lighten' },
  { id: 'colorDodge', label: 'Color Dodge', group: 'lighten' },
  { id: 'linearDodge', label: 'Linear Dodge (Add)', group: 'lighten' },
  { id: 'lighterColor', label: 'Lighter Color', group: 'lighten' },
  { id: 'overlay', label: 'Overlay', group: 'contrast' },
  { id: 'softLight', label: 'Soft Light', group: 'contrast' },
  { id: 'hardLight', label: 'Hard Light', group: 'contrast' },
  { id: 'vividLight', label: 'Vivid Light', group: 'contrast' },
  { id: 'linearLight', label: 'Linear Light', group: 'contrast' },
  { id: 'pinLight', label: 'Pin Light', group: 'contrast' },
  { id: 'hardMix', label: 'Hard Mix', group: 'contrast' },
  { id: 'difference', label: 'Difference', group: 'inversion' },
  { id: 'exclusion', label: 'Exclusion', group: 'inversion' },
  { id: 'subtract', label: 'Subtract', group: 'inversion' },
  { id: 'divide', label: 'Divide', group: 'inversion' },
  { id: 'hue', label: 'Hue', group: 'component' },
  { id: 'saturation', label: 'Saturation', group: 'component' },
  { id: 'color', label: 'Color', group: 'component' },
  { id: 'luminosity', label: 'Luminosity', group: 'component' },
] as const;

export type BlendMode = (typeof BLEND_MODES)[number]['id'];

/** Groups may additionally pass their children through to the parent stack instead of compositing in isolation. */
export type GroupBlendMode = BlendMode | 'passThrough';

export const BLEND_MODE_IDS: readonly BlendMode[] = BLEND_MODES.map((m) => m.id);

/** Numeric index used by GPU shaders. Never reorder BLEND_MODES without updating shaders (they are generated from this). */
export function blendModeIndex(mode: BlendMode): number {
  const i = BLEND_MODE_IDS.indexOf(mode);
  if (i < 0) throw new Error(`Unknown blend mode: ${mode}`);
  return i;
}

export function isBlendMode(value: unknown): value is BlendMode {
  return typeof value === 'string' && (BLEND_MODE_IDS as readonly string[]).includes(value);
}
