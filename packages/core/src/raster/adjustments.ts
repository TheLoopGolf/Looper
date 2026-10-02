/**
 * Adjustments. Each adjustment compiles to
 *   1. a per-channel 256-entry lookup table (exact for separable ops), then
 *   2. an optional per-pixel kernel (hue/saturation, black & white, …).
 * The CPU path here is the reference; the WebGL2/WebGPU shaders implement the
 * same kernels and are golden-tested against it (≤ 1 LSB).
 */
import type { JSONSchema } from '../schema';

export const KERNEL = { none: 0, hueSat: 1, vibrance: 2, preserveLum: 3, blackWhite: 4, photoFilter: 5, threshold: 6 } as const;

export interface CompiledAdjustment {
  /** 256 × RGBA lookup table (alpha unused). */
  readonly lut: Uint8Array;
  readonly kernel: number;
  /** Kernel parameters (16 floats = 4 vec4 in shaders). */
  readonly params: Float32Array;
}

type Params = Record<string, unknown>;
type Curve = readonly (readonly [number, number])[];

const num = (p: Params, k: string, d: number) => (typeof p[k] === 'number' ? (p[k] as number) : d);
const clamp01 = (v: number) => Math.min(1, Math.max(0, v));

interface AdjustmentDef {
  readonly label: string;
  readonly description: string;
  readonly defaults: Params;
  readonly schema: Record<string, JSONSchema>;
  compile(p: Params): CompiledAdjustment;
}

function lutFrom(fr: (v: number) => number, fg = fr, fb = fr): Uint8Array {
  const lut = new Uint8Array(256 * 4);
  for (let v = 0; v < 256; v++) {
    lut[v * 4] = Math.round(Math.min(255, Math.max(0, fr(v))));
    lut[v * 4 + 1] = Math.round(Math.min(255, Math.max(0, fg(v))));
    lut[v * 4 + 2] = Math.round(Math.min(255, Math.max(0, fb(v))));
    lut[v * 4 + 3] = 255;
  }
  return lut;
}

export const IDENTITY_LUT = lutFrom((v) => v);
const noKernel = () => new Float32Array(16);
const compiled = (lut: Uint8Array, kernel = 0, params = noKernel()): CompiledAdjustment => ({ lut, kernel, params });

/** Monotone cubic (Fritsch–Carlson) curve through points on 0..255, sampled to 256 values. */
export function curveTable(points: Curve): number[] {
  const pts = [...points].map(([x, y]) => [Math.min(255, Math.max(0, x)), Math.min(255, Math.max(0, y))] as const).sort((a, b) => a[0] - b[0]);
  const uniq = pts.filter((p, i) => i === 0 || p[0] !== pts[i - 1][0]);
  if (uniq.length < 2) return Array.from({ length: 256 }, (_, v) => (uniq.length ? uniq[0][1] : v));
  const n = uniq.length;
  const dx = [], slope = [];
  for (let i = 0; i < n - 1; i++) {
    dx.push(uniq[i + 1][0] - uniq[i][0]);
    slope.push((uniq[i + 1][1] - uniq[i][1]) / dx[i]);
  }
  const m = [slope[0]];
  for (let i = 1; i < n - 1; i++) m.push(slope[i - 1] * slope[i] <= 0 ? 0 : (3 * (dx[i - 1] + dx[i])) / ((2 * dx[i] + dx[i - 1]) / slope[i - 1] + (dx[i] + 2 * dx[i - 1]) / slope[i]));
  m.push(slope[n - 2]);
  const out: number[] = [];
  for (let v = 0; v < 256; v++) {
    if (v <= uniq[0][0]) out.push(uniq[0][1]);
    else if (v >= uniq[n - 1][0]) out.push(uniq[n - 1][1]);
    else {
      let i = 0;
      while (uniq[i + 1][0] < v) i++;
      const h = dx[i], t = (v - uniq[i][0]) / h;
      const t2 = t * t, t3 = t2 * t;
      out.push((2 * t3 - 3 * t2 + 1) * uniq[i][1] + (t3 - 2 * t2 + t) * h * m[i] + (-2 * t3 + 3 * t2) * uniq[i + 1][1] + (t3 - t2) * h * m[i + 1]);
    }
  }
  return out;
}

const srgbToLinear = (c: number) => (c <= 0.04045 ? c / 12.92 : ((c + 0.055) / 1.055) ** 2.4);
const linearToSrgb = (c: number) => (c <= 0.0031308 ? c * 12.92 : 1.055 * c ** (1 / 2.4) - 0.055);

