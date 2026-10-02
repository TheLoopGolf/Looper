/**
 * Deterministic brush engine. A stroke accumulates per-pixel coverage
 * (build-up with flow) from evenly spaced stamps; the coverage is then applied
 * to pixels, a mask or the selection. The live preview and the committed
 * `paint.stroke` command run exactly this code, so what you see is what is
 * recorded.
 */
import { tileCoords, tileKey, TILE_SIZE, tilesInRect } from '../tiles';

export interface BrushSettings {
  /** Diameter in pixels. */
  readonly size: number;
  /** 0 = soft falloff from the center, 1 = hard edge. */
  readonly hardness: number;
  /** Max coverage for the whole stroke (0..1). */
  readonly opacity: number;
  /** Coverage added per stamp (0..1). */
  readonly flow: number;
  /** Stamp spacing as a fraction of the diameter. */
  readonly spacing: number;
  readonly pressureSize: boolean;
  readonly pressureOpacity: boolean;
}

export const DEFAULT_BRUSH: BrushSettings = {
  size: 30,
  hardness: 0.8,
  opacity: 1,
  flow: 1,
  spacing: 0.15,
  pressureSize: true,
  pressureOpacity: false,
};

export interface StrokePoint {
  readonly x: number;
  readonly y: number;
  /** 0..1 (mouse = 1). */
  readonly pressure: number;
}

export class StrokeAccumulator {
  /** Coverage 0..1 per touched tile. */
  readonly tiles = new Map<number, Float32Array>();
  private last: StrokePoint | null = null;
  /** Distance travelled since the last stamp. */
  private carry = 0;

  constructor(
    readonly brush: BrushSettings,
    readonly width: number,
    readonly height: number,
  ) {}

  /** Adds points; returns the keys of tiles whose coverage changed. */
  addPoints(points: readonly StrokePoint[]): Set<number> {
    const dirty = new Set<number>();
    for (const p of points) {
      if (!this.last) {
        this.stamp(p.x, p.y, p.pressure, dirty);
        this.last = p;
        continue;
      }
      const a = this.last;
      const len = Math.hypot(p.x - a.x, p.y - a.y);
      if (len === 0) continue;
      let t = 0;
      // Walk the segment placing stamps every `step` pixels (step follows pressure-scaled size).
      for (;;) {
        const pr = a.pressure + (p.pressure - a.pressure) * (t / len);
        const step = Math.max(0.5, this.brush.spacing * this.diameter(pr));
        const need = step - this.carry;
        if (t + need > len) {
          this.carry += len - t;
          break;
        }
        t += need;
        this.carry = 0;
        const f = t / len;
        this.stamp(a.x + (p.x - a.x) * f, a.y + (p.y - a.y) * f, a.pressure + (p.pressure - a.pressure) * f, dirty);
      }
      this.last = p;
    }
    return dirty;
  }

  private diameter(pressure: number): number {
    return this.brush.pressureSize ? Math.max(1, this.brush.size * pressure) : this.brush.size;
  }

  private stamp(cx: number, cy: number, pressure: number, dirty: Set<number>): void {
    const r = Math.max(0.5, this.diameter(pressure) / 2);
    const flow = this.brush.flow * (this.brush.pressureOpacity ? pressure : 1);
    const inner = r * Math.min(1, Math.max(0, this.brush.hardness));
    const rect = { x: Math.floor(cx - r - 1), y: Math.floor(cy - r - 1), width: Math.ceil(2 * r + 3), height: Math.ceil(2 * r + 3) };
    for (const key of tilesInRect(this.width, this.height, rect)) {
      const [tx, ty] = tileCoords(key);
      let acc = this.tiles.get(key);
      if (!acc) {
        acc = new Float32Array(TILE_SIZE * TILE_SIZE);
        this.tiles.set(key, acc);
      }
      const x0 = Math.max(rect.x, tx * TILE_SIZE), x1 = Math.min(rect.x + rect.width, (tx + 1) * TILE_SIZE, this.width);
      const y0 = Math.max(rect.y, ty * TILE_SIZE), y1 = Math.min(rect.y + rect.height, (ty + 1) * TILE_SIZE, this.height);
      let touched = false;
      for (let y = y0; y < y1; y++) {
        for (let x = x0; x < x1; x++) {
          const d = Math.hypot(x + 0.5 - cx, y + 0.5 - cy);
          if (d >= r + 0.5) continue;
          const edge = Math.min(1, Math.max(0, r - d + 0.5)); // 1px anti-aliased rim
          let fall = 1;
          if (d > inner && r > inner) {
            const t = Math.min(1, Math.max(0, (r - d) / (r - inner)));
            fall = t * t * (3 - 2 * t);
          }
          const a = Math.min(edge, fall) * flow;
          if (a <= 0) continue;
          const i = (y - ty * TILE_SIZE) * TILE_SIZE + (x - tx * TILE_SIZE);
          acc[i] = acc[i] + a * (1 - acc[i]);
          touched = true;
        }
      }
      if (touched) dirty.add(key);
    }
  }
}

/** Flat [x, y, pressure, …] list ⇄ points (the compact form stored in history and sent by agents). */
export function unpackPoints(flat: readonly number[]): StrokePoint[] {
  const out: StrokePoint[] = [];
  for (let i = 0; i + 2 < flat.length; i += 3) out.push({ x: flat[i], y: flat[i + 1], pressure: Math.min(1, Math.max(0, flat[i + 2])) });
  return out;
}

export function packPoints(points: readonly StrokePoint[]): number[] {
  return points.flatMap((p) => [Math.round(p.x * 100) / 100, Math.round(p.y * 100) / 100, Math.round(p.pressure * 1000) / 1000]);
}

export { tileKey };
