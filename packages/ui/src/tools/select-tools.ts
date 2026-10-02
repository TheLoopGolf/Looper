import { findLayer } from '@canvas-ai/core';
import { modeFromModifiers, type Tool, type ToolContext, type ToolPointer } from './types';

interface Drag {
  x0: number;
  y0: number;
  x1: number;
  y1: number;
  mode: 'replace' | 'add' | 'subtract' | 'intersect';
}

let drag: Drag | null = null;

function rectOf(d: Drag, square: boolean) {
  let w = d.x1 - d.x0, h = d.y1 - d.y0;
  if (square) {
    const s = Math.max(Math.abs(w), Math.abs(h));
    w = Math.sign(w || 1) * s;
    h = Math.sign(h || 1) * s;
  }
  return { x: Math.round(Math.min(d.x0, d.x0 + w)), y: Math.round(Math.min(d.y0, d.y0 + h)), width: Math.round(Math.abs(w)), height: Math.round(Math.abs(h)) };
}

let square = false;

function marqueeTool(kind: 'rect' | 'ellipse'): Tool {
  return {
    cursor: () => 'crosshair',
    down(ctx, p) {
      drag = { x0: p.x, y0: p.y, x1: p.x, y1: p.y, mode: modeFromModifiers(p, ctx.state.toolOptions.select.mode) };
    },
    move(ctx, p) {
      if (!drag) return;
      drag = { ...drag, x1: p.x, y1: p.y };
      square = p.shift && drag.mode === 'replace';
      ctx.invalidate();
    },
    up(ctx) {
      const d = drag;
      drag = null;
      ctx.invalidate();
      if (!d) return;
      const r = rectOf(d, square);
      const o = ctx.state.toolOptions.select;
      if (r.width < 1 || r.height < 1) {
        if (d.mode === 'replace') ctx.run('selection.deselect', {});
        return;
      }
      ctx.run(kind === 'rect' ? 'selection.rect' : 'selection.ellipse', { ...r, mode: d.mode, feather: o.feather, antiAlias: o.antiAlias });
    },
    overlay(ctx, g) {
      if (!drag) return;
      const r = rectOf(drag, square);
      const [x0, y0] = ctx.toScreen(r.x, r.y);
      const [x1, y1] = ctx.toScreen(r.x + r.width, r.y + r.height);
      outline(g, () => {
        if (kind === 'rect') g.rect(x0 + 0.5, y0 + 0.5, x1 - x0, y1 - y0);
        else g.ellipse((x0 + x1) / 2, (y0 + y1) / 2, Math.abs(x1 - x0) / 2, Math.abs(y1 - y0) / 2, 0, 0, Math.PI * 2);
      });
    },
    cancel() {
      drag = null;
    },
    busy: () => drag !== null,
  };
}

function outline(g: CanvasRenderingContext2D, path: () => void) {
  g.save();
  g.lineWidth = 1;
  g.strokeStyle = '#fff';
  g.beginPath();
  path();
  g.stroke();
  g.setLineDash([4, 4]);
  g.strokeStyle = '#000';
  g.beginPath();
  path();
  g.stroke();
  g.restore();
}

export const marquee = marqueeTool('rect');
export const ellipseMarquee = marqueeTool('ellipse');

let lassoPts: number[] | null = null;
let lassoMode: Drag['mode'] = 'replace';

export const lasso: Tool = {
  cursor: () => 'crosshair',
  down(ctx, p) {
    lassoPts = [p.x, p.y];
    lassoMode = modeFromModifiers(p, ctx.state.toolOptions.select.mode);
  },
  move(ctx, p, events) {
    if (!lassoPts) return;
    for (const e of events) lassoPts.push(e.x, e.y);
    ctx.invalidate();
  },
  up(ctx) {
    const pts = lassoPts;
    lassoPts = null;
    ctx.invalidate();
    if (!pts || pts.length < 6) return;
    const o = ctx.state.toolOptions.select;
    ctx.run('selection.polygon', { points: pts.map((v) => Math.round(v * 4) / 4), mode: lassoMode, feather: o.feather, antiAlias: o.antiAlias });
  },
  overlay(ctx, g) {
    if (!lassoPts) return;
    polyline(ctx, g, lassoPts, false);
  },
  cancel() {
    lassoPts = null;
  },
  busy: () => lassoPts !== null,
};

function polyline(ctx: ToolContext, g: CanvasRenderingContext2D, pts: number[], closed: boolean, extra?: [number, number]) {
  outline(g, () => {
    for (let i = 0; i < pts.length; i += 2) {
      const [x, y] = ctx.toScreen(pts[i], pts[i + 1]);
      if (i === 0) g.moveTo(x, y);
      else g.lineTo(x, y);
    }
    if (extra) g.lineTo(...ctx.toScreen(extra[0], extra[1]));
    if (closed) g.closePath();
  });
}

let poly: { pts: number[]; cursor: [number, number]; mode: Drag['mode'] } | null = null;

function finishPoly(ctx: ToolContext) {
  const p = poly;
  poly = null;
  ctx.invalidate();
  if (!p || p.pts.length < 6) return;
  const o = ctx.state.toolOptions.select;
  ctx.run('selection.polygon', { points: p.pts, mode: p.mode, feather: o.feather, antiAlias: o.antiAlias });
}

export const polyLasso: Tool = {
  cursor: () => 'crosshair',
  down(ctx, p: ToolPointer) {
    if (!poly) {
      poly = { pts: [p.x, p.y], cursor: [p.x, p.y], mode: modeFromModifiers(p, ctx.state.toolOptions.select.mode) };
      return;
    }
    const [sx, sy] = ctx.toScreen(poly.pts[0], poly.pts[1]);
    if (p.detail >= 2 || Math.hypot(p.sx - sx, p.sy - sy) < 8) return finishPoly(ctx);
    poly.pts.push(p.x, p.y);
    ctx.invalidate();
  },
  hover(ctx, p) {
    if (!poly) return;
    poly.cursor = [p.x, p.y];
    ctx.invalidate();
  },
  move(ctx, p) {
    this.hover!(ctx, p);
  },
  key(ctx, e) {
    if (!poly) return false;
    if (e.key === 'Enter') finishPoly(ctx);
    else if (e.key === 'Escape') {
      poly = null;
      ctx.invalidate();
    } else if (e.key === 'Backspace' && poly.pts.length > 2) {
      poly.pts.length -= 2;
      ctx.invalidate();
    } else return false;
    return true;
  },
  overlay(ctx, g) {
    if (poly) polyline(ctx, g, poly.pts, false, poly.cursor);
  },
  cancel() {
    poly = null;
  },
  busy: () => poly !== null,
};

export const wand: Tool = {
  cursor: () => 'crosshair',
  down(ctx, p) {
    const o = ctx.state.toolOptions.wand;
    const id = ctx.tab.activeLayerId;
    const l = id ? findLayer(ctx.doc, id)?.layer : undefined;
    const layer = l && l.type !== 'group' && l.type !== 'adjustment' ? l.id : undefined;
    ctx.run('selection.magicWand', {
      x: Math.floor(p.x),
      y: Math.floor(p.y),
      tolerance: o.tolerance,
      contiguous: o.contiguous,
      sampleMerged: o.sampleMerged || !layer,
      ...(layer && !o.sampleMerged ? { layerId: layer } : {}),
      mode: modeFromModifiers(p, ctx.state.toolOptions.select.mode),
    });
  },
};
