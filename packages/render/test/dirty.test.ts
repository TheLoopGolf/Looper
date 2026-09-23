import { describe, expect, it } from 'vitest';
import { CommandBus, createDefaultRegistry, createDocument, tileKey } from '@canvas-ai/core';
import { diffDocuments } from '../src';

function setup() {
  const bus = new CommandBus(createDefaultRegistry(), createDocument({ id: 'd', width: 1024, height: 1024 }), { idPrefix: 't' });
  const a = bus.dispatch<{ layerId: string }>('layer.create', {}).layerId;
  bus.dispatch('pixels.fillRect', { layerId: a, x: 0, y: 0, width: 300, height: 10, color: [255, 0, 0, 255] }); // tiles (0,0),(1,0)
  const b = bus.dispatch<{ layerId: string }>('layer.create', {}).layerId;
  bus.dispatch('pixels.fillRect', { layerId: b, x: 600, y: 600, width: 10, height: 10, color: [0, 255, 0, 255] }); // tile (2,2)
  return { bus, a, b };
}

const keys = (d: { tiles: ReadonlySet<number> }) => [...d.tiles].sort((x, y) => x - y);

describe('dirty tile tracking', () => {
  it('first render and resize dirty everything', () => {
    const { bus } = setup();
    expect(diffDocuments(null, bus.document).all).toBe(true);
    expect(diffDocuments({ ...bus.document, width: 5 }, bus.document).all).toBe(true);
  });

  it('pixel edits dirty only touched tiles', () => {
    const { bus, a } = setup();
    const prev = bus.document;
    bus.dispatch('pixels.fillRect', { layerId: a, x: 260, y: 0, width: 5, height: 5, color: [1, 1, 1, 255] });
    expect(keys(diffDocuments(prev, bus.document))).toEqual([tileKey(1, 0)]);
  });

  it('property changes dirty the layer content area', () => {
    const { bus, a, b } = setup();
    let prev = bus.document;
    bus.dispatch('layer.setOpacity', { layerId: a, opacity: 50 });
    expect(keys(diffDocuments(prev, bus.document))).toEqual([tileKey(0, 0), tileKey(1, 0)]);
    prev = bus.document;
    bus.dispatch('layer.rename', { layerId: b, name: 'renamed' });
    expect(keys(diffDocuments(prev, bus.document))).toEqual([]);
  });

  it('reorder, grouping and deletion dirty the affected content', () => {
    const { bus, a, b } = setup();
    let prev = bus.document;
    bus.dispatch('layer.move', { layerId: a, above: b });
    expect(keys(diffDocuments(prev, bus.document))).toEqual([tileKey(0, 0), tileKey(1, 0), tileKey(2, 2)]);
    prev = bus.document;
    const { groupId } = bus.dispatch<{ groupId: string }>('layer.group', { layerIds: [b] });
    expect(keys(diffDocuments(prev, bus.document))).toEqual([tileKey(2, 2)]);
    prev = bus.document;
    bus.dispatch('layer.setBlendMode', { layerId: groupId, blendMode: 'multiply' });
    expect(keys(diffDocuments(prev, bus.document))).toEqual([tileKey(2, 2)]);
    prev = bus.document;
    bus.dispatch('layer.delete', { layerId: a });
    expect(keys(diffDocuments(prev, bus.document))).toEqual([tileKey(0, 0), tileKey(1, 0)]);
    prev = bus.document;
    bus.undo();
    expect(keys(diffDocuments(prev, bus.document))).toEqual([tileKey(0, 0), tileKey(1, 0)]);
  });
});
