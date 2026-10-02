import { findLayer, raster, type TextLayer } from '@canvas-ai/core';
import { useEditor } from '../store';
import type { Tool, ToolContext, ToolPointer } from './types';

// ---------------------------------------------------------------------------
// Shape tool: drag for rect/ellipse/polygon/line, click points for pen paths
// ---------------------------------------------------------------------------

let shapeDrag: { x0: number; y0: number; x1: number; y1: number; constrain: boolean } | null = null;
let pen: { pts: number[]; cursor: [number, number] } | null = null;

function geometry(ctx: ToolContext): Record<string, unknown> | null {
  const d = shapeDrag;
  if (!d) return null;
  const o = ctx.state.toolOptions.shape;
  let w = d.x1 - d.x0, h = d.y1 - d.y0;
  if (d.constrain && o.kind !== 'line') {
    const s = Math.max(Math.abs(w), Math.abs(h));
    w = Math.sign(w || 1) * s;
    h = Math.sign(h || 1) * s;
  }
  switch (o.kind) {
    case 'rect':
      return { kind: 'rect', x: Math.min(d.x0, d.x0 + w), y: Math.min(d.y0, d.y0 + h), width: Math.abs(w), height: Math.abs(h), radius: o.radius };
    case 'ellipse':
      return { kind: 'ellipse', cx: d.x0 + w / 2, cy: d.y0 + h / 2, rx: Math.abs(w) / 2, ry: Math.abs(h) / 2 };
    case 'polygon':
      return { kind: 'polygon', cx: d.x0, cy: d.y0, radius: Math.hypot(w, h), sides: o.sides, rotation: (Math.atan2(h, w) * 180) / Math.PI + 90 };
    case 'line': {
      let x1 = d.x1, y1 = d.y1;
      if (d.constrain) {
        const a = Math.round(Math.atan2(h, w) / (Math.PI / 4)) * (Math.PI / 4);
        const len = Math.hypot(w, h);
        x1 = d.x0 + Math.cos(a) * len;
        y1 = d.y0 + Math.sin(a) * len;
      }
      return { kind: 'line', x1: d.x0, y1: d.y0, x2: x1, y2: y1 };
    }
    default:
      return null;
  }
}

function createShape(ctx: ToolContext, shape: Record<string, unknown>) {
  const o = ctx.state.toolOptions.shape;
  const { fg, bg } = ctx.state.colors;
  const isLine = shape.kind === 'line' || (shape.kind === 'path' && !shape.closed);
  const fill = o.fill && !isLine;
  const stroke = o.stroke || isLine;
  ctx.run('layer.createShape', {
    shape,
    fillColor: fill ? fg : null,
    strokeColor: stroke ? (fill ? bg : fg) : null,
    strokeWidth: stroke ? o.strokeWidth : 0,
    ...(ctx.tab.activeLayerId ? { above: ctx.tab.activeLayerId } : {}),
  });
}

function finishPen(ctx: ToolContext, closed: boolean) {
  const p = pen;
  pen = null;
  ctx.invalidate();
  if (p && p.pts.length >= 4) createShape(ctx, { kind: 'path', points: p.pts.map((v) => Math.round(v * 10) / 10), closed: closed && p.pts.length >= 6 });
}

function drawOutline(ctx: ToolContext, g: CanvasRenderingContext2D, shape: Record<string, unknown>, extra?: [number, number]) {
  const { points, closed } = raster.shapeOutline(shape as never);
  g.save();
  g.lineWidth = 1;
  g.strokeStyle = '#2dd4bf';
  g.beginPath();
  for (let i = 0; i < points.length; i += 2) {
    const [x, y] = ctx.toScreen(points[i], points[i + 1]);
    if (i === 0) g.moveTo(x, y);
    else g.lineTo(x, y);
  }
  if (extra) g.lineTo(...ctx.toScreen(extra[0], extra[1]));
  if (closed) g.closePath();
  g.stroke();
  g.restore();
}

export const shape: Tool = {
  cursor: () => 'crosshair',
  down(ctx, p: ToolPointer) {
    if (ctx.state.toolOptions.shape.kind === 'pen') {
      if (!pen) {
        pen = { pts: [p.x, p.y], cursor: [p.x, p.y] };
        return;
      }
      const [sx, sy] = ctx.toScreen(pen.pts[0], pen.pts[1]);
      if (Math.hypot(p.sx - sx, p.sy - sy) < 8 && pen.pts.length >= 6) return finishPen(ctx, true);
      if (p.detail >= 2) return finishPen(ctx, false);
      pen.pts.push(p.x, p.y);
      ctx.invalidate();
      return;
    }
    shapeDrag = { x0: p.x, y0: p.y, x1: p.x, y1: p.y, constrain: p.shift };
  },
  move(ctx, p) {
    if (pen) {
      pen.cursor = [p.x, p.y];
      ctx.invalidate();
      return;
    }
    if (!shapeDrag) return;
    shapeDrag = { ...shapeDrag, x1: p.x, y1: p.y, constrain: p.shift };
    ctx.invalidate();
  },
  hover(ctx, p) {
    if (pen) this.move!(ctx, p, [p]);
  },
  up(ctx) {
    const g = geometry(ctx);
    const d = shapeDrag;
    shapeDrag = null;
    ctx.invalidate();
    if (!g || !d || Math.hypot(d.x1 - d.x0, d.y1 - d.y0) < 2) return;
    createShape(ctx, g);
  },
  key(ctx, e) {
    if (!pen) return false;
    if (e.key === 'Enter') finishPen(ctx, false);
    else if (e.key === 'Escape') {
      pen = null;
      ctx.invalidate();
    } else return false;
    return true;
  },
  overlay(ctx, g) {
    if (pen) return drawOutline(ctx, g, { kind: 'path', points: pen.pts.length >= 4 ? pen.pts : [...pen.pts, ...pen.pts], closed: false }, pen.cursor);
    const geo = geometry(ctx);
    if (geo) drawOutline(ctx, g, geo);
  },
  cancel() {
    shapeDrag = null;
    pen = null;
  },
  busy: () => shapeDrag !== null || pen !== null,
};

// ---------------------------------------------------------------------------
// Text tool: click to start typing (new layer) or click an active text layer to edit it.
// The editor itself is a React overlay (TextEditorOverlay) driven by store.textEdit.
// ---------------------------------------------------------------------------

function textBounds(ctx: ToolContext, layer: TextLayer) {
  const grid = raster.layerPixels(layer, ctx.doc.width, ctx.doc.height);
  const b = grid && raster.gridBounds(grid);
  const tight = b && raster.tightBounds(raster.readGrid(grid!, b));
  return tight ?? { x: layer.x, y: layer.y, width: layer.style.fontSize * 2, height: layer.style.fontSize };
}

export const text: Tool = {
  cursor: () => 'text',
  down(ctx, p) {
    const st = useEditor.getState();
    if (st.textEdit) return; // the overlay commits on blur
    const active = ctx.tab.activeLayerId ? findLayer(ctx.doc, ctx.tab.activeLayerId)?.layer : null;
    if (active?.type === 'text') {
      const b = textBounds(ctx, active);
      if (p.x >= b.x - 4 && p.y >= b.y - 4 && p.x <= b.x + b.width + 4 && p.y <= b.y + b.height + 4) {
        st.setTextEdit({ layerId: active.id, x: active.x, y: active.y, text: active.text });
        return;
      }
    }
    st.setTextEdit({ layerId: null, x: Math.round(p.x), y: Math.round(p.y - ctx.state.toolOptions.text.fontSize * 0.8), text: '' });
  },
};
