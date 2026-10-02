import { describe, expect, it } from 'vitest';
import { FILTER_KINDS, renderDocumentCPU, setTextRasterizer, type PixelLayer } from '../src';
import { raster } from '../src';
import { checkGolden, create, makeBus, pixel } from './helpers';

function testImage() {
  const bus = makeBus(128, 96);
  const l = create(bus, { color: [235, 235, 230, 255] });
  bus.dispatch('pixels.fillRect', { layerId: l, x: 20, y: 20, width: 50, height: 40, color: [200, 40, 40, 255] });
  bus.dispatch('pixels.fillRect', { layerId: l, x: 60, y: 45, width: 40, height: 35, color: [30, 90, 200, 255] });
  bus.dispatch('pixels.gradient', { layerId: l, x0: 0, y0: 0, x1: 128, y1: 0, colorA: [0, 0, 0, 0], colorB: [20, 160, 60, 160] });
  return { bus, l };
}

const FILTER_PARAMS: Record<string, Record<string, unknown>> = {
  gaussianBlur: { radius: 6 },
  motionBlur: { angle: 30, distance: 15 },
  unsharpMask: { amount: 150, radius: 3, threshold: 2 },
  highPass: { radius: 5 },
  addNoise: { amount: 20, distribution: 'gaussian', monochromatic: false, seed: 7 },
  pixelate: { cellSize: 8 },
  emboss: { angle: 135, height: 2, amount: 120 },
};

describe('filters', () => {
  it.each(FILTER_KINDS)('%s matches its golden', (kind) => {
    const { bus, l } = testImage();
    bus.dispatch(`filter.${kind}`, { layerId: l, ...FILTER_PARAMS[kind] });
    checkGolden(`filter-${kind}`, renderDocumentCPU(bus.document), 128, 96);
  });

  it('respects the selection and is undoable', () => {
    const { bus, l } = testImage();
    const before = renderDocumentCPU(bus.document);
    bus.dispatch('selection.rect', { x: 0, y: 0, width: 64, height: 96 });
    bus.dispatch('filter.gaussianBlur', { layerId: l, radius: 10 });
    const after = renderDocumentCPU(bus.document);
    const at = (buf: Uint8ClampedArray, x: number, y: number) => [...buf.subarray((y * 128 + x) * 4, (y * 128 + x) * 4 + 4)];
    expect(at(after, 100, 50)).toEqual(at(before, 100, 50));
    expect(at(after, 20, 20)).not.toEqual(at(before, 20, 20));
    bus.undo();
    expect(renderDocumentCPU(bus.document)).toEqual(before);
  });

  it('noise is seeded and independent of the processed region', () => {
    const a = testImage();
    a.bus.dispatch('filter.addNoise', { layerId: a.l, amount: 30, seed: 3 });
    const b = testImage();
    b.bus.dispatch('selection.rect', { x: 10, y: 10, width: 30, height: 30 });
    b.bus.dispatch('filter.addNoise', { layerId: b.l, amount: 30, seed: 3 });
    const ta = (a.bus.document.layers[0] as PixelLayer).tiles, tb = (b.bus.document.layers[0] as PixelLayer).tiles;
    expect(pixel(ta, 25, 25)).toEqual(pixel(tb, 25, 25));
  });
});

