/**
 * The subset of JSON Schema used for command params. The same schema drives
 * the UI (property editors), plugin validation, and the AI agent's tool list,
 * so it is kept deliberately small and fully validated here.
 */
/** `title` labels the field in generated UI (dialogs, property panels); `description` documents it for agents. */
interface Meta {
  title?: string;
  description?: string;
}

export type JSONSchema =
  | ({ type: 'object'; properties: Readonly<Record<string, JSONSchema>>; required?: readonly string[]; additionalProperties?: false } & Meta)
  | ({ type: 'string'; enum?: readonly string[]; minLength?: number; maxLength?: number; default?: string } & Meta)
  | ({ type: 'number' | 'integer'; minimum?: number; maximum?: number; default?: number } & Meta)
  | ({ type: 'boolean'; default?: boolean } & Meta)
  | ({ type: 'array'; items: JSONSchema; minItems?: number; maxItems?: number; default?: unknown } & Meta)
  | ({ type: 'null' } & Meta)
  | ({ anyOf: readonly JSONSchema[] } & Meta);

/** Returns a list of human-readable problems; empty means valid. */
export function validate(schema: JSONSchema, value: unknown, path = 'params'): string[] {
  if ('anyOf' in schema) {
    const results = schema.anyOf.map((s) => validate(s, value, path));
    return results.some((r) => r.length === 0) ? [] : [`${path} does not match any allowed shape`];
  }
  switch (schema.type) {
    case 'object': {
      if (typeof value !== 'object' || value === null || Array.isArray(value)) return [`${path} must be an object`];
      const errors: string[] = [];
      const obj = value as Record<string, unknown>;
      for (const key of schema.required ?? []) if (obj[key] === undefined) errors.push(`${path}.${key} is required`);
      for (const [key, v] of Object.entries(obj)) {
        const prop = schema.properties[key];
        if (!prop) {
          if (schema.additionalProperties === false) errors.push(`${path}.${key} is not a known parameter`);
          continue;
        }
        if (v !== undefined) errors.push(...validate(prop, v, `${path}.${key}`));
      }
      return errors;
    }
    case 'string':
      if (typeof value !== 'string') return [`${path} must be a string`];
      if (schema.enum && !schema.enum.includes(value)) return [`${path} must be one of: ${schema.enum.join(', ')}`];
      if (schema.minLength !== undefined && value.length < schema.minLength) return [`${path} is too short`];
      if (schema.maxLength !== undefined && value.length > schema.maxLength) return [`${path} is too long`];
      return [];
    case 'number':
    case 'integer':
      if (typeof value !== 'number' || !Number.isFinite(value)) return [`${path} must be a finite number`];
      if (schema.type === 'integer' && !Number.isInteger(value)) return [`${path} must be an integer`];
      if (schema.minimum !== undefined && value < schema.minimum) return [`${path} must be >= ${schema.minimum}`];
      if (schema.maximum !== undefined && value > schema.maximum) return [`${path} must be <= ${schema.maximum}`];
      return [];
    case 'boolean':
      return typeof value === 'boolean' ? [] : [`${path} must be a boolean`];
    case 'null':
      return value === null ? [] : [`${path} must be null`];
    case 'array': {
      if (!Array.isArray(value)) return [`${path} must be an array`];
      if (schema.minItems !== undefined && value.length < schema.minItems) return [`${path} needs at least ${schema.minItems} items`];
      if (schema.maxItems !== undefined && value.length > schema.maxItems) return [`${path} allows at most ${schema.maxItems} items`];
      return value.flatMap((v, i) => validate(schema.items, v, `${path}[${i}]`));
    }
  }
}
