import { describe, expect, it } from 'vitest';
import { zipSync, strToU8 } from 'fflate';
import { decodePng, encodePng, historyMetadata, readCnva, writeCnva, type PixelLayer } from '../src';
import { makeBus, sameBytes } from './helpers';

describe('png codec', () => {
  it('round-trips RGBA and grayscale', () => {
    const w = 37, h = 19;
    const rgba = new Uint8ClampedArray(w * h * 4).map((_, i) => (i * 31 + (i >> 5)) & 255);
    const out = decodePng(encodePng(rgba, w, h, 4));
    expect(out).toMatchObject({ width: w, height: h, channels: 4 });
    expect(sameBytes(out.pixels, rgba)).toBe(true);
    const gray = new Uint8ClampedArray(w * h).map((_, i) => i & 255);
    expect(decodePng(encodePng(gray, w, h, 1)).pixels).toEqual(gray);
  });

  it('rejects non-PNG data', () => {
    expect(() => decodePng(new Uint8Array(20))).toThrow(/Not a PNG/);
  });
});

describe('.cnva format', () => {
  it('round-trips a layered document with groups and shared tiles', async () => {
    const bus = makeBus(600, 300);
    const a = bus.dispatch<{ layerId: string }>('layer.create', { color: [10, 20, 30, 255] }).layerId;
    bus.dispatch('pixels.fillRect', { layerId: a, x: 5, y: 5, width: 300, height: 10, color: [200, 0, 0, 100] });
    const b = bus.dispatch<{ layerId: string }>('layer.duplicate', { layerId: a }).layerId;
    bus.dispatch('layer.setBlendMode', { layerId: b, blendMode: 'screen' });
    bus.dispatch('layer.setOpacity', { layerId: b, opacity: 55 });
    bus.dispatch('layer.group', { layerIds: [b], name: 'G' });
    const doc = bus.document;

    const bytes = await writeCnva({
      document: doc,
      history: historyMetadata(bus.history.entries, bus.history.cursor),
      thumbnail: { width: 2, height: 1, pixels: new Uint8ClampedArray(8).fill(9) },
    });
    const back = readCnva(bytes);
    expect(back.document.layers.map((l) => l.name)).toEqual(['Layer 1', 'G']);
    const la = back.document.layers[0] as PixelLayer;
    const src = doc.layers[0] as PixelLayer;
    expect(la.tiles.tiles.size).toBe(src.tiles.tiles.size);
    for (const [k, t] of src.tiles.tiles) expect(sameBytes(la.tiles.tiles.get(k)!.data, t.data)).toBe(true);
    const g = back.document.layers[1];
    expect(g.type === 'group' && g.children[0]).toMatchObject({ blendMode: 'screen', opacity: 0.55 });
    // Shared grids stay shared after loading.
    expect(g.type === 'group' && (g.children[0] as PixelLayer).tiles).toBe(la.tiles);
    expect(back.history!.entries.map((e) => e.title)).toEqual(bus.history.entries.map((e) => e.title));
    expect(back.thumbnail!.width).toBe(2);
  });

  it('reports corrupt and future-version files', () => {
    expect(() => readCnva(new Uint8Array([1, 2, 3]))).toThrow(/not a valid/);
    const future = zipSync({ 'document.json': strToU8(JSON.stringify({ format: 'cnva', version: 99, document: {} })) });
    expect(() => readCnva(future)).toThrow(/newer version/);
  });
});