const range = (min: number, max: number, def: number, title: string, description?: string): JSONSchema => ({
  type: 'number',
  minimum: min,
  maximum: max,
  default: def,
  title,
  description,
});
const colorSchema = (title: string): JSONSchema => ({ type: 'array', minItems: 3, maxItems: 4, items: { type: 'integer', minimum: 0, maximum: 255 }, title });
const curveSchema = (title: string): JSONSchema => ({
  type: 'array',
  minItems: 2,
  maxItems: 16,
  title,
  items: { type: 'array', minItems: 2, maxItems: 2, items: { type: 'number', minimum: 0, maximum: 255 } },
});

const BW_DEFAULTS = { reds: 40, yellows: 60, greens: 40, cyans: 60, blues: 20, magentas: 80 } as const;

export const ADJUSTMENTS = {
  brightnessContrast: {
    label: 'Brightness/Contrast',
    description: 'Shift brightness and stretch or flatten contrast around mid-grey.',
    defaults: { brightness: 0, contrast: 0 },
    schema: { brightness: range(-150, 150, 0, 'Brightness'), contrast: range(-50, 100, 0, 'Contrast') },
    compile(p) {
      const b = num(p, 'brightness', 0);
      const k = Math.tan(((45 + 44 * (num(p, 'contrast', 0) / 100)) * Math.PI) / 180);
      return compiled(lutFrom((v) => (v + b - 127.5) * k + 127.5));
    },
  },
  levels: {
    label: 'Levels',
    description: 'Remap input black/white points with a midtone gamma, then the output range. Optionally for one channel.',
    defaults: { channel: 'rgb', inBlack: 0, inWhite: 255, gamma: 1, outBlack: 0, outWhite: 255 },
    schema: {
      channel: { type: 'string', enum: ['rgb', 'red', 'green', 'blue'], default: 'rgb', title: 'Channel' },
      inBlack: range(0, 253, 0, 'Input black'),
      inWhite: range(2, 255, 255, 'Input white'),
      gamma: range(0.1, 9.99, 1, 'Gamma'),
      outBlack: range(0, 255, 0, 'Output black'),
      outWhite: range(0, 255, 255, 'Output white'),
    },
    compile(p) {
      const inB = num(p, 'inBlack', 0), inW = Math.max(inB + 1, num(p, 'inWhite', 255));
      const g = num(p, 'gamma', 1), oB = num(p, 'outBlack', 0), oW = num(p, 'outWhite', 255);
      const f = (v: number) => oB + clamp01((v - inB) / (inW - inB)) ** (1 / g) * (oW - oB);
      const id = (v: number) => v;
      const ch = p.channel ?? 'rgb';
      return compiled(lutFrom(ch === 'rgb' || ch === 'red' ? f : id, ch === 'rgb' || ch === 'green' ? f : id, ch === 'rgb' || ch === 'blue' ? f : id));
    },
  },
  curves: {
    label: 'Curves',
    description: 'Tone curves through control points [[input, output], …] (0-255) for RGB and optionally each channel.',
    defaults: { rgb: [[0, 0], [255, 255]] },
    schema: { rgb: curveSchema('RGB curve'), red: curveSchema('Red curve'), green: curveSchema('Green curve'), blue: curveSchema('Blue curve') },
    compile(p) {
      const master = curveTable((p.rgb as Curve) ?? [[0, 0], [255, 255]]);
      const ch = (k: string) => (p[k] ? curveTable(p[k] as Curve) : null);
      const [r, g, b] = [ch('red'), ch('green'), ch('blue')];
      const at = (t: number[], v: number) => t[Math.round(Math.min(255, Math.max(0, v)))];
      return compiled(lutFrom((v) => at(master, r ? r[v] : v), (v) => at(master, g ? g[v] : v), (v) => at(master, b ? b[v] : v)));
    },
  },
  exposure: {
    label: 'Exposure',
    description: 'Photographic exposure in stops (linear light), plus offset and gamma correction.',
    defaults: { exposure: 0, offset: 0, gamma: 1 },
    schema: { exposure: range(-5, 5, 0, 'Exposure (stops)'), offset: range(-0.5, 0.5, 0, 'Offset'), gamma: range(0.01, 9.99, 1, 'Gamma') },
    compile(p) {
      const e = 2 ** num(p, 'exposure', 0), o = num(p, 'offset', 0), g = num(p, 'gamma', 1);
      return compiled(lutFrom((v) => linearToSrgb(clamp01(Math.max(0, srgbToLinear(v / 255) * e + o) ** (1 / g))) * 255));
    },
  },
  hueSaturation: {
    label: 'Hue/Saturation',
    description: 'Rotate hue (degrees), scale saturation and lightness (-100..100). Colorize tints the whole image with one hue.',
    defaults: { hue: 0, saturation: 0, lightness: 0, colorize: false },
    schema: {
      hue: range(-180, 180, 0, 'Hue'),
      saturation: range(-100, 100, 0, 'Saturation'),
      lightness: range(-100, 100, 0, 'Lightness'),
      colorize: { type: 'boolean', default: false, title: 'Colorize' },
    },
    compile(p) {
      return compiled(IDENTITY_LUT, KERNEL.hueSat, Float32Array.of(num(p, 'hue', 0), num(p, 'saturation', 0) / 100, num(p, 'lightness', 0) / 100, p.colorize ? 1 : 0, ...new Array(12).fill(0)));
    },
  },
  vibrance: {
    label: 'Vibrance',
    description: 'Vibrance boosts muted colors more than saturated ones; saturation scales all colors (-100..100).',
    defaults: { vibrance: 0, saturation: 0 },
    schema: { vibrance: range(-100, 100, 0, 'Vibrance'), saturation: range(-100, 100, 0, 'Saturation') },
    compile(p) {
      return compiled(IDENTITY_LUT, KERNEL.vibrance, Float32Array.of(num(p, 'vibrance', 0) / 100, num(p, 'saturation', 0) / 100, ...new Array(14).fill(0)));
    },
  },
  colorBalance: {
    label: 'Color Balance',
    description: 'Shift shadows, midtones and highlights along cyan–red, magenta–green and yellow–blue axes (each -100..100).',
    defaults: { shadows: [0, 0, 0], midtones: [0, 0, 0], highlights: [0, 0, 0], preserveLuminosity: true },
    schema: {
      shadows: { type: 'array', minItems: 3, maxItems: 3, items: { type: 'number', minimum: -100, maximum: 100 }, title: 'Shadows' },
      midtones: { type: 'array', minItems: 3, maxItems: 3, items: { type: 'number', minimum: -100, maximum: 100 }, title: 'Midtones' },
      highlights: { type: 'array', minItems: 3, maxItems: 3, items: { type: 'number', minimum: -100, maximum: 100 }, title: 'Highlights' },
      preserveLuminosity: { type: 'boolean', default: true, title: 'Preserve luminosity' },
    },
    compile(p) {
      const tri = (k: string) => ((p[k] as number[] | undefined) ?? [0, 0, 0]).map((v) => v / 100);
      const [s, m, h] = [tri('shadows'), tri('midtones'), tri('highlights')];
      const a = 0.25, b = 0.333, scale = 0.7;
      const ch = (c: number) => (v: number) => {
        const x = v / 255;
        const sw = clamp01((x - b) / -a + 0.5) * scale;
        const mw = clamp01((x - b) / a + 0.5) * clamp01((x + b - 1) / -a + 0.5) * scale;
        const hw = clamp01((x + b - 1) / a + 0.5) * scale;
        return clamp01(x + s[c] * sw + m[c] * mw + h[c] * hw) * 255;
      };
      const pl = p.preserveLuminosity !== false;
      return compiled(lutFrom(ch(0), ch(1), ch(2)), pl ? KERNEL.preserveLum : 0);
    },
  },
  blackWhite: {
    label: 'Black & White',
    description: 'Convert to grayscale with per-hue brightness weights (percent), optionally tinted.',
    defaults: { ...BW_DEFAULTS, tint: false, tintColor: [225, 211, 179], tintAmount: 100 },
    schema: {
      reds: range(-200, 300, 40, 'Reds'),
      yellows: range(-200, 300, 60, 'Yellows'),
      greens: range(-200, 300, 40, 'Greens'),
      cyans: range(-200, 300, 60, 'Cyans'),
      blues: range(-200, 300, 20, 'Blues'),
      magentas: range(-200, 300, 80, 'Magentas'),
      tint: { type: 'boolean', default: false, title: 'Tint' },
      tintColor: colorSchema('Tint color'),
      tintAmount: range(0, 100, 100, 'Tint amount'),
    },
    compile(p) {
      const w = (Object.keys(BW_DEFAULTS) as (keyof typeof BW_DEFAULTS)[]).map((k) => num(p, k, BW_DEFAULTS[k]) / 100);
      const tc = ((p.tintColor as number[]) ?? [225, 211, 179]).map((v) => v / 255);
      return compiled(IDENTITY_LUT, KERNEL.blackWhite, Float32Array.of(...w, 0, 0, tc[0], tc[1], tc[2], p.tint ? num(p, 'tintAmount', 100) / 100 : 0, 0, 0, 0, 0));
    },
  },
  photoFilter: {
    label: 'Photo Filter',
    description: 'Simulate a colored lens filter (e.g. warm orange or cool blue) at a density 0-100.',
    defaults: { color: [236, 138, 0], density: 25, preserveLuminosity: true },
    schema: { color: colorSchema('Filter color'), density: range(0, 100, 25, 'Density'), preserveLuminosity: { type: 'boolean', default: true, title: 'Preserve luminosity' } },
    compile(p) {
      const c = ((p.color as number[]) ?? [236, 138, 0]).map((v) => v / 255);
      return compiled(IDENTITY_LUT, KERNEL.photoFilter, Float32Array.of(c[0], c[1], c[2], num(p, 'density', 25) / 100, p.preserveLuminosity === false ? 0 : 1, ...new Array(11).fill(0)));
    },
  },
  invert: {
    label: 'Invert',
    description: 'Invert all color channels (negative).',
    defaults: {},
    schema: {},
    compile: () => compiled(lutFrom((v) => 255 - v)),
  },
  posterize: {
    label: 'Posterize',
    description: 'Reduce each channel to a number of tone levels (2-255).',
    defaults: { levels: 4 },
    schema: { levels: { type: 'integer', minimum: 2, maximum: 255, default: 4, title: 'Levels' } },
    compile(p) {
      const n = Math.max(2, Math.round(num(p, 'levels', 4)));
      return compiled(lutFrom((v) => (Math.round((v / 255) * (n - 1)) / (n - 1)) * 255));
    },
  },
  threshold: {
    label: 'Threshold',
    description: 'Pure black and white: pixels with luminance at or above the level (1-255) become white.',
    defaults: { level: 128 },
    schema: { level: { type: 'integer', minimum: 1, maximum: 255, default: 128, title: 'Level' } },
    compile: (p) => compiled(IDENTITY_LUT, KERNEL.threshold, Float32Array.of(num(p, 'level', 128), ...new Array(15).fill(0))),
  },
} satisfies Record<string, AdjustmentDef>;

