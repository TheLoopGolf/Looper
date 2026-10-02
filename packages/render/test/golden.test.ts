import { existsSync, mkdirSync, readFileSync, writeFileSync } from 'node:fs';
import { dirname, join } from 'node:path';
import { fileURLToPath } from 'node:url';
import { describe, expect, it } from 'vitest';
import { BLEND_MODE_IDS, decodePng, encodePng } from '@canvas-ai/core';
import { renderDocumentCPU } from '../src';
import { ADJUSTMENT_KINDS } from '@canvas-ai/core';
import { adjustmentScene, blendScene, groupScene, maskScene, SCENE_SIZE } from '../src/testing/scenes';

const GOLDEN_DIR = join(dirname(fileURLToPath(import.meta.url)), 'golden');
const UPDATE = process.env.UPDATE_GOLDENS === '1';

/** Max per-channel difference between two RGBA8 buffers. */
export function maxDiff(a: Uint8ClampedArray, b: Uint8ClampedArray): number {
  let m = 0;
  for (let i = 0; i < a.length; i++) m = Math.max(m, Math.abs(a[i] - b[i]));
  return m;
}

function checkGolden(name: string, pixels: Uint8ClampedArray) {
  const file = join(GOLDEN_DIR, `${name}.png`);
  if (UPDATE || !existsSync(file)) {
    mkdirSync(GOLDEN_DIR, { recursive: true });
    writeFileSync(file, encodePng(pixels, SCENE_SIZE, SCENE_SIZE, 4, 9));
    if (!UPDATE) throw new Error(`Golden ${name}.png was missing and has been created; re-run to verify`);
    return;
  }
  const golden = decodePng(readFileSync(file));
  expect(golden.width).toBe(SCENE_SIZE);
  expect(maxDiff(golden.pixels, pixels)).toBeLessThanOrEqual(1);
}

describe('CPU compositor golden images (≤ 1 LSB)', () => {
  it.each(BLEND_MODE_IDS)('blend mode %s', (mode) => {
    checkGolden(`blend-${mode}`, renderDocumentCPU(blendScene(mode)));
  });

  it.each(['multiply', 'normal', 'passThrough'] as const)('groups (%s)', (mode) => {
    checkGolden(`groups-${mode}`, renderDocumentCPU(groupScene(mode)));
  });

  it.each(ADJUSTMENT_KINDS)('adjustment layer %s', (kind) => {
    checkGolden(`adjust-${kind}`, renderDocumentCPU(adjustmentScene(kind)));
  });

  it('adjustment layer with a blend mode (hue/sat in Color mode)', () => {
    checkGolden('adjust-hueSaturation-color', renderDocumentCPU(adjustmentScene('hueSaturation', 'color')));
  });

  it('layer and group masks', () => {
    checkGolden('masks', renderDocumentCPU(maskScene()));
  });
});
