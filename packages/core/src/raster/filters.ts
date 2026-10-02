/**
 * Pixel filters on RGBA regions. Pure and deterministic (noise is seeded and
 * keyed by absolute document coordinates, so results don't depend on the
 * region a filter happens to run on).
 */
import type { JSONSchema } from '../schema';
import { blurFloat, type Region } from './region';

interface FilterDef {
  readonly label: string;
  readonly description: string;
  readonly defaults: Record<string, unknown>;
  readonly schema: Record<string, JSONSchema>;
  /** Extra pixels of context needed around the output area. */
  pad(p: Record<string, unknown>): number;
  apply(src: Region, p: Record<string, unknown>): Region;
}

const n = (p: Record<string, unknown>, k: string, d: number) => (typeof p[k] === 'number' ? (p[k] as number) : d);
const range = (min: number, max: number, def: number, title: string): JSONSchema => ({ type: 'number', minimum: min, maximum: max, default: def, title });

/** Premultiplied float copy (blurring straight alpha creates dark fringes). */
function toPremul(r: Region): Float32Array {
  const f = new Float32Array(r.data.length);
  for (let i = 0; i < f.length; i += 4) {
    const a = r.data[i + 3] / 255;
    f[i] = r.data[i] * a;
    f[i + 1] = r.data[i + 1] * a;
    f[i + 2] = r.data[i + 2] * a;
    f[i + 3] = r.data[i + 3];
  }
  return f;
}

function fromPremul(f: Float32Array, like: Region): Region {
  const data = new Uint8ClampedArray(f.length);
  for (let i = 0; i < f.length; i += 4) {
    const a = f[i + 3];
    const ar = Math.round(a);
    data[i + 3] = ar;
    if (ar <= 0) continue;
    const k = 255 / a;
    data[i] = Math.round(f[i] * k);
    data[i + 1] = Math.round(f[i + 1] * k);
    data[i + 2] = Math.round(f[i + 2] * k);
  }
  return { ...like, data };
}

export function gaussianBlurRegion(r: Region, radius: number): Region {
  const f = toPremul(r);
  blurFloat(f, r.width, r.height, 4, radius / 2);
  return fromPremul(f, r);
}

/** Deterministic hash → [0, 1). */
function hash01(x: number, y: number, seed: number, salt: number): number {
  let h = (Math.imul(x, 374761393) + Math.imul(y, 668265263) + Math.imul(seed, 2147483647) + Math.imul(salt, 1274126177)) | 0;
  h = Math.imul(h ^ (h >>> 13), 1103515245);
  h ^= h >>> 16;
  return (h >>> 0) / 4294967296;
}

const luma = (d: Uint8ClampedArray, i: number) => 0.299 * d[i] + 0.587 * d[i + 1] + 0.114 * d[i + 2];