export type AdjustmentKind = keyof typeof ADJUSTMENTS;
export const ADJUSTMENT_KINDS: string[] = Object.keys(ADJUSTMENTS);

export function isAdjustmentKind(kind: unknown): kind is AdjustmentKind {
  return typeof kind === 'string' && kind in ADJUSTMENTS;
}

const compileCache = new WeakMap<object, CompiledAdjustment>();

/** Compiles (and caches by params object identity) an adjustment. */
export function compileAdjustment(kind: string, params: Params): CompiledAdjustment {
  const cached = compileCache.get(params);
  if (cached) return cached;
  if (!isAdjustmentKind(kind)) throw new Error(`Unknown adjustment: ${kind}`);
  const def: AdjustmentDef = ADJUSTMENTS[kind];
  const c = def.compile({ ...def.defaults, ...params });
  compileCache.set(params, c);
  return c;
}

// ---------------------------------------------------------------------------
// CPU kernels (reference). Shaders: render/src/gl/adjust-glsl.ts, webgpu/adjust-wgsl.ts
// ---------------------------------------------------------------------------

type RGB = [number, number, number];
const lum = (c: RGB) => 0.3 * c[0] + 0.59 * c[1] + 0.11 * c[2];

function clipColor(c: RGB): RGB {
  const l = lum(c);
  const n = Math.min(c[0], c[1], c[2]);
  const x = Math.max(c[0], c[1], c[2]);
  let o = c;
  if (n < 0) o = o.map((v) => l + ((v - l) * l) / (l - n)) as RGB;
  if (x > 1) o = o.map((v) => l + ((v - l) * (1 - l)) / (x - l)) as RGB;
  return o;
}
const setLum = (c: RGB, l: number): RGB => {
  const d = l - lum(c);
  return clipColor([c[0] + d, c[1] + d, c[2] + d]);
};

