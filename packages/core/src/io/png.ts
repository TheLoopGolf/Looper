import { unzlibSync, zlibSync } from 'fflate';

/**
 * Minimal, deterministic PNG codec used for `.cnva` tiles and golden-image
 * tests (works in workers and Node, no canvas needed). Arbitrary user images
 * are decoded by the platform (createImageBitmap) instead; this decoder only
 * needs to cover non-interlaced 8/16-bit images.
 */

const SIGNATURE = [137, 80, 78, 71, 13, 10, 26, 10];

const CRC_TABLE = (() => {
  const t = new Uint32Array(256);
  for (let n = 0; n < 256; n++) {
    let c = n;
    for (let k = 0; k < 8; k++) c = c & 1 ? 0xedb88320 ^ (c >>> 1) : c >>> 1;
    t[n] = c >>> 0;
  }
  return t;
})();

function crc32(data: Uint8Array, start: number, end: number): number {
  let c = 0xffffffff;
  for (let i = start; i < end; i++) c = CRC_TABLE[(c ^ data[i]) & 0xff] ^ (c >>> 8);
  return (c ^ 0xffffffff) >>> 0;
}

export interface DecodedImage {
  width: number;
  height: number;
  /** 1 (gray, used for masks) or 4 (RGBA). */
  channels: 1 | 4;
  pixels: Uint8ClampedArray;
}

function paeth(a: number, b: number, c: number): number {
  const p = a + b - c;
  const pa = Math.abs(p - a);
  const pb = Math.abs(p - b);
  const pc = Math.abs(p - c);
  return pa <= pb && pa <= pc ? a : pb <= pc ? b : c;
}

export function encodePng(
  pixels: Uint8ClampedArray | Uint8Array,
  width: number,
  height: number,
  channels: 1 | 4 = 4,
  level: 0 | 1 | 2 | 3 | 4 | 5 | 6 | 7 | 8 | 9 = 6,
): Uint8Array {
  if (pixels.length !== width * height * channels) throw new Error('Pixel buffer size mismatch');
  const stride = width * channels;
  const raw = new Uint8Array((stride + 1) * height);
  const candidate = new Uint8Array(stride);
  for (let y = 0; y < height; y++) {
    const row = y * stride;
    const prev = row - stride;
    // Pick the filter with the smallest sum of absolute (signed) residuals — the standard heuristic.
    let bestFilter = 0;
    let bestScore = Infinity;
    const out = raw.subarray(y * (stride + 1) + 1, (y + 1) * (stride + 1));
    for (const filter of [0, 1, 2, 4]) {
      let score = 0;
      for (let i = 0; i < stride; i++) {
        const x = pixels[row + i];
        const a = i >= channels ? pixels[row + i - channels] : 0;
        const b = y > 0 ? pixels[prev + i] : 0;
        const c = y > 0 && i >= channels ? pixels[prev + i - channels] : 0;
        const v = (x - (filter === 0 ? 0 : filter === 1 ? a : filter === 2 ? b : paeth(a, b, c))) & 0xff;
        candidate[i] = v;
        score += v < 128 ? v : 256 - v;
      }
      if (score < bestScore) {
        bestScore = score;
        bestFilter = filter;
        out.set(candidate);
      }
    }
    raw[y * (stride + 1)] = bestFilter;
  }
  const idat = zlibSync(raw, { level });

  const chunks: [string, Uint8Array][] = [];
  const ihdr = new Uint8Array(13);
  const dv = new DataView(ihdr.buffer);
  dv.setUint32(0, width);
  dv.setUint32(4, height);
  ihdr[8] = 8;
  ihdr[9] = channels === 4 ? 6 : 0;
  chunks.push(['IHDR', ihdr], ['IDAT', idat], ['IEND', new Uint8Array(0)]);

  const total = 8 + chunks.reduce((n, [, d]) => n + 12 + d.length, 0);
  const png = new Uint8Array(total);
  png.set(SIGNATURE, 0);
  const view = new DataView(png.buffer);
  let o = 8;
  for (const [type, data] of chunks) {
    view.setUint32(o, data.length);
    for (let i = 0; i < 4; i++) png[o + 4 + i] = type.charCodeAt(i);
    png.set(data, o + 8);
    view.setUint32(o + 8 + data.length, crc32(png, o + 4, o + 8 + data.length));
    o += 12 + data.length;
  }
  return png;
}

