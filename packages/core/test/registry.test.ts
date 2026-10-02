import { describe, expect, it } from 'vitest';
import { createDefaultRegistry, matchesCommandPattern, validate } from '../src';

describe('registry', () => {
  it('generates agent tools from command metadata', () => {
    const tools = createDefaultRegistry().toAgentTools();
    const create = tools.find((t) => t.commandId === 'layer.create')!;
    expect(create.name).toBe('layer_create');
    expect(create.input_schema).toMatchObject({ type: 'object' });
    const del = tools.find((t) => t.commandId === 'layer.delete')!;
    expect(del.destructive).toBe(true);
    expect(del.description).toMatch(/approval/);
    for (const t of tools) {
      expect(t.name).toMatch(/^[a-zA-Z0-9_-]{1,64}$/);
      expect(t.description.length).toBeGreaterThan(20);
    }
  });

  it('filters by allowed-command patterns', () => {
    expect(matchesCommandPattern('layer.create', 'layer.*')).toBe(true);
    expect(matchesCommandPattern('layerx.create', 'layer.*')).toBe(false);
    expect(matchesCommandPattern('pixels.fillRect', 'pixels.fillRect')).toBe(true);
    const ids = createDefaultRegistry().list(['pixels.*']).map((c) => c.id);
    expect(ids).toContain('pixels.fillRect');
    expect(ids.every((id) => id.startsWith('pixels.'))).toBe(true);
  });

  it('validates nested schemas', () => {
    const schema = {
      type: 'object',
      required: ['c'],
      properties: { c: { type: 'array', minItems: 2, maxItems: 2, items: { type: 'integer', minimum: 0 } }, p: { anyOf: [{ type: 'string' }, { type: 'null' }] } },
    } as const;
    expect(validate(schema, { c: [1, 2], p: null })).toEqual([]);
    expect(validate(schema, { c: [1, -1] })).toEqual(['params.c[1] must be >= 0']);
    expect(validate(schema, { c: [1], p: 3 })).toHaveLength(2);
  });
});
