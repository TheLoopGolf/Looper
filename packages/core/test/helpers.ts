import { CommandBus, createDefaultRegistry, createDocument } from '../src';

export function makeBus(width = 512, height = 512) {
  let t = 0;
  const bus = new CommandBus(createDefaultRegistry(), createDocument({ id: 'doc', width, height }), {
    idPrefix: 't',
    now: () => (t += 10_000),
  });
  return bus;
}

export const ids = (bus: CommandBus) => bus.document.layers.map((l) => l.id);

/** Fast typed-array equality (vitest's deep equality is very slow on large buffers). */
export function sameBytes(a: ArrayLike<number> & ArrayBufferView, b: ArrayLike<number> & ArrayBufferView): boolean {
  return Buffer.from(a.buffer, a.byteOffset, a.byteLength).equals(Buffer.from(b.buffer, b.byteOffset, b.byteLength));
}
