import { describe, expect, it } from 'vitest';
import { CommandError, findLayer, readRegion, type PixelLayer } from '../src';
import { ids, makeBus } from './helpers';

describe('command bus', () => {
  it('creates layers and undoes/redoes them', () => {
    const bus = makeBus();
    const { layerId: a } = bus.dispatch<{ layerId: string }>('layer.create', {});
    const { layerId: b } = bus.dispatch<{ layerId: string }>('layer.create', { name: 'Top' });
    expect(ids(bus)).toEqual([a, b]);
    expect(bus.document.layers.map((l) => l.name)).toEqual(['Layer 1', 'Top']);
    bus.undo();
    expect(ids(bus)).toEqual([a]);
    bus.redo();
    expect(ids(bus)).toEqual([a, b]);
    expect(bus.history.entries.map((e) => e.title)).toEqual(['New Layer', 'New Layer']);
  });

  it('rejects invalid params with readable messages and leaves the document untouched', () => {
    const bus = makeBus();
    const before = bus.document;
    expect(() => bus.dispatch('layer.setOpacity', { layerId: 'x', opacity: 150 })).toThrow(/opacity must be <= 100/);
    expect(() => bus.dispatch('layer.create', { bogus: 1 })).toThrow(/not a known parameter/);
    expect(() => bus.dispatch('nope.nothing', {})).toThrow(CommandError);
    expect(() => bus.dispatch('layer.delete', { layerId: 'missing' })).toThrow(/Layer not found/);
    expect(bus.document).toBe(before);
    expect(bus.history.entries).toHaveLength(0);
  });

  it('places layers relative to others and reorders them', () => {
    const bus = makeBus();
    const a = bus.dispatch<{ layerId: string }>('layer.create', {}).layerId;
    const b = bus.dispatch<{ layerId: string }>('layer.create', {}).layerId;
    const c = bus.dispatch<{ layerId: string }>('layer.create', { below: a }).layerId;
    expect(ids(bus)).toEqual([c, a, b]);
    bus.dispatch('layer.move', { layerId: c, above: b });
    expect(ids(bus)).toEqual([a, b, c]);
    bus.dispatch('layer.move', { layerId: c, index: 0 });
    expect(ids(bus)).toEqual([c, a, b]);
    bus.undo();
    bus.undo();
    expect(ids(bus)).toEqual([c, a, b]);
  });

  it('groups, nests and ungroups layers', () => {
    const bus = makeBus();
    const [a, b, c] = [0, 1, 2].map(() => bus.dispatch<{ layerId: string }>('layer.create', {}).layerId);
    const { groupId } = bus.dispatch<{ groupId: string }>('layer.group', { layerIds: [c, a] });
    expect(ids(bus)).toEqual([b, groupId]);
    const group = bus.document.layers[1];
    expect(group.type === 'group' && group.children.map((l) => l.id)).toEqual([a, c]);
    // Move b into the group, then check a group can't be moved into itself.
    bus.dispatch('layer.move', { layerId: b, parentId: groupId, index: 1 });
    expect(findLayer(bus.document, b)).toMatchObject({ parentId: groupId, index: 1 });
    expect(() => bus.dispatch('layer.move', { layerId: groupId, parentId: groupId })).toThrow(/into itself/);
    bus.dispatch('layer.ungroup', { groupId });
    expect(ids(bus)).toEqual([a, b, c]);
    bus.undo();
    bus.undo();
    bus.undo();
    expect(ids(bus)).toEqual([a, b, c]);
  });

  it('sets layer properties and blocks edits on locked layers', () => {
    const bus = makeBus();
    const a = bus.dispatch<{ layerId: string }>('layer.create', {}).layerId;
    bus.dispatch('layer.setOpacity', { layerId: a, opacity: 40 });
    bus.dispatch('layer.setBlendMode', { layerId: a, blendMode: 'multiply' });
    bus.dispatch('layer.setVisible', { layerId: a, visible: false });
    expect(bus.document.layers[0]).toMatchObject({ opacity: 0.4, blendMode: 'multiply', visible: false });
    expect(() => bus.dispatch('layer.setBlendMode', { layerId: a, blendMode: 'passThrough' })).toThrow(/groups/);
    bus.dispatch('layer.setLocked', { layerId: a, locked: true });
    expect(() => bus.dispatch('layer.delete', { layerId: a })).toThrow(/locked/);
    expect(() =>
      bus.dispatch('pixels.fillRect', { layerId: a, x: 0, y: 0, width: 1, height: 1, color: [0, 0, 0, 255] }),
    ).toThrow(/locked/);
  });

  it('coalesces slider drags into one history entry', () => {
    const bus = makeBus();
    let t = 0;
    const a = bus.dispatch<{ layerId: string }>('layer.create', {}).layerId;
    (bus as unknown as { now: () => number }).now = () => (t += 100);
    for (const v of [90, 80, 70, 60]) bus.dispatch('layer.setOpacity', { layerId: a, opacity: v });
    expect(bus.history.entries.map((e) => e.title)).toEqual(['New Layer', 'Opacity 60%']);
    expect(bus.history.entries[1].patch.ops).toHaveLength(1);
    bus.undo();
    expect(bus.document.layers[0].opacity).toBe(1);
  });

  it('no-op commands do not create history entries', () => {
    const bus = makeBus();
    const a = bus.dispatch<{ layerId: string }>('layer.create', {}).layerId;
    bus.dispatch('layer.setVisible', { layerId: a, visible: true });
    bus.dispatch('layer.move', { layerId: a, index: 0 });
    expect(bus.history.entries).toHaveLength(1);
  });

  it('groups agent runs into a single undoable entry and can cancel them', () => {
    const bus = makeBus();
    const base = bus.dispatch<{ layerId: string }>('layer.create', {}).layerId;
    const actor = { kind: 'agent', name: 'Color Agent', runId: 'r1' } as const;
    bus.beginGroup('Warm the tones', actor);
    const x = bus.dispatch<{ layerId: string }>('layer.create', { name: 'Warm' }).layerId;
    bus.dispatch('layer.setBlendMode', { layerId: x, blendMode: 'softLight' });
    bus.dispatch('layer.setOpacity', { layerId: x, opacity: 30 });
    expect(() => bus.undo()).toThrow(/while a grouped action/);
    const entry = bus.endGroup()!;
    expect(entry.actor).toEqual(actor);
    expect(entry.children!.map((c) => c.title)).toEqual(['New Layer', 'Blend Mode: softLight', 'Opacity 30%']);
    expect(entry.children!.every((c) => c.actor === actor)).toBe(true);
    expect(bus.history.entries).toHaveLength(2);
    bus.undo();
    expect(ids(bus)).toEqual([base]);
    bus.redo();
    expect(bus.document.layers[1]).toMatchObject({ name: 'Warm', blendMode: 'softLight', opacity: 0.3 });

    const before = bus.document;
    bus.beginGroup('Stopped run', actor);
    bus.dispatch('layer.delete', { layerId: base });
    bus.cancelGroup();
    expect(bus.document.layers).toEqual(before.layers);
    expect(bus.history.entries).toHaveLength(2);
  });

  it('jumps through history', () => {
    const bus = makeBus();
    for (let i = 0; i < 5; i++) bus.dispatch('layer.create', {});
    bus.jumpTo(2);
    expect(bus.document.layers).toHaveLength(2);
    bus.jumpTo(5);
    expect(bus.document.layers).toHaveLength(5);
    bus.jumpTo(0);
    bus.dispatch('layer.create', {});
    expect(bus.history.entries).toHaveLength(1);
    expect(bus.history.canRedo).toBe(false);
  });

  it('fills pixels, duplicates share tiles, and places images', () => {
    const bus = makeBus(300, 300);
    const a = bus.dispatch<{ layerId: string }>('layer.create', { color: [255, 0, 0, 255] }).layerId;
    bus.dispatch('pixels.fillRect', { layerId: a, x: 10, y: 10, width: 5, height: 5, color: [0, 0, 255, 128] });
    const layer = bus.document.layers[0] as PixelLayer;
    expect([...readRegion(layer.tiles, { x: 10, y: 10, width: 1, height: 1 })]).toEqual([0, 0, 255, 128]);
    expect([...readRegion(layer.tiles, { x: 9, y: 9, width: 1, height: 1 })]).toEqual([255, 0, 0, 255]);

    const copyId = bus.dispatch<{ layerId: string }>('layer.duplicate', { layerId: a }).layerId;
    const copy = findLayer(bus.document, copyId)!.layer as PixelLayer;
    expect(copy.name).toBe('Layer 1 copy');
    expect(copy.tiles).toBe(layer.tiles);

    const resourceId = bus.resources.add({ width: 2, height: 1, pixels: new Uint8ClampedArray([1, 2, 3, 255, 4, 5, 6, 255]) });
    const placed = bus.dispatch<{ layerId: string }>('layer.placeImage', { resourceId, x: 299, y: 0, name: 'Img' }).layerId;
    const img = findLayer(bus.document, placed)!.layer as PixelLayer;
    expect([...readRegion(img.tiles, { x: 298, y: 0, width: 2, height: 1 })]).toEqual([0, 0, 0, 0, 1, 2, 3, 255]);
  });
});
