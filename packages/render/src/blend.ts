/**
 * CPU reference implementation of layer blending. GPU shaders mirror these
 * formulas exactly (see gl/shaders.ts, webgpu/shaders.ts) and golden tests
 * check they agree within 1 LSB.
 *
 * Model (W3C Compositing & Blending Level 1), straight-alpha RGBA8 in/out:
 *   Cs' = (1 − αb)·Cs + αb·B(Cb, Cs)
 *   αo  = αs + αb·(1 − αs)
 *   Co  = (αs·Cs' + αb·Cb·(1 − αs)) / αo
 * Each blend step is quantized to 8 bits, matching RGBA8 render targets.
 */
import { BLEND_MODE_IDS, type BlendMode } from '@canvas-ai/core';

type Separable = (cb: number, cs: number) => number;

const screen: Separable = (cb, cs) => cb + cs - cb * cs;
const colorBurn: Separable = (cb, cs) => (cb >= 1 ? 1 : cs <= 0 ? 0 : 1 - Math.min(1, (1 - cb) / cs));
const colorDodge: Separable = (cb, cs) => (cb <= 0 ? 0 : cs >= 1 ? 1 : Math.min(1, cb / (1 - cs)));
const hardLight: Separable = (cb, cs) => (cs <= 0.5 ? cb * 2 * cs : screen(cb, 2 * cs - 1));
const softLight: Separable = (cb, cs) => {
  if (cs <= 0.5) return cb - (1 - 2 * cs) * cb * (1 - cb);
  const d = cb <= 0.25 ? ((16 * cb - 12) * cb + 4) * cb : Math.sqrt(cb);
  return cb + (2 * cs - 1) * (d - cb);
};

/** Separable blend functions B(Cb, Cs) on [0,1] channels. */
export const SEPARABLE: Partial<Record<BlendMode, Separable>> = {
  normal: (_cb, cs) => cs,
  darken: Math.min,
  multiply: (cb, cs) => cb * cs,
  colorBurn,
  linearBurn: (cb, cs) => Math.max(0, cb + cs - 1),
  lighten: Math.max,
  screen,
  colorDodge,
  linearDodge: (cb, cs) => Math.min(1, cb + cs),
  overlay: (cb, cs) => hardLight(cs, cb),
  softLight,
  hardLight,
  vividLight: (cb, cs) => (cs <= 0.5 ? colorBurn(cb, 2 * cs) : colorDodge(cb, 2 * cs - 1)),
  linearLight: (cb, cs) => Math.min(1, Math.max(0, cb + 2 * cs - 1)),
  pinLight: (cb, cs) => (cs <= 0.5 ? Math.min(cb, 2 * cs) : Math.max(cb, 2 * cs - 1)),
  // Threshold decided on 8-bit values so float precision can't flip the result.
  hardMix: (cb, cs) => (Math.round(cb * 255) + Math.round(cs * 255) >= 255 ? 1 : 0),
  difference: (cb, cs) => Math.abs(cb - cs),
  exclusion: (cb, cs) => cb + cs - 2 * cb * cs,
  subtract: (cb, cs) => Math.max(0, cb - cs),
  divide: (cb, cs) => (cs <= 0 ? (cb <= 0 ? 0 : 1) : Math.min(1, cb / cs)),
};

type RGB = [number, number, number];

const lum = (c: RGB) => 0.3 * c[0] + 0.59 * c[1] + 0.11 * c[2];

function clipColor(c: RGB): RGB {
  const l = lum(c);
  const n = Math.min(c[0], c[1], c[2]);
  const x = Math.max(c[0], c[1], c[2]);
  let out = c;
  if (n < 0) out = out.map((v) => l + ((v - l) * l) / (l - n)) as RGB;
  if (x > 1) out = out.map((v) => l + ((v - l) * (1 - l)) / (x - l)) as RGB;
  return out;
}

function setLum(c: RGB, l: number): RGB {
  const d = l - lum(c);
  return clipColor([c[0] + d, c[1] + d, c[2] + d]);
}

