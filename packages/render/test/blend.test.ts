import { describe, expect, it } from 'vitest';
import { BLEND_MODE_IDS } from '@canvas-ai/core';
import { blendBuffers, blendRGB, mixBuffers } from '../src';

const px = (...v: number[]) => new Uint8ClampedArray(v);

describe('blend formulas', () => {
  it('implements every blend mode', () => {
    for (const m of BLEND_MODE_IDS) expect(() => blendRGB(m, [0.2, 0.5, 0.8], [0.6, 0.4, 0.1])).not.toThrow();
  });

  it.each([
    ['multiply', 0.5, 0.5, 0.25],
    ['screen', 0.5, 0.5, 0.75],
    ['darken', 0.3, 0.7, 0.3],
    ['lighten', 0.3, 0.7, 0.7],
    ['difference', 0.3, 0.7, 0.4],
    ['exclusion', 0.5, 0.5, 0.5],
    ['linearDodge', 0.7, 0.6, 1],
    ['linearBurn', 0.7, 0.6, 0.3],
    ['subtract', 0.7, 0.2, 0.5],
    ['divide', 0.25, 0.5, 0.5],
    ['colorDodge', 0.25, 0.5, 0.5],
    ['colorBurn', 0.75, 0.5, 0.5],
    ['overlay', 0.25, 0.8, 0.4],
    ['hardLight', 0.8, 0.25, 0.4],
    ['pinLight', 0.2, 0.9, 0.8],
    ['linearLight', 0.5, 0.75, 1],
  ] as const)('%s(%d, %d) = %d', (mode, cb, cs, expected) => {
    expect(blendRGB(mode, [cb, cb, cb], [cs, cs, cs])[0]).toBeCloseTo(expected, 6);
  });

  it('non-separable modes preserve the right components', () => {
    const cb: [number, number, number] = [0.8, 0.4, 0.2];
    const cs: [number, number, number] = [0.1, 0.3, 0.9];
    const lum = (c: number[]) => 0.3 * c[0] + 0.59 * c[1] + 0.11 * c[2];
    expect(lum(blendRGB('luminosity', cb, cs))).toBeCloseTo(lum(cs), 6);
    expect(lum(blendRGB('color', cb, cs))).toBeCloseTo(lum(cb), 6);
    expect(lum(blendRGB('hue', cb, cs))).toBeCloseTo(lum(cb), 6);
    const grey = blendRGB('saturation', [0.5, 0.5, 0.5], cs);
    expect(grey[0]).toBeCloseTo(0.5, 6);
  });

  it('composites normal over with straight alpha', () => {
    const dst = px(255, 0, 0, 255);
    blendBuffers(dst, px(0, 0, 255, 128), 'normal', 1);
    expect([...dst]).toEqual([127, 0, 128, 255]);
    const empty = px(0, 0, 0, 0);
    blendBuffers(empty, px(10, 20, 30, 100), 'multiply', 0.5);
    // Blend modes don't apply over transparent backdrop: the source shows through as-is.
    expect([...empty]).toEqual([10, 20, 30, 50]);
  });

  it('mixes pass-through results by opacity', () => {
    const dst = px(200, 0, 0, 255);
    mixBuffers(dst, px(0, 0, 200, 255), 0.25);
    expect([...dst]).toEqual([150, 0, 50, 255]);
  });
});