export function decodePng(bytes: Uint8Array): DecodedImage {
  for (let i = 0; i < 8; i++) if (bytes[i] !== SIGNATURE[i]) throw new Error('Not a PNG file');
  const view = new DataView(bytes.buffer, bytes.byteOffset, bytes.byteLength);
  let width = 0;
  let height = 0;
  let bitDepth = 0;
  let colorType = 0;
  let palette: Uint8Array | null = null;
  let trns: Uint8Array | null = null;
  const idat: Uint8Array[] = [];
  let o = 8;
  while (o + 8 <= bytes.length) {
    const len = view.getUint32(o);
    const type = String.fromCharCode(bytes[o + 4], bytes[o + 5], bytes[o + 6], bytes[o + 7]);
    const data = bytes.subarray(o + 8, o + 8 + len);
    if (type === 'IHDR') {
      width = view.getUint32(o + 8);
      height = view.getUint32(o + 12);
      bitDepth = data[8];
      colorType = data[9];
      if (data[12] !== 0) throw new Error('Interlaced PNGs are not supported by the built-in decoder');
    } else if (type === 'PLTE') palette = data;
    else if (type === 'tRNS') trns = data;
    else if (type === 'IDAT') idat.push(data);
    else if (type === 'IEND') break;
    o += 12 + len;
  }
  const samplesPerPixel = { 0: 1, 2: 3, 3: 1, 4: 2, 6: 4 }[colorType];
  if (!samplesPerPixel || !width || !height) throw new Error('Unsupported or corrupt PNG');
  if (bitDepth !== 8 && !(bitDepth === 16 && colorType !== 3)) throw new Error(`Unsupported PNG bit depth ${bitDepth}`);

  const joined = new Uint8Array(idat.reduce((n, d) => n + d.length, 0));
  let p = 0;
  for (const d of idat) {
    joined.set(d, p);
    p += d.length;
  }
  const raw = unzlibSync(joined);
  const bpp = samplesPerPixel * (bitDepth / 8);
  const stride = width * bpp;
  const data = new Uint8Array(stride * height);
  for (let y = 0; y < height; y++) {
    const filter = raw[y * (stride + 1)];
    const src = y * (stride + 1) + 1;
    const row = y * stride;
    for (let i = 0; i < stride; i++) {
      const a = i >= bpp ? data[row + i - bpp] : 0;
      const b = y > 0 ? data[row - stride + i] : 0;
      const c = y > 0 && i >= bpp ? data[row - stride + i - bpp] : 0;
      const x = raw[src + i];
      data[row + i] = (x + (filter === 0 ? 0 : filter === 1 ? a : filter === 2 ? b : filter === 3 ? (a + b) >> 1 : paeth(a, b, c))) & 0xff;
    }
  }

  const step = bitDepth / 8; // take the high byte of 16-bit samples
  const n = width * height;
  if (colorType === 0 && !trns) {
    const out = new Uint8ClampedArray(n);
    for (let i = 0; i < n; i++) out[i] = data[i * step];
    return { width, height, channels: 1, pixels: out };
  }
  const out = new Uint8ClampedArray(n * 4);
  for (let i = 0; i < n; i++) {
    const s = i * bpp;
    let r: number, g: number, b: number, a = 255;
    switch (colorType) {
      case 0:
        r = g = b = data[s];
        if (trns && trns.length >= 2 && data[s] === trns[step === 2 ? 0 : 1]) a = 0;
        break;
      case 2:
        r = data[s];
        g = data[s + step];
        b = data[s + 2 * step];
        break;
      case 3: {
        const idx = data[s];
        r = palette![idx * 3];
        g = palette![idx * 3 + 1];
        b = palette![idx * 3 + 2];
        if (trns && idx < trns.length) a = trns[idx];
        break;
      }
      case 4:
        r = g = b = data[s];
        a = data[s + step];
        break;
      default:
        r = data[s];
        g = data[s + step];
        b = data[s + 2 * step];
        a = data[s + 3 * step];
    }
    out.set([r, g, b, a], i * 4);
  }
  return { width, height, channels: 4, pixels: out };
}