describe('shape and text layers', () => {
  it('rasterizes shapes deterministically (golden) and stays editable', () => {
    const bus = makeBus(200, 120);
    create(bus, { color: [255, 255, 255, 255] });
    bus.dispatch('layer.createShape', { shape: { kind: 'rect', x: 10, y: 10, width: 60, height: 40, radius: 10 }, fillColor: [230, 120, 30, 255] });
    bus.dispatch('layer.createShape', { shape: { kind: 'ellipse', cx: 120, cy: 40, rx: 35, ry: 25 }, fillColor: null, strokeColor: [40, 40, 160, 255], strokeWidth: 6 });
    bus.dispatch('layer.createShape', { shape: { kind: 'polygon', cx: 50, cy: 90, radius: 25, sides: 5, star: 0.5 }, fillColor: [250, 200, 0, 255], strokeColor: [0, 0, 0, 255], strokeWidth: 2 });
    bus.dispatch('layer.createShape', { shape: { kind: 'line', x1: 100, y1: 80, x2: 190, y2: 110 }, strokeColor: [200, 0, 80, 200], strokeWidth: 8 });
    const { layerId } = bus.dispatch<{ layerId: string }>('layer.createShape', { shape: { kind: 'path', points: [150, 70, 190, 70, 170, 100], closed: true }, fillColor: [0, 160, 120, 180] });
    checkGolden('shapes', renderDocumentCPU(bus.document), 200, 120);
    bus.dispatch('layer.updateShape', { layerId, fillColor: [0, 0, 0, 255] });
    expect(renderDocumentCPU(bus.document, { x: 170, y: 80, width: 1, height: 1 })[0]).toBe(0);
    expect(() => bus.dispatch('layer.createShape', { shape: { kind: 'rect', x: 0 } })).toThrow(/missing y, width, height/);
  });

  it('text layers render through the platform rasterizer and rasterize to pixels', () => {
    const bus = makeBus(100, 50);
    expect(() => bus.dispatch('layer.createText', { text: 'Hi', x: 10, y: 10 })).not.toThrow();
    // Without a platform rasterizer text renders nothing (Node), and can't be rasterized.
    expect(renderDocumentCPU(bus.document).every((v) => v === 0)).toBe(true);
    const textId = bus.document.layers[0].id;
    expect(() => bus.dispatch('layer.rasterize', { layerId: textId })).toThrow(/not available/);
    // A fake rasterizer: fills the text box with the text color.
    setTextRasterizer((layer) => {
      const w = layer.text.length * 10, h = layer.style.fontSize;
      const data = new Uint8ClampedArray(w * h * 4);
      for (let i = 0; i < data.length; i += 4) data.set(layer.style.color, i);
      return { x: Math.round(layer.x), y: Math.round(layer.y), width: w, height: h, channels: 4, data };
    });
    try {
      bus.dispatch('layer.updateText', { layerId: textId, style: { fontSize: 20, color: [255, 0, 0, 255] } });
      expect([...renderDocumentCPU(bus.document, { x: 12, y: 12, width: 1, height: 1 })]).toEqual([255, 0, 0, 255]);
      bus.dispatch('layer.rasterize', { layerId: textId });
      expect(bus.document.layers[0].type).toBe('pixel');
      expect(pixel((bus.document.layers[0] as PixelLayer).tiles, 12, 12)).toEqual([255, 0, 0, 255]);
    } finally {
      setTextRasterizer(null);
    }
  });
});

