import {
  createDocument,
  createGroupLayer,
  createPixelLayer,
  gridFromPixels,
  type BlendMode,
  type Document,
  type GroupBlendMode,
  type LayerNode,
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
