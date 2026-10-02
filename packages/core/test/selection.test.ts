import { describe, expect, it } from 'vitest';
import { create, makeBus, sel } from './helpers';

describe('selections', () => {
  it('rect marquee covers exact pixels and anti-aliases fractional edges', () => {
    const bus = makeBus(100, 100);
    bus.dispatch('selection.rect', { x: 10, y: 10, width: 20, height: 5 });
    const d = bus.document;
    expect([sel(d, 10, 10), sel(d, 29, 14), sel(d, 30, 10), sel(d, 9, 10), sel(d, 10, 15)]).toEqual([255, 255, 0, 0, 0]);
    bus.dispatch('selection.rect', { x: 10.5, y: 10, width: 10, height: 10 });
    expect(sel(bus.document, 10, 12)).toBe(128);
    bus.dispatch('selection.rect', { x: 10.5, y: 10, width: 10, height: 10, antiAlias: false });
    expect(sel(bus.document, 10, 12)).toBe(255);
  });

  it('combines with add, subtract and intersect', () => {
    const bus = makeBus(100, 100);
    bus.dispatch('selection.rect', { x: 0, y: 0, width: 50, height: 50 });
    bus.dispatch('selection.rect', { x: 40, y: 40, width: 20, height: 20, mode: 'add' });
    expect(sel(bus.document, 55, 55)).toBe(255);
    bus.dispatch('selection.rect', { x: 0, y: 0, width: 10, height: 10, mode: 'subtract' });
    expect(sel(bus.document, 5, 5)).toBe(0);
    expect(sel(bus.document, 20, 20)).toBe(255);
    bus.dispatch('selection.rect', { x: 45, y: 45, width: 100, height: 100, mode: 'intersect' });
    expect([sel(bus.document, 20, 20), sel(bus.document, 50, 50)]).toEqual([0, 255]);
  });

  it('ellipse area matches π·a·b and lasso selects a triangle', () => {
    const bus = makeBus(200, 200);
    bus.dispatch('selection.ellipse', { x: 20, y: 20, width: 100, height: 60 });
    let sum = 0;
    for (const t of bus.document.selection!.mask.tiles.values()) for (const v of t.data) sum += v / 255;
    expect(Math.abs(sum - Math.PI * 50 * 30) / (Math.PI * 50 * 30)).toBeLessThan(0.005);
    bus.dispatch('selection.polygon', { points: [0, 0, 100, 0, 0, 100] });
    expect([sel(bus.document, 10, 10), sel(bus.document, 90, 90)]).toEqual([255, 0]);
  });

  it('inverse, select all, deselect and undo', () => {
    const bus = makeBus(300, 300);
    bus.dispatch('selection.rect', { x: 0, y: 0, width: 10, height: 10 });
    bus.dispatch('selection.invert', {});
    expect([sel(bus.document, 5, 5), sel(bus.document, 299, 299)]).toEqual([0, 255]);
    bus.dispatch('selection.deselect', {});
    expect(bus.document.selection).toBeNull();
    bus.dispatch('selection.all', {});
    expect(sel(bus.document, 150, 150)).toBe(255);
    bus.undo();
    bus.undo();
    expect(sel(bus.document, 5, 5)).toBe(0);
  });

  it('feather softens, expand/contract grow and shrink by whole pixels', () => {
    const bus = makeBus(200, 200);
    bus.dispatch('selection.rect', { x: 50, y: 50, width: 100, height: 100 });
    bus.dispatch('selection.expand', { pixels: 5 });
    expect([sel(bus.document, 45, 100), sel(bus.document, 44, 100)]).toEqual([255, 0]);
    expect(sel(bus.document, 46, 46)).toBe(0); // rounded corner
    bus.dispatch('selection.contract', { pixels: 10 });
    expect([sel(bus.document, 55, 100), sel(bus.document, 54, 100)]).toEqual([255, 0]);
    bus.dispatch('selection.feather', { radius: 8 });
    const edge = sel(bus.document, 55, 100);
    expect(edge).toBeGreaterThan(60);
    expect(edge).toBeLessThan(200);
    expect(sel(bus.document, 100, 100)).toBe(255);
  });

  it('magic wand (contiguous and global) and color range', () => {
    const bus = makeBus(100, 100);
    const a = create(bus, { color: [255, 255, 255, 255] });
    bus.dispatch('pixels.fillRect', { layerId: a, x: 0, y: 0, width: 20, height: 20, color: [255, 0, 0, 255] });
    bus.dispatch('pixels.fillRect', { layerId: a, x: 60, y: 60, width: 20, height: 20, color: [250, 5, 0, 255] });
    bus.dispatch('selection.magicWand', { x: 5, y: 5, layerId: a, tolerance: 10 });
    expect([sel(bus.document, 10, 10), sel(bus.document, 70, 70), sel(bus.document, 50, 50)]).toEqual([255, 0, 0]);
    bus.dispatch('selection.magicWand', { x: 5, y: 5, layerId: a, tolerance: 10, contiguous: false });
    expect(sel(bus.document, 70, 70)).toBe(255);
    bus.dispatch('selection.colorRange', { color: [255, 0, 0, 255], fuzziness: 40 });
    expect([sel(bus.document, 10, 10), sel(bus.document, 70, 70), sel(bus.document, 50, 50)]).toEqual([255, 255, 0]);
  });

  it('saves, loads and deletes channels; loads layer transparency', () => {
    const bus = makeBus(100, 100);
    bus.dispatch('selection.rect', { x: 0, y: 0, width: 10, height: 10 });
    const { channelId } = bus.dispatch<{ channelId: string }>('selection.save', { name: 'Corner' });
    bus.dispatch('selection.deselect', {});
    bus.dispatch('selection.load', { channelId, invert: true });
    expect([sel(bus.document, 5, 5), sel(bus.document, 50, 50)]).toEqual([0, 255]);
    bus.dispatch('channel.delete', { channelId });
    expect(bus.document.channels).toEqual([]);
    const l = create(bus);
    bus.dispatch('pixels.fillRect', { layerId: l, x: 30, y: 30, width: 5, height: 5, color: [1, 2, 3, 200] });
    bus.dispatch('selection.fromLayer', { layerId: l });
    expect([sel(bus.document, 31, 31), sel(bus.document, 29, 29)]).toEqual([200, 0]);
  });
});
