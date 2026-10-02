import { describe, expect, it } from 'vitest';
import { DEFAULT_BRUSH, packPoints, readRegion, renderDocumentCPU, StrokeAccumulator, unpackPoints, type PixelLayer } from '../src';
import { raster } from '../src';
import { checkGolden, create, makeBus, pixel, sameBytes } from './helpers';

const stroke = (pts: [number, number, number][]) => pts.flat();

describe('brush engine', () => {
  it('chunked input (live preview) and one-shot command produce identical pixels', () => {
    const pts = unpackPoints(packPoints(Array.from({ length: 40 }, (_, i) => ({ x: 20 + i * 7.3, y: 100 + Math.sin(i / 4) * 60, pressure: 0.3 + (i % 10) / 14 }))));
    const brush = { ...DEFAULT_BRUSH, size: 24, hardness: 0.5, flow: 0.6 };
    const whole = new StrokeAccumulator(brush, 400, 300);
    whole.addPoints(pts);
    const chunked = new StrokeAccumulator(brush, 400, 300);
    for (let i = 0; i < pts.length; i += 3) chunked.addPoints(pts.slice(i, i + 3));
    expect([...whole.tiles.keys()].sort()).toEqual([...chunked.tiles.keys()].sort());
    for (const [k, t] of whole.tiles) expect(Buffer.from(t.buffer).equals(Buffer.from(chunked.tiles.get(k)!.buffer))).toBe(true);
  });

  it('paints, erases, respects the selection and matches the golden', () => {
    const bus = makeBus(256, 160);
    const bg = create(bus, { color: [255, 255, 255, 255] });
    const ink = create(bus);
    bus.dispatch('paint.stroke', { layerId: ink, color: [200, 30, 60, 255], brush: { size: 30, hardness: 1 }, points: stroke([[20, 40, 1], [230, 40, 1]]) });
    bus.dispatch('paint.stroke', { layerId: ink, color: [30, 60, 200, 255], brush: { size: 40, hardness: 0, flow: 0.5 }, points: stroke([[20, 110, 0.2], [128, 80, 1], [230, 120, 0.5]]) });
    expect(pixel((bus.document.layers[1] as PixelLayer).tiles, 128, 40)).toEqual([200, 30, 60, 255]);
    bus.dispatch('paint.stroke', { layerId: ink, mode: 'erase', brush: { size: 20, hardness: 1 }, points: stroke([[128, 0, 1], [128, 159, 1]]) });
    expect(pixel((bus.document.layers[1] as PixelLayer).tiles, 128, 40)[3]).toBe(0);
    bus.dispatch('selection.rect', { x: 0, y: 0, width: 60, height: 160 });
    bus.dispatch('paint.stroke', { layerId: bg, color: [0, 0, 0, 255], brush: { size: 10, hardness: 1 }, points: stroke([[10, 150, 1], [250, 150, 1]]) });
    const bgTiles = (bus.document.layers[0] as PixelLayer).tiles;
    expect(pixel(bgTiles, 30, 150)).toEqual([0, 0, 0, 255]);
    expect(pixel(bgTiles, 100, 150)).toEqual([255, 255, 255, 255]);
    bus.dispatch('selection.deselect', {});
    checkGolden('paint-strokes', renderDocumentCPU(bus.document), 256, 160);
  });

  it('paints masks and the selection (quick mask)', () => {
    const bus = makeBus(100, 100);
    const l = create(bus, { color: [10, 20, 30, 255] });
    bus.dispatch('layer.addMask', { layerId: l });
    bus.dispatch('paint.stroke', { layerId: l, target: 'mask', color: [0, 0, 0, 255], brush: { size: 10, hardness: 1 }, points: stroke([[50, 0, 1], [50, 99, 1]]) });
    expect(renderDocumentCPU(bus.document, { x: 50, y: 50, width: 1, height: 1 })[3]).toBe(0);
    expect(renderDocumentCPU(bus.document, { x: 20, y: 50, width: 1, height: 1 })[3]).toBe(255);
    bus.dispatch('paint.stroke', { target: 'selection', brush: { size: 10, hardness: 1 }, points: stroke([[10, 10, 1], [10, 10, 1]]) });
    expect(bus.document.selection && pixel(bus.document.selection.mask, 10, 10)[0]).toBe(255);
    expect(() => bus.dispatch('paint.stroke', { points: [1, 1, 1] })).toThrow(/layerId is required/);
  });

  it('applyStroke never mutates the source grid', () => {
    const bus = makeBus(64, 64);
    const l = create(bus, { color: [1, 1, 1, 255] });
    const before = (bus.document.layers[0] as PixelLayer).tiles;
    const copy = new Uint8ClampedArray([...before.tiles.values()][0].data);
    const acc = new StrokeAccumulator({ ...DEFAULT_BRUSH, size: 10 }, 64, 64);
    const keys = acc.addPoints([{ x: 10, y: 10, pressure: 1 }]);
    raster.applyStroke(before, acc, keys, { mode: 'paint', color: [255, 0, 0, 255], selection: null });
    expect(sameBytes([...before.tiles.values()][0].data, copy)).toBe(true);
    void l;
  });
});