describe('transforms and canvas', () => {
  it('translate is exact; flip twice is identity; content outside the canvas is clipped', () => {
    const bus = makeBus(64, 64);
    const l = create(bus);
    bus.dispatch('pixels.fillRect', { layerId: l, x: 10, y: 10, width: 5, height: 5, color: [9, 8, 7, 255] });
    bus.dispatch('layer.translate', { layerId: l, dx: 20, dy: -5 });
    const t = () => (bus.document.layers[0] as PixelLayer).tiles;
    expect([pixel(t(), 30, 5), pixel(t(), 10, 10)[3]]).toEqual([[9, 8, 7, 255], 0]);
    const before = renderDocumentCPU(bus.document);
    bus.dispatch('layer.flip', { layerId: l, axis: 'horizontal' });
    expect(pixel(t(), 64 - 31, 5)).toEqual([9, 8, 7, 255]);
    bus.dispatch('layer.flip', { layerId: l, axis: 'horizontal' });
    expect(renderDocumentCPU(bus.document)).toEqual(before);
    bus.dispatch('layer.translate', { layerId: l, dx: 100, dy: 0 });
    expect(t().tiles.size).toBe(0);
  });

  it('free transform scales pixels (bilinear) and keeps shapes vector', () => {
    const bus = makeBus(100, 100);
    const l = create(bus);
    bus.dispatch('pixels.fillRect', { layerId: l, x: 10, y: 10, width: 10, height: 10, color: [255, 0, 0, 255] });
    bus.dispatch('layer.transform', { layerId: l, matrix: [2, 0, 0, 2, 0, 0] });
    const t = (bus.document.layers[0] as PixelLayer).tiles;
    expect([pixel(t, 30, 30), pixel(t, 21, 21)]).toEqual([[255, 0, 0, 255], [255, 0, 0, 255]]);
    expect(pixel(t, 41, 30)[3]).toBe(0);
    const { layerId } = bus.dispatch<{ layerId: string }>('layer.createShape', { shape: { kind: 'rect', x: 0, y: 0, width: 10, height: 10 } });
    bus.dispatch('layer.transform', { layerId, matrix: [3, 0, 0, 3, 5, 5] });
    expect((bus.document.layers[1] as unknown as { shape: unknown }).shape).toMatchObject({ kind: 'rect', x: 5, y: 5, width: 30, height: 30 });
    expect(() => bus.dispatch('layer.transform', { layerId, matrix: [0, 0, 0, 0, 0, 0] })).toThrow(/not invertible/);
  });

  it('crop, straighten, crop to selection, canvas size and image size', () => {
    const bus = makeBus(200, 100);
    const l = create(bus, { color: [255, 255, 255, 255] });
    bus.dispatch('pixels.fillRect', { layerId: l, x: 50, y: 20, width: 10, height: 10, color: [0, 0, 255, 255] });
    bus.dispatch('selection.rect', { x: 0, y: 0, width: 5, height: 5 });
    bus.dispatch('document.crop', { x: 40, y: 10, width: 50, height: 40 });
    expect([bus.document.width, bus.document.height, bus.document.selection]).toEqual([50, 40, null]);
    const t = () => (bus.document.layers[0] as PixelLayer).tiles;
    expect([pixel(t(), 10, 10), pixel(t(), 9, 9)]).toEqual([[0, 0, 255, 255], [255, 255, 255, 255]]);
    bus.undo();
    expect(bus.document.width).toBe(200);
    bus.dispatch('document.crop', { x: 50, y: 0, width: 100, height: 100, angle: 10 });
    expect(bus.document.width).toBe(100);
    expect(renderDocumentCPU(bus.document, { x: 50, y: 50, width: 1, height: 1 })[3]).toBe(255);
    bus.undo();
    bus.dispatch('selection.ellipse', { x: 50, y: 20, width: 30, height: 20 });
    bus.dispatch('document.cropToSelection', {});
    expect([bus.document.width, bus.document.height]).toEqual([30, 20]);
    bus.undo();
    bus.dispatch('document.resizeCanvas', { width: 220, height: 100, anchor: 'r' });
    expect(pixel(t(), 75, 25)).toEqual([0, 0, 255, 255]);
    expect(pixel(t(), 5, 5)[3]).toBe(0);
    bus.dispatch('document.resizeImage', { width: 110, height: 50 });
    expect([bus.document.width, bus.document.height]).toEqual([110, 50]);
    void raster;
  });
});

describe('merging', () => {
  it('merge down and flatten equal the composite they replace', () => {
    const bus = makeBus(80, 60);
    create(bus, { color: [240, 230, 200, 255] });
    const mid = create(bus);
    bus.dispatch('pixels.fillRect', { layerId: mid, x: 10, y: 10, width: 40, height: 30, color: [20, 120, 220, 200] });
    const top = create(bus);
    bus.dispatch('pixels.fillRect', { layerId: top, x: 30, y: 20, width: 40, height: 30, color: [250, 60, 40, 255] });
    bus.dispatch('layer.setBlendMode', { layerId: top, blendMode: 'multiply' });
    bus.dispatch('layer.setOpacity', { layerId: top, opacity: 70 });
    bus.dispatch('layer.createAdjustment', { kind: 'posterize', params: { levels: 6 } });
    const before = renderDocumentCPU(bus.document);
    bus.dispatch('layer.mergeDown', { layerId: top });
    expect(bus.document.layers).toHaveLength(3);
    expect(renderDocumentCPU(bus.document)).toEqual(before);
    bus.dispatch('layer.flatten', {});
    expect(bus.document.layers.map((l) => l.name)).toEqual(['Background']);
    expect(renderDocumentCPU(bus.document)).toEqual(before);
    expect(() => bus.dispatch('layer.mergeDown', { layerId: bus.document.layers[0].id })).toThrow(/no layer below/);
  });
});