export const FILTERS = {
  gaussianBlur: {
    label: 'Gaussian Blur',
    description: 'Soft blur; radius in pixels (≈ 2 standard deviations).',
    defaults: { radius: 4 },
    schema: { radius: range(0.1, 250, 4, 'Radius (px)') },
    pad: (p) => Math.ceil(n(p, 'radius', 4) * 1.5) + 2,
    apply: (r, p) => gaussianBlurRegion(r, n(p, 'radius', 4)),
  },
  motionBlur: {
    label: 'Motion Blur',
    description: 'Directional blur along an angle (degrees) over a distance (pixels).',
    defaults: { angle: 0, distance: 20 },
    schema: { angle: range(-180, 180, 0, 'Angle (°)'), distance: range(1, 200, 20, 'Distance (px)') },
    pad: (p) => Math.ceil(n(p, 'distance', 20) / 2) + 1,
    apply(r, p) {
      const dist = Math.max(1, Math.round(n(p, 'distance', 20)));
      const a = (n(p, 'angle', 0) * Math.PI) / 180;
      const offs: [number, number][] = [];
      for (let i = 0; i < dist; i++) {
        const t = i - (dist - 1) / 2;
        offs.push([Math.round(t * Math.cos(a)), Math.round(-t * Math.sin(a))]);
      }
      const src = toPremul(r);
      const out = new Float32Array(src.length);
      const { width: w, height: h } = r;
      for (let y = 0; y < h; y++) {
        for (let x = 0; x < w; x++) {
          let s0 = 0, s1 = 0, s2 = 0, s3 = 0;
          for (const [dx, dy] of offs) {
            const sx = Math.min(w - 1, Math.max(0, x + dx));
            const sy = Math.min(h - 1, Math.max(0, y + dy));
            const i = (sy * w + sx) * 4;
            s0 += src[i]; s1 += src[i + 1]; s2 += src[i + 2]; s3 += src[i + 3];
          }
          const o = (y * w + x) * 4;
          out[o] = s0 / dist; out[o + 1] = s1 / dist; out[o + 2] = s2 / dist; out[o + 3] = s3 / dist;
        }
      }
      return fromPremul(out, r);
    },
  },
  unsharpMask: {
    label: 'Unsharp Mask',
    description: 'Sharpen edges: amount (%), radius (px), threshold (levels below which differences are ignored).',
    defaults: { amount: 100, radius: 2, threshold: 0 },
    schema: { amount: range(1, 500, 100, 'Amount (%)'), radius: range(0.1, 250, 2, 'Radius (px)'), threshold: range(0, 255, 0, 'Threshold') },
    pad: (p) => Math.ceil(n(p, 'radius', 2) * 1.5) + 2,
    apply(r, p) {
      const blurred = gaussianBlurRegion(r, n(p, 'radius', 2));
      const amount = n(p, 'amount', 100) / 100;
      const threshold = n(p, 'threshold', 0);
      const data = new Uint8ClampedArray(r.data);
      for (let i = 0; i < data.length; i += 4) {
        for (let c = 0; c < 3; c++) {
          const diff = r.data[i + c] - blurred.data[i + c];
          if (Math.abs(diff) >= threshold) data[i + c] = Math.round(r.data[i + c] + amount * diff);
        }
      }
      return { ...r, data };
    },
  },
  highPass: {
    label: 'High Pass',
    description: 'Keep only detail above the radius (px), around mid-grey; use with Overlay/Soft Light to sharpen.',
    defaults: { radius: 10 },
    schema: { radius: range(0.1, 250, 10, 'Radius (px)') },
    pad: (p) => Math.ceil(n(p, 'radius', 10) * 1.5) + 2,
    apply(r, p) {
      const blurred = gaussianBlurRegion(r, n(p, 'radius', 10));
      const data = new Uint8ClampedArray(r.data);
      for (let i = 0; i < data.length; i += 4) for (let c = 0; c < 3; c++) data[i + c] = Math.round(r.data[i + c] - blurred.data[i + c] + 128);
      return { ...r, data };
    },
  },
  addNoise: {
    label: 'Add Noise',
    description: 'Add grain: amount (%), uniform or gaussian distribution, optionally monochromatic. Seeded and repeatable.',
    defaults: { amount: 10, distribution: 'gaussian', monochromatic: true, seed: 1 },
    schema: {
      amount: range(0, 400, 10, 'Amount (%)'),
      distribution: { type: 'string', enum: ['uniform', 'gaussian'], default: 'gaussian', title: 'Distribution' },
      monochromatic: { type: 'boolean', default: true, title: 'Monochromatic' },
      seed: { type: 'integer', minimum: 0, maximum: 2147483647, default: 1, title: 'Seed' },
    },
    pad: () => 0,
    apply(r, p) {
      const amount = n(p, 'amount', 10) / 100;
      const gaussian = p.distribution !== 'uniform';
      const mono = p.monochromatic !== false;
      const seed = n(p, 'seed', 1);
      const data = new Uint8ClampedArray(r.data);
      const sample = (x: number, y: number, c: number) => {
        if (!gaussian) return (hash01(x, y, seed, c) * 2 - 1) * 128 * amount;
        const u1 = Math.max(1e-7, hash01(x, y, seed, c * 2 + 10));
        const u2 = hash01(x, y, seed, c * 2 + 11);
        return Math.sqrt(-2 * Math.log(u1)) * Math.cos(2 * Math.PI * u2) * 48 * amount;
      };
      for (let y = 0; y < r.height; y++) {
        for (let x = 0; x < r.width; x++) {
          const i = (y * r.width + x) * 4;
          if (!data[i + 3]) continue;
          const X = r.x + x, Y = r.y + y;
          const m = mono ? sample(X, Y, 0) : 0;
          for (let c = 0; c < 3; c++) data[i + c] = Math.round(data[i + c] + (mono ? m : sample(X, Y, c)));
        }
      }
      return { ...r, data };
    },
  },
  pixelate: {
    label: 'Pixelate',
    description: 'Mosaic: average colors in square cells aligned to the document grid.',
    defaults: { cellSize: 10 },
    schema: { cellSize: { type: 'integer', minimum: 2, maximum: 200, default: 10, title: 'Cell size (px)' } },
    pad: (p) => Math.round(n(p, 'cellSize', 10)),
    apply(r, p) {
      const s = Math.max(2, Math.round(n(p, 'cellSize', 10)));
      const src = toPremul(r);
      const out = new Float32Array(src.length);
      const cx0 = Math.floor(r.x / s), cx1 = Math.floor((r.x + r.width - 1) / s);
      const cy0 = Math.floor(r.y / s), cy1 = Math.floor((r.y + r.height - 1) / s);
      for (let cy = cy0; cy <= cy1; cy++) {
        for (let cx = cx0; cx <= cx1; cx++) {
          const x0 = Math.max(0, cx * s - r.x), x1 = Math.min(r.width, (cx + 1) * s - r.x);
          const y0 = Math.max(0, cy * s - r.y), y1 = Math.min(r.height, (cy + 1) * s - r.y);
          const sum = [0, 0, 0, 0];
          for (let y = y0; y < y1; y++) for (let x = x0; x < x1; x++) for (let c = 0; c < 4; c++) sum[c] += src[(y * r.width + x) * 4 + c];
          const cnt = (x1 - x0) * (y1 - y0);
          for (let y = y0; y < y1; y++) for (let x = x0; x < x1; x++) for (let c = 0; c < 4; c++) out[(y * r.width + x) * 4 + c] = sum[c] / cnt;
        }
      }
      return fromPremul(out, r);
    },
  },
  emboss: {
    label: 'Emboss',
    description: 'Grey relief from edges lit from an angle (°), with height (px) and amount (%).',
    defaults: { angle: 135, height: 3, amount: 100 },
    schema: { angle: range(-180, 180, 135, 'Angle (°)'), height: range(1, 10, 3, 'Height (px)'), amount: range(1, 500, 100, 'Amount (%)') },
    pad: (p) => Math.ceil(n(p, 'height', 3)) + 1,
    apply(r, p) {
      const a = (n(p, 'angle', 135) * Math.PI) / 180;
      const h = n(p, 'height', 3);
      const dx = Math.round(Math.cos(a) * h), dy = Math.round(-Math.sin(a) * h);
      const amt = n(p, 'amount', 100) / 100;
      const { width: w, height: hh } = r;
      const data = new Uint8ClampedArray(r.data);
      const at = (x: number, y: number) => luma(r.data, (Math.min(hh - 1, Math.max(0, y)) * w + Math.min(w - 1, Math.max(0, x))) * 4);
      for (let y = 0; y < hh; y++) {
        for (let x = 0; x < w; x++) {
          const v = Math.round(128 + (at(x + dx, y + dy) - at(x - dx, y - dy)) * amt);
          const i = (y * w + x) * 4;
          data[i] = data[i + 1] = data[i + 2] = v;
        }
      }
      return { ...r, data };
    },
  },
} satisfies Record<string, FilterDef>;

export type FilterKind = keyof typeof FILTERS;
export const FILTER_KINDS = Object.keys(FILTERS) as FilterKind[];
