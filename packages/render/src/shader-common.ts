import { BLEND_MODE_IDS, type BlendMode } from '@canvas-ai/core';

/** Blend mode → shader integer, generated from the core table so shaders can't drift from it. */
export const MODE: Record<BlendMode, number> = Object.fromEntries(BLEND_MODE_IDS.map((id, i) => [id, i])) as Record<BlendMode, number>;
