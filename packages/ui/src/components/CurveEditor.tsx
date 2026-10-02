import { raster } from '@canvas-ai/core';
import { useRef, useState } from 'react';

const SIZE = 200;

/** Tone curve editor: click to add a point, drag to move, double-click to remove (endpoints stay). */
export function CurveEditor({ points, onChange }: { points: [number, number][]; onChange: (p: [number, number][]) => void }) {
  const ref = useRef<SVGSVGElement>(null);
  const [drag, setDrag] = useState<number | null>(null);
  const sorted = [...points].sort((a, b) => a[0] - b[0]);
  const table = raster.curveTable(sorted);
  const toSvg = (x: number, y: number) => [(x / 255) * SIZE, SIZE - (y / 255) * SIZE];
  const fromEvent = (e: React.PointerEvent): [number, number] => {
    const r = ref.current!.getBoundingClientRect();
    const x = Math.round(((e.clientX - r.left) / r.width) * 255);
    const y = Math.round((1 - (e.clientY - r.top) / r.height) * 255);
    return [Math.min(255, Math.max(0, x)), Math.min(255, Math.max(0, y))];
  };
  const path = table.map((y, x) => `${x ? 'L' : 'M'}${toSvg(x, y).join(',')}`).join('');
  return (
    <svg
      ref={ref}
      className="curve-editor"
      viewBox={`0 0 ${SIZE} ${SIZE}`}
      role="img"
      aria-label="Tone curve"
      onPointerDown={(e) => {
        const [x, y] = fromEvent(e);
        let i = sorted.findIndex(([px, py]) => Math.hypot(px - x, py - y) < 12);
        if (i < 0) {
          const next = [...sorted, [x, y] as [number, number]].sort((a, b) => a[0] - b[0]);
          i = next.findIndex((p) => p[0] === x && p[1] === y);
          onChange(next);
        }
        setDrag(i);
        (e.target as Element).setPointerCapture(e.pointerId);
      }}
      onPointerMove={(e) => {
        if (drag === null) return;
        const [x, y] = fromEvent(e);
        const next = sorted.map((p, j) => (j === drag ? ([j === 0 ? 0 : j === sorted.length - 1 ? 255 : x, y] as [number, number]) : p));
        onChange(next);
      }}
      onPointerUp={() => setDrag(null)}
      onDoubleClick={(e) => {
        const [x, y] = fromEvent(e as unknown as React.PointerEvent);
        const i = sorted.findIndex(([px, py]) => Math.hypot(px - x, py - y) < 12);
        if (i > 0 && i < sorted.length - 1) onChange(sorted.filter((_, j) => j !== i));
      }}
    >
      <rect width={SIZE} height={SIZE} className="curve-bg" />
      {[1, 2, 3].map((k) => (
        <g key={k} className="curve-grid">
          <line x1={(k * SIZE) / 4} y1={0} x2={(k * SIZE) / 4} y2={SIZE} />
          <line y1={(k * SIZE) / 4} x1={0} y2={(k * SIZE) / 4} x2={SIZE} />
        </g>
      ))}
      <line x1={0} y1={SIZE} x2={SIZE} y2={0} className="curve-diag" />
      <path d={path} className="curve-line" />
      {sorted.map(([x, y], i) => {
        const [cx, cy] = toSvg(x, y);
        return <circle key={i} cx={cx} cy={cy} r={4} className="curve-point" />;
      })}
    </svg>
  );
}