function rgbToHsl([r, g, b]: RGB): RGB {
  const mx = Math.max(r, g, b), mn = Math.min(r, g, b);
  const l = (mx + mn) / 2;
  if (mx === mn) return [0, 0, l];
  const d = mx - mn;
  const s = l > 0.5 ? d / (2 - mx - mn) : d / (mx + mn);
  let h: number;
  if (mx === r) h = (g - b) / d + (g < b ? 6 : 0);
  else if (mx === g) h = (b - r) / d + 2;
  else h = (r - g) / d + 4;
  return [h * 60, s, l];
}

function hueToRgb(p: number, q: number, t: number): number {
  if (t < 0) t += 1;
  if (t > 1) t -= 1;
  if (t < 1 / 6) return p + (q - p) * 6 * t;
  if (t < 1 / 2) return q;
  if (t < 2 / 3) return p + (q - p) * (2 / 3 - t) * 6;
  return p;
}

function hslToRgb([h, s, l]: RGB): RGB {
  if (s <= 0) return [l, l, l];
  const q = l < 0.5 ? l * (1 + s) : l + s - l * s;
  const p = 2 * l - q;
  const hh = (((h % 360) + 360) % 360) / 360;
  return [hueToRgb(p, q, hh + 1 / 3), hueToRgb(p, q, hh), hueToRgb(p, q, hh - 1 / 3)];
}

