import {
  createAdjustmentLayer,
  createDocument,
  createGroupLayer,
  createPixelLayer,
  gridFromPixels,
  type BlendMode,
  type Document,
  type GroupBlendMode,
  type LayerNode,
  type Mask,
} from '@canvas-ai/core';

/** Deterministic test scenes shared by CPU golden tests (Node) and GPU equivalence tests (browser). */

export const SCENE_SIZE = 160; // spans a partial second tile in each direction

function pattern(w: number, h: number, f: (x: number, y: number) => [number, number, number, number]): Uint8ClampedArray {
  const px = new Uint8ClampedArray(w * h * 4);
  for (let y = 0; y < h; y++) for (let x = 0; x < w; x++) px.set(f(x, y), (y * w + x) * 4);
  return px;
}

function layer(doc: Document, id: string, px: Uint8ClampedArray, props: Partial<{ blendMode: BlendMode; opacity: number; fill: number }> = {}): LayerNode {
  return { ...createPixelLayer(doc, id, id, gridFromPixels(px, doc.width, doc.height)), ...props };
}

/**
 * One blend operation: backdrop + source gradients covering the 0..255 range,
 * with varying alpha on both. Single-step scenes are what the ≤1 LSB criterion
 * measures; multi-layer stacks compound per-step rounding (a 1 LSB difference
 * in an intermediate can flip threshold modes like Hard Mix).
 */
export function blendScene(mode: BlendMode): Document {
  const n = SCENE_SIZE;
  const base = createDocument({ id: `blend-${mode}`, width: n, height: n });
  const s = (v: number) => Math.round((v * 255) / (n - 1));
  const bottom = layer(base, 'backdrop', pattern(n, n, (x, y) => [s(x), s(y), 255 - s(x), y < n / 2 ? 255 : 255 - s(x) / 2]));
  const top = layer(
    base,
    'source',
    pattern(n, n, (x, y) => [s(y), 255 - s(x), (s(x) + s(y)) >> 1, x < n / 2 ? 255 : 255 - s(y)]),
    { blendMode: mode, opacity: 0.85, fill: 0.9 },
  );
  return { ...base, layers: [bottom, top] };
}

/** Nested groups: isolated group with a blend mode, pass-through with opacity, and hidden layers. */
export function groupScene(groupMode: GroupBlendMode = 'multiply'): Document {
  const n = SCENE_SIZE;
  const base = createDocument({ id: 'groups', width: n, height: n });
  const s = (v: number) => Math.round((v * 255) / (n - 1));
  const bg = layer(base, 'bg', pattern(n, n, (x, y) => [s(x), 128, s(y), 255]));
  const a = layer(base, 'a', pattern(n, n, (x, y) => (x > 20 && x < 120 ? [255, s(y), 0, 180] : [0, 0, 0, 0])), { blendMode: 'screen' });
  const b = layer(base, 'b', pattern(n, n, (x, y) => (y > 40 && y < 140 ? [0, 60, 255, 220] : [0, 0, 0, 0])), { blendMode: 'overlay', opacity: 0.7 });
  const hidden = { ...layer(base, 'hidden', pattern(n, n, () => [255, 0, 255, 255])), visible: false };
  const c = layer(base, 'c', pattern(n, n, (x, y) => ((x + y) % 40 < 20 ? [30, 200, 90, 255] : [0, 0, 0, 0])), { blendMode: 'difference' });
  const inner = { ...createGroupLayer('inner', 'inner', [c]), blendMode: 'passThrough' as GroupBlendMode, opacity: 0.6 };
  const outer = { ...createGroupLayer('outer', 'outer', [a, hidden, b, inner]), blendMode: groupMode, opacity: 0.9 };
  return { ...base, layers: [bg, outer] };
}

function gradientMask(doc: Document, f: (x: number, y: number) => number, defaultValue: 0 | 255, density = 1): Mask {
  const n = doc.width;
  const px = new Uint8ClampedArray(n * doc.height);
  for (let y = 0; y < doc.height; y++) for (let x = 0; x < n; x++) px[y * n + x] = f(x, y);
  return { tiles: gridFromPixels(px, n, doc.height, 1), defaultValue, enabled: true, linked: true, density, feather: 0 };
}

