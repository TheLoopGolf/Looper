export type RGBA = readonly [number, number, number, number];

export const toHex = (c: readonly number[]) => `#${c.slice(0, 3).map((v) => Math.round(v).toString(16).padStart(2, '0')).join('')}`;

export function fromHex(hex: string, alpha = 255): [number, number, number, number] {
  const m = hex.match(/^#?([0-9a-f]{6})$/i);
  const n = m ? parseInt(m[1], 16) : 0;
  return [(n >> 16) & 255, (n >> 8) & 255, n & 255, alpha];
}

export const cssColor = (c: readonly number[]) => `rgba(${c[0]},${c[1]},${c[2]},${(c[3] ?? 255) / 255})`;
