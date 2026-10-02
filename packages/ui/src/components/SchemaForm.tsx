import type { JSONSchema } from '@canvas-ai/core';
import { useId } from 'react';
import { fromHex, toHex } from '../lib/color';
import { CurveEditor } from './CurveEditor';

type Values = Record<string, unknown>;
type Field = Extract<JSONSchema, { type: unknown }>;

const isColor = (s: Field) =>
  s.type === 'array' && 'items' in s && (s.items as Field).type === 'integer' && (s.minItems === 3 || s.minItems === 4) && (s.items as { maximum?: number }).maximum === 255;
const isTriple = (s: Field) => s.type === 'array' && 'items' in s && (s.items as Field).type === 'number' && s.minItems === 3 && s.maxItems === 3;
const isCurve = (s: Field) => s.type === 'array' && 'items' in s && (s.items as Field).type === 'array';

const TRIPLE_LABELS = ['Cyan ↔ Red', 'Magenta ↔ Green', 'Yellow ↔ Blue'];

function label(key: string, s: JSONSchema) {
  return s.title ?? key.replace(/([A-Z])/g, ' $1').replace(/^./, (c) => c.toUpperCase());
}

function NumberField({ id, title, value, min, max, integer, onChange }: { id: string; title: string; value: number; min: number; max: number; integer: boolean; onChange: (v: number) => void }) {
  const span = max - min;
  const step = integer ? 1 : span > 50 ? 1 : span > 5 ? 0.1 : 0.01;
  return (
    <div className="sf-row">
      <label htmlFor={id}>{title}</label>
      <input type="range" min={min} max={max} step={step} value={value} aria-label={`${title} slider`} onChange={(e) => onChange(Number(e.target.value))} />
      <input
        id={id}
        type="number"
        min={min}
        max={max}
        step={integer ? 1 : 'any'}
        value={integer ? Math.round(value) : +value.toFixed(3)}
        onChange={(e) => {
          const v = Number(e.target.value);
          if (Number.isFinite(v)) onChange(Math.min(max, Math.max(min, v)));
        }}
      />
    </div>
  );
}

/** Form generated from command/adjustment JSON schemas (titles, ranges, enums, colors, curves). */
export function SchemaForm({ fields, values, onChange }: { fields: Readonly<Record<string, JSONSchema>>; values: Values; onChange: (patch: Values) => void }) {
  const base = useId();
  return (
    <div className="schema-form">
      {Object.entries(fields).map(([key, schema]) => {
        if (!('type' in schema)) return null;
        const s = schema as Field;
        const id = `${base}-${key}`;
        const title = label(key, s);
        const v = values[key] ?? ('default' in s ? s.default : undefined);
        if (s.type === 'number' || s.type === 'integer') {
          return (
            <NumberField key={key} id={id} title={title} value={Number(v ?? s.minimum ?? 0)} min={s.minimum ?? -1000} max={s.maximum ?? 1000} integer={s.type === 'integer'} onChange={(x) => onChange({ [key]: x })} />
          );
        }
        if (s.type === 'boolean') {
          return (
            <label key={key} className="sf-check">
              <input type="checkbox" checked={!!v} onChange={(e) => onChange({ [key]: e.target.checked })} /> {title}
            </label>
          );
        }
        if (s.type === 'string' && s.enum) {
          return (
            <div key={key} className="sf-row">
              <label htmlFor={id}>{title}</label>
              <select id={id} value={String(v ?? s.enum[0])} onChange={(e) => onChange({ [key]: e.target.value })}>
                {s.enum.map((o) => (
                  <option key={o} value={o}>
                    {o}
                  </option>
                ))}
              </select>
            </div>
          );
        }
        if (isColor(s)) {
          const c = (v as number[] | undefined) ?? [0, 0, 0, 255];
          return (
            <div key={key} className="sf-row">
              <label htmlFor={id}>{title}</label>
              <input id={id} type="color" value={toHex(c)} onChange={(e) => onChange({ [key]: (s as { minItems?: number }).minItems === 3 ? fromHex(e.target.value).slice(0, 3) : fromHex(e.target.value, c[3] ?? 255) })} />
            </div>
          );
        }
        if (isTriple(s)) {
          const t = (v as number[] | undefined) ?? [0, 0, 0];
          return (
            <fieldset key={key} className="sf-group">
              <legend>{title}</legend>
              {t.map((x, i) => (
                <NumberField key={i} id={`${id}-${i}`} title={TRIPLE_LABELS[i]} value={x} min={-100} max={100} integer onChange={(nv) => onChange({ [key]: t.map((o, j) => (j === i ? nv : o)) })} />
              ))}
            </fieldset>
          );
        }
        if (isCurve(s)) {
          const pts = (v as [number, number][] | undefined) ?? (key === 'rgb' ? [[0, 0], [255, 255]] : undefined);
          return (
            <fieldset key={key} className="sf-group">
              <legend>{title}</legend>
              {pts ? (
                <CurveEditor points={pts} onChange={(p) => onChange({ [key]: p })} />
              ) : (
                <button type="button" className="btn small" onClick={() => onChange({ [key]: [[0, 0], [255, 255]] })}>
                  Add {title.toLowerCase()}
                </button>
              )}
            </fieldset>
          );
        }
        return null;
      })}
    </div>
  );
}