const sat = (c: RGB) => Math.max(c[0], c[1], c[2]) - Math.min(c[0], c[1], c[2]);

function setSat(c: RGB, s: number): RGB {
  const n = Math.min(c[0], c[1], c[2]);
  const x = Math.max(c[0], c[1], c[2]);
  if (x <= n) return [0, 0, 0];
  return [((c[0] - n) * s) / (x - n), ((c[1] - n) * s) / (x - n), ((c[2] - n) * s) / (x - n)];
}

/** 8-bit integer luma used by darker/lighter color so the comparison is exact on every backend. */
const lum8 = (c: RGB) => 30 * Math.round(c[0] * 255) + 59 * Math.round(c[1] * 255) + 11 * Math.round(c[2] * 255);

export const NON_SEPARABLE: Partial<Record<BlendMode, (cb: RGB, cs: RGB) => RGB>> = {
  hue: (cb, cs) => setLum(setSat(cs, sat(cb)), lum(cb)),
  saturation: (cb, cs) => setLum(setSat(cb, sat(cs)), lum(cb)),
  color: (cb, cs) => setLum(cs, lum(cb)),
  luminosity: (cb, cs) => setLum(cb, lum(cs)),
  darkerColor: (cb, cs) => (lum8(cs) < lum8(cb) ? cs : cb),
  lighterColor: (cb, cs) => (lum8(cs) > lum8(cb) ? cs : cb),
};

export function blendRGB(mode: BlendMode, cb: RGB, cs: RGB): RGB {
  const sep = SEPARABLE[mode];
  if (sep) return [sep(cb[0], cs[0]), sep(cb[1], cs[1]), sep(cb[2], cs[2])];
  const ns = NON_SEPARABLE[mode];
  if (!ns) throw new Error(`Blend mode not implemented: ${mode}`);
  return ns(cb, cs);
}

const q = (v: number) => Math.round(Math.min(1, Math.max(0, v)) * 255);

/**
 * Blends `src` over `dst` in place (both straight RGBA8, same length).
 * `opacity` (0..1) multiplies source alpha.
 */
export function blendBuffers(dst: Uint8ClampedArray, src: Uint8ClampedArray, mode: BlendMode, opacity: number): void {
  const sep = SEPARABLE[mode];
  const ns = NON_SEPARABLE[mode];
  if (!sep && !ns) throw new Error(`Blend mode not implemented: ${mode}`);
  const cb: RGB = [0, 0, 0];
  const cs: RGB = [0, 0, 0];
  for (let i = 0; i < dst.length; i += 4) {
    const as = (src[i + 3] / 255) * opacity;
    if (as <= 0) continue;
    const ab = dst[i + 3] / 255;
    for (let c = 0; c < 3; c++) {
      cb[c] = dst[i + c] / 255;
      cs[c] = src[i + c] / 255;
    }
    const b = sep ? [sep(cb[0], cs[0]), sep(cb[1], cs[1]), sep(cb[2], cs[2])] : ns!(cb, cs);
    const ao = as + ab * (1 - as);
    for (let c = 0; c < 3; c++) {
      const csp = (1 - ab) * cs[c] + ab * b[c];
      dst[i + c] = q((as * csp + ab * cb[c] * (1 - as)) / ao);
    }
    dst[i + 3] = q(ao);
  }
}

/** Pass-through group opacity: dst = lerp(dst, inner, opacity) in premultiplied space. */
export function mixBuffers(dst: Uint8ClampedArray, inner: Uint8ClampedArray, opacity: number): void {
  for (let i = 0; i < dst.length; i += 4) {
    const a0 = dst[i + 3] / 255;
    const a1 = inner[i + 3] / 255;
    const ao = a0 * (1 - opacity) + a1 * opacity;
    for (let c = 0; c < 3; c++) {
      const pc = (dst[i + c] / 255) * a0 * (1 - opacity) + (inner[i + c] / 255) * a1 * opacity;
      dst[i + c] = ao > 0 ? q(pc / ao) : 0;
    }
    dst[i + 3] = q(ao);
  }
}

export { BLEND_MODE_IDS };
