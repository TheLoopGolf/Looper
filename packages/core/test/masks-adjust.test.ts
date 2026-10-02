import { describe, expect, it } from 'vitest';
import { ADJUSTMENT_KINDS, ADJUSTMENTS, compileAdjustment, raster, renderDocumentCPU, type PixelLayer } from '../src';
import { create, makeBus, pixel } from './helpers';

const px = (doc: Parameters<typeof renderDocumentCPU>[0], x: number, y: number) => [...renderDocumentCPU(doc, { x, y, width: 1, height: 1 })];

describe('layer masks', () => {
  it('reveal all, hide all, from selection; toggle, invert, density, apply, delete', () => {
    const bus = makeBus(64, 64);
    const l = create(bus, { color: [200, 0, 0, 255] });
    bus.dispatch('layer.addMask', { layerId: l, from: 'hideAll' });
    expect(px(bus.document, 5, 5)[3]).toBe(0);
    bus.dispatch('layer.invertMask', { layerId: l });
    expect(px(bus.document, 5, 5)[3]).toBe(255);
    bus.dispatch('layer.deleteMask', { layerId: l });
    bus.dispatch('selection.rect', { x: 0, y: 0, width: 32, height: 64 });
    bus.dispatch('layer.addMask', { layerId: l });
    expect([px(bus.document, 5, 5)[3], px(bus.document, 40, 5)[3]]).toEqual([255, 0]);
    bus.dispatch('layer.setMaskDensity', { layerId: l, density: 50 });
    expect(px(bus.document, 40, 5)[3]).toBe(128);
    bus.dispatch('layer.setMaskEnabled', { layerId: l, enabled: false });
    expect(px(bus.document, 40, 5)[3]).toBe(255);
    bus.dispatch('layer.setMaskEnabled', { layerId: l, enabled: true });
    bus.dispatch('layer.applyMask', { layerId: l });
    const layer = bus.document.layers[0] as PixelLayer;
    expect(layer.mask).toBeNull();
    expect([pixel(layer.tiles, 5, 5)[3], pixel(layer.tiles, 40, 5)[3]]).toEqual([255, 128]);
    expect(() => bus.dispatch('layer.addMask', { layerId: l, from: 'selection' })).not.toThrow();
    expect(() => bus.dispatch('layer.addMask', { layerId: l })).toThrow(/already has a mask/);
  });
});

describe('adjustments', () => {
  it('every kind compiles with defaults to an identity-ish or documented transform', () => {
    for (const kind of ADJUSTMENT_KINDS) {
      const c = compileAdjustment(kind, {});
      expect(c.lut.length).toBe(1024);
      expect(ADJUSTMENTS[kind as keyof typeof ADJUSTMENTS].label.length).toBeGreaterThan(2);
    }
    const id = (k: string) => raster.adjustPixel(compileAdjustment(k, {}), 100, 150, 200).map((v) => Math.round(v * 255));
    for (const k of ['brightnessContrast', 'levels', 'curves', 'exposure', 'hueSaturation', 'vibrance', 'colorBalance']) expect(id(k)).toEqual([100, 150, 200]);
    expect(id('invert')).toEqual([155, 105, 55]);
    expect(id('threshold')).toEqual([255, 255, 255]);
    const bw = id('blackWhite');
    expect(bw[0]).toBe(bw[1]);
  });

  it('curves are monotone and pass through control points; levels remap the range', () => {
    const t = raster.curveTable([[0, 0], [64, 40], [192, 220], [255, 255]]);
    expect(Math.round(t[64])).toBe(40);
    expect(Math.round(t[192])).toBe(220);
    for (let i = 1; i < 256; i++) expect(t[i]).toBeGreaterThanOrEqual(t[i - 1]);
    const lv = compileAdjustment('levels', { inBlack: 50, inWhite: 150 });
    expect([lv.lut[50 * 4], lv.lut[100 * 4], lv.lut[150 * 4], lv.lut[200 * 4]]).toEqual([0, 128, 255, 255]);
  });

  it('adjustment layers render non-destructively and can be edited; adjust.apply bakes the same result', () => {
    const bus = makeBus(64, 64);
    const l = create(bus, { color: [30, 120, 220, 255] });
    const { layerId: adj } = bus.dispatch<{ layerId: string }>('layer.createAdjustment', { kind: 'invert' });
    expect(px(bus.document, 1, 1)).toEqual([225, 135, 35, 255]);
    bus.dispatch('layer.setOpacity', { layerId: adj, opacity: 0 });
    expect(px(bus.document, 1, 1)).toEqual([30, 120, 220, 255]);
    bus.dispatch('layer.delete', { layerId: adj });
    const { layerId: hs } = bus.dispatch<{ layerId: string }>('layer.createAdjustment', { kind: 'hueSaturation', params: { saturation: -100 } });
    const layered = px(bus.document, 1, 1);
    expect(layered[0]).toBe(layered[1]);
    bus.dispatch('layer.setAdjustment', { layerId: hs, params: { saturation: 0, hue: 180 } });
    const rotated = px(bus.document, 1, 1);
    bus.dispatch('layer.delete', { layerId: hs });
    bus.dispatch('adjust.apply', { layerId: l, kind: 'hueSaturation', params: { hue: 180 } });
    expect(pixel((bus.document.layers[0] as PixelLayer).tiles, 1, 1)).toEqual(rotated);
    expect(() => bus.dispatch('layer.setAdjustment', { layerId: l, params: {} })).toThrow(/not an adjustment layer/);
    expect(() => bus.dispatch('layer.createAdjustment', { kind: 'levels', params: { bogus: 1 } })).toThrow(/Unknown setting/);
  });

  it('adjustment layers created with a selection are masked to it', () => {
    const bus = makeBus(64, 64);
    create(bus, { color: [0, 0, 0, 255] });
    bus.dispatch('selection.rect', { x: 0, y: 0, width: 32, height: 64 });
    bus.dispatch('layer.createAdjustment', { kind: 'invert' });
    expect([px(bus.document, 5, 5)[0], px(bus.document, 50, 5)[0]]).toEqual([255, 0]);
  });
});