export function applyKernel(kernel: number, p: Float32Array, c0: RGB, x: RGB, rgb8: [number, number, number]): RGB {
  switch (kernel) {
    case KERNEL.hueSat: {
      const [hue, sat, light, colorize] = p;
      let [h, s, l] = rgbToHsl(x);
      if (colorize > 0.5) {
        h = hue < 0 ? hue + 360 : hue;
        s = (sat + 1) / 2;
      } else {
        h += hue;
        s = clamp01(s * (1 + sat));
      }
      l = light < 0 ? l * (1 + light) : l + (1 - l) * light;
      return hslToRgb([h, s, l]);
    }
    case KERNEL.vibrance: {
      const [vib, sat] = p;
      const l = 0.299 * x[0] + 0.587 * x[1] + 0.114 * x[2];
      const sAmt = Math.max(x[0], x[1], x[2]) - Math.min(x[0], x[1], x[2]);
      const f = (1 + vib * (1 - sAmt)) * (1 + sat);
      return x.map((v) => clamp01(l + (v - l) * f)) as RGB;
    }
    case KERNEL.preserveLum:
      return setLum(x, lum(c0));
    case KERNEL.blackWhite: {
      const [r, g, b] = x;
      const w = p; // reds, yellows, greens, cyans, blues, magentas
      let gray: number;
      if (r >= g && r >= b) {
        // primary red; secondary yellow (r,g) or magenta (r,b)
        if (g >= b) gray = b + (g - b) * w[1] + (r - g) * w[0];
        else gray = g + (b - g) * w[5] + (r - b) * w[0];
      } else if (g >= r && g >= b) {
        if (r >= b) gray = b + (r - b) * w[1] + (g - r) * w[2];
        else gray = r + (b - r) * w[3] + (g - b) * w[2];
      } else {
        if (r >= g) gray = g + (r - g) * w[5] + (b - r) * w[4];
        else gray = r + (g - r) * w[3] + (b - g) * w[4];
      }
      gray = clamp01(gray);
      const amt = p[11];
      if (amt <= 0) return [gray, gray, gray];
      const t = setLum([p[8], p[9], p[10]], gray);
      return [gray + (t[0] - gray) * amt, gray + (t[1] - gray) * amt, gray + (t[2] - gray) * amt];
    }
    case KERNEL.photoFilter: {
      const d = p[3];
      const f: RGB = [x[0] * (1 - d + d * p[0]), x[1] * (1 - d + d * p[1]), x[2] * (1 - d + d * p[2])];
      return p[4] > 0.5 ? setLum(f, lum(x)) : f;
    }
    case KERNEL.threshold: {
      // Integer luma on the 8-bit values so every backend agrees exactly.
      const white = 299 * rgb8[0] + 587 * rgb8[1] + 114 * rgb8[2] >= p[0] * 1000;
      return white ? [1, 1, 1] : [0, 0, 0];
    }
    default:
      return x;
  }
}

/** Adjusts one straight-RGBA8 pixel's color (alpha untouched). */
export function adjustPixel(c: CompiledAdjustment, r: number, g: number, b: number): RGB {
  const c0: RGB = [r / 255, g / 255, b / 255];
  const x: RGB = [c.lut[r * 4] / 255, c.lut[g * 4 + 1] / 255, c.lut[b * 4 + 2] / 255];
  if (!c.kernel) return x;
  const y = applyKernel(c.kernel, c.params, c0, x, [c.lut[r * 4], c.lut[g * 4 + 1], c.lut[b * 4 + 2]]);
  return [clamp01(y[0]), clamp01(y[1]), clamp01(y[2])];
}