describe('fills', () => {
  it('fill and clear respect the selection', () => {
    const bus = makeBus(50, 50);
    const l = create(bus);
    bus.dispatch('selection.rect', { x: 10, y: 10, width: 10, height: 10 });
    bus.dispatch('pixels.fill', { layerId: l, color: [0, 128, 255, 255] });
    const t = () => (bus.document.layers[0] as PixelLayer).tiles;
    expect([pixel(t(), 15, 15), pixel(t(), 5, 5)]).toEqual([[0, 128, 255, 255], [0, 0, 0, 0]]);
    bus.dispatch('selection.rect', { x: 10, y: 10, width: 5, height: 10 });
    bus.dispatch('pixels.clear', { layerId: l });
    expect([pixel(t(), 12, 15)[3], pixel(t(), 17, 15)[3]]).toEqual([0, 255]);
  });

  it('paint bucket fills the contiguous region within tolerance', () => {
    const bus = makeBus(60, 60);
    const l = create(bus, { color: [255, 255, 255, 255] });
    bus.dispatch('pixels.fillRect', { layerId: l, x: 30, y: 0, width: 2, height: 60, color: [0, 0, 0, 255] });
    bus.dispatch('pixels.floodFill', { layerId: l, x: 5, y: 5, color: [255, 0, 0, 255], tolerance: 10 });
    const t = (bus.document.layers[0] as PixelLayer).tiles;
    expect([pixel(t, 29, 59), pixel(t, 40, 5), pixel(t, 30, 5)]).toEqual([[255, 0, 0, 255], [255, 255, 255, 255], [0, 0, 0, 255]]);
  });

  it('gradients interpolate between endpoints for every kind', () => {
    const bus = makeBus(101, 20);
    const l = create(bus);
    bus.dispatch('pixels.gradient', { layerId: l, x0: 0, y0: 0, x1: 101, y1: 0, colorA: [0, 0, 0, 255], colorB: [200, 100, 0, 255] });
    const t = (bus.document.layers[0] as PixelLayer).tiles;
    expect(pixel(t, 0, 5)).toEqual([1, 0, 0, 255]);
    expect(pixel(t, 50, 5)).toEqual([100, 50, 0, 255]);
    expect(pixel(t, 100, 5)).toEqual([199, 100, 0, 255]);
    for (const kind of ['radial', 'angle', 'reflected', 'diamond'] as const) {
      expect(raster.gradientT(kind, 10, 0, 0, 0, 20, 0)).toBeGreaterThanOrEqual(0);
    }
    expect(raster.gradientT('reflected', -10, 0, 0, 0, 20, 0)).toBeCloseTo(0.5);
    expect(raster.gradientT('radial', 0, 10, 0, 0, 20, 0)).toBeCloseTo(0.5);
  });

  it('pixel edits are rejected on non-pixel or locked layers with readable errors', () => {
    const bus = makeBus(50, 50);
    const { layerId } = bus.dispatch<{ layerId: string }>('layer.createShape', { shape: { kind: 'rect', x: 0, y: 0, width: 10, height: 10 } });
    expect(() => bus.dispatch('pixels.fill', { layerId, color: [0, 0, 0, 255] })).toThrow(/Rasterize it first/);
    const l = create(bus);
    bus.dispatch('layer.setLocked', { layerId: l, locked: true });
    expect(() => bus.dispatch('paint.stroke', { layerId: l, points: [1, 1, 1] })).toThrow(/locked/);
    void readRegion;
  });
});