/** Non-default settings per adjustment so every code path does real work. */
export const ADJUSTMENT_SCENE_PARAMS: Record<string, Record<string, unknown>> = {
  brightnessContrast: { brightness: 25, contrast: 40 },
  levels: { inBlack: 20, inWhite: 220, gamma: 1.4, outBlack: 10, outWhite: 245 },
  curves: { rgb: [[0, 0], [64, 40], [192, 220], [255, 255]], red: [[0, 20], [255, 235]] },
  exposure: { exposure: 0.8, offset: 0.02, gamma: 1.1 },
  hueSaturation: { hue: 40, saturation: 35, lightness: -10 },
  vibrance: { vibrance: 60, saturation: -15 },
  colorBalance: { shadows: [20, 0, -15], midtones: [-10, 25, 0], highlights: [0, -20, 30], preserveLuminosity: true },
  blackWhite: { reds: 55, yellows: 70, greens: 30, cyans: 50, blues: 10, magentas: 90, tint: true, tintColor: [230, 200, 160], tintAmount: 60 },
  photoFilter: { color: [40, 120, 230], density: 45, preserveLuminosity: true },
  invert: {},
  posterize: { levels: 5 },
  threshold: { level: 120 },
};

/** Gradient image + one adjustment layer (opacity < 1, gradient mask); blend mode optional. */
export function adjustmentScene(kind: string, mode: BlendMode = 'normal'): Document {
  const n = SCENE_SIZE;
  const base = createDocument({ id: `adjust-${kind}${mode === 'normal' ? '' : `-${mode}`}`, width: n, height: n });
  const s = (v: number) => Math.round((v * 255) / (n - 1));
  const img = layer(base, 'image', pattern(n, n, (x, y) => [s(x), s(y), (s(x) * 7 + s(y) * 3) % 256, y < n * 0.75 ? 255 : 255 - s(x) / 2]));
  const adj = {
    ...createAdjustmentLayer('adj', kind, kind, ADJUSTMENT_SCENE_PARAMS[kind] ?? {}),
    blendMode: mode,
    opacity: 0.9,
    mask: gradientMask(base, (x) => (x < n / 4 ? 255 : Math.round(255 - (x * 160) / n)), 255),
  } as LayerNode;
  return { ...base, layers: [img, adj] };
}

/** Layer masks on pixel layers, isolated groups and pass-through groups, including density and hide-all defaults. */
export function maskScene(): Document {
  const n = SCENE_SIZE;
  const base = createDocument({ id: 'masks', width: n, height: n });
  const s = (v: number) => Math.round((v * 255) / (n - 1));
  const bg = layer(base, 'bg', pattern(n, n, (x, y) => [s(y), 90, s(x), 255]));
  const a = { ...layer(base, 'a', pattern(n, n, () => [250, 200, 40, 255])), mask: gradientMask(base, (x, y) => ((x >> 3) + (y >> 3)) % 2 ? 255 : s(x), 255) };
  const b = { ...layer(base, 'b', pattern(n, n, (x) => [20, s(x), 230, 200]), { blendMode: 'multiply' }), mask: gradientMask(base, (x, y) => (Math.hypot(x - 80, y - 80) < 50 ? 255 : 0), 0, 0.7) };
  const c = layer(base, 'c', pattern(n, n, (x, y) => ((x + y) % 32 < 16 ? [0, 255, 120, 255] : [0, 0, 0, 0])), { blendMode: 'screen' });
  const d = layer(base, 'd', pattern(n, n, (x, y) => (y > 100 ? [255, 255, 255, 180] : [0, 0, 0, 0])), { blendMode: 'overlay' });
  const iso = { ...createGroupLayer('iso', 'iso', [c]), blendMode: 'normal' as GroupBlendMode, opacity: 0.8, mask: gradientMask(base, (_x, y) => s(y), 255) };
  const pass = { ...createGroupLayer('pass', 'pass', [d]), mask: gradientMask(base, (x) => 255 - s(x), 255) };
  return { ...base, layers: [bg, a, b, iso, pass] };
}
