import { describe, expect, it } from 'vitest';
import { makeBus } from './helpers';

const gc = (globalThis as { gc?: () => void }).gc;

function heapMB(): number {
  gc!();
  gc!();
  return process.memoryUsage().heapUsed / 1e6 + process.memoryUsage().arrayBuffers / 1e6;
}

describe('history memory', () => {
  it.runIf(gc)('100-step undo/redo cycles do not leak pixel data', () => {
    const bus = makeBus(2048, 2048); // 64 tiles, 16 MB per full layer
    const layerId = bus.dispatch<{ layerId: string }>('layer.create', { color: [1, 2, 3, 255] }).layerId;

    const cycle = (seed: number) => {
      for (let i = 0; i < 100; i++) {
        bus.dispatch('pixels.fillRect', {
          layerId, x: ((i * 97 + seed) % 1800), y: ((i * 53) % 1800), width: 200, height: 200, color: [i, seed & 255, 0, 255],
        });
      }
      for (let i = 0; i < 100; i++) bus.undo();
      for (let i = 0; i < 100; i++) bus.redo();
      // Branching discards the redo stack; its tiles must become collectable.
      for (let i = 0; i < 100; i++) bus.undo();
      bus.dispatch('layer.setOpacity', { layerId, opacity: seed % 100 });
    };

    cycle(1);
    const baseline = heapMB();
    for (let s = 2; s < 8; s++) cycle(s);
    const after = heapMB();
    expect(bus.history.entries.length).toBeLessThanOrEqual(8); // create + one opacity entry per cycle
    // Each cycle allocates ~100 tiles × 256 KB ≈ 100 MB; a leak would grow by that per cycle.
    expect(after - baseline).toBeLessThan(30);
  });

  it('caps history length', () => {
    const bus = makeBus(64, 64);
    for (let i = 0; i < 250; i++) bus.dispatch('layer.create', {});
    expect(bus.history.entries).toHaveLength(200);
    for (let i = 0; i < 200; i++) bus.undo();
    expect(bus.document.layers).toHaveLength(50);
  });
});
