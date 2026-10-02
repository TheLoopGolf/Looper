import { findLayer, raster, type Document } from '@canvas-ai/core';
import { useEditor } from '../store';
import { frameThrottle, previewCommand } from './preview';
import type { Tool, ToolContext, ToolPointer } from './types';

type Affine = readonly [number, number, number, number, number, number];
const { multiply, translate, rotate, scale } = raster;

// ---------------------------------------------------------------------------
// Move tool (drag = translate whole pixels; arrows nudge)
// ---------------------------------------------------------------------------

let moveDrag: { x0: number; y0: number; dx: number; dy: number; layerId: string } | null = null;
let moveCtx: ToolContext | null = null;
const movePreview = frameThrottle(() => {
  if (!moveDrag || !moveCtx) return;
  moveCtx.setPreview(previewCommand(moveCtx, 'layer.translate', { layerId: moveDrag.layerId, dx: moveDrag.dx, dy: moveDrag.dy }));
});

function contentBounds(doc: Document, layerId: string) {
  const layer = findLayer(doc, layerId)?.layer;
  if (!layer) return null;
  const collect = (l: typeof layer): import('@canvas-ai/core').TileGrid[] =>
    l.type === 'group' ? l.children.flatMap(collect) : [raster.layerPixels(l, doc.width, doc.height)].filter((g): g is NonNullable<typeof g> => !!g);
  let box: { x: number; y: number; width: number; height: number } | null = null;
  for (const grid of collect(layer)) {
    const coarse = raster.gridBounds(grid);
    if (!coarse) continue;
    const tight = raster.tightBounds(raster.readGrid(grid, coarse));
    if (!tight) continue;
    box = box
      ? (() => {
          const x0 = Math.min(box!.x, tight.x), y0 = Math.min(box!.y, tight.y);
          const x1 = Math.max(box!.x + box!.width, tight.x + tight.width), y1 = Math.max(box!.y + box!.height, tight.y + tight.height);
          return { x: x0, y: y0, width: x1 - x0, height: y1 - y0 };
        })()
      : tight;
  }
  return box;
}

// ---------------------------------------------------------------------------
// Free transform session (Edit › Free Transform)
// ---------------------------------------------------------------------------

export interface TransformState {
  layerId: string;
  box: { x: number; y: number; width: number; height: number };
  tx: number;
  ty: number;
  sx: number;
  sy: number;
  angle: number;
}

let tf: TransformState | null = null;
let tfDrag: { kind: 'move' | 'rotate' | 'scale'; hx: number; hy: number; start: TransformState; px: number; py: number } | null = null;
let tfCtx: ToolContext | null = null;
const tfPreview = frameThrottle(() => {
  if (!tf || !tfCtx) return;
  tfCtx.setPreview(previewCommand(tfCtx, 'layer.transform', { layerId: tf.layerId, matrix: [...transformMatrix(tf)] }));
});

export function transformMatrix(t: TransformState): Affine {
  const cx = t.box.x + t.box.width / 2, cy = t.box.y + t.box.height / 2;
  return multiply(translate(cx + t.tx, cy + t.ty), multiply(rotate(t.angle), multiply(scale(t.sx, t.sy), translate(-cx, -cy))));
}

export function getTransform(): TransformState | null {
  return tf;
}

const notify = () => useEditor.setState((st) => ({ transformVersion: st.transformVersion + 1 }));

export function beginTransform(ctx: Pick<ToolContext, 'doc' | 'tab' | 'state'>): boolean {
  const layerId = ctx.tab.activeLayerId;
  if (!layerId) return false;
  const loc = findLayer(ctx.doc, layerId);
  if (!loc || loc.layer.locked || loc.layer.type === 'adjustment') {
    ctx.state.notify('error', loc?.layer.locked ? 'The layer is locked' : 'Select a layer with content to transform');
    return false;
  }
  const box = contentBounds(ctx.doc, layerId);
  if (!box) {
    ctx.state.notify('error', 'The layer is empty');
    return false;
  }
  tf = { layerId, box, tx: 0, ty: 0, sx: 1, sy: 1, angle: 0 };
  notify();
  return true;
}

export function commitTransform(ctx: Pick<ToolContext, 'run' | 'setPreview' | 'invalidate'>) {
  const t = tf;
  tf = null;
  tfDrag = null;
  tfPreview.cancel();
  notify();
  ctx.setPreview(null);
  ctx.invalidate();
  if (t && (t.tx || t.ty || t.sx !== 1 || t.sy !== 1 || t.angle)) ctx.run('layer.transform', { layerId: t.layerId, matrix: [...transformMatrix(t)] });
}

export function cancelTransform(ctx: Pick<ToolContext, 'setPreview' | 'invalidate'>) {
  tf = null;
  tfDrag = null;
  tfPreview.cancel();
  notify();
  ctx.setPreview(null);
  ctx.invalidate();
}

export function updateTransform(ctx: ToolContext, patch: Partial<Pick<TransformState, 'sx' | 'sy' | 'angle'>>) {
  if (!tf) return;
  tf = { ...tf, ...patch };
  tfCtx = ctx;
  tfPreview.schedule();
  ctx.invalidate();
}

/** Screen-space corners/edge midpoints of the transform box: [hx, hy, x, y]. */
function handles(ctx: ToolContext, t: TransformState): [number, number, number, number][] {
  const m = transformMatrix(t);
  const out: [number, number, number, number][] = [];
  for (const hy of [-1, 0, 1]) for (const hx of [-1, 0, 1]) {
    if (!hx && !hy) continue;
    const [dx, dy] = raster.apply(m, t.box.x + ((hx + 1) / 2) * t.box.width, t.box.y + ((hy + 1) / 2) * t.box.height);
    out.push([hx, hy, ...ctx.toScreen(dx, dy)]);
  }
  return out;
}

function pointInBox(t: TransformState, x: number, y: number): boolean {
  const [lx, ly] = raster.apply(raster.invert(transformMatrix(t)), x, y);
  return lx >= t.box.x && ly >= t.box.y && lx <= t.box.x + t.box.width && ly <= t.box.y + t.box.height;
}

function transformDown(ctx: ToolContext, p: ToolPointer) {
  const t = tf!;
  if (p.detail >= 2 && pointInBox(t, p.x, p.y)) return commitTransform(ctx);
  const hit = handles(ctx, t).find(([, , x, y]) => Math.hypot(p.sx - x, p.sy - y) < 8);
  const kind = hit ? 'scale' : pointInBox(t, p.x, p.y) ? 'move' : 'rotate';
  tfDrag = { kind, hx: hit?.[0] ?? 0, hy: hit?.[1] ?? 0, start: { ...t }, px: p.x, py: p.y };
}

function transformMove(ctx: ToolContext, p: ToolPointer) {
  const d = tfDrag;
  if (!d || !tf) return;
  const s = d.start;
  const cx = s.box.x + s.box.width / 2 + s.tx, cy = s.box.y + s.box.height / 2 + s.ty;
  if (d.kind === 'move') {
    tf = { ...tf, tx: s.tx + Math.round(p.x - d.px), ty: s.ty + Math.round(p.y - d.py) };
  } else if (d.kind === 'rotate') {
    let a = s.angle + ((Math.atan2(p.y - cy, p.x - cx) - Math.atan2(d.py - cy, d.px - cx)) * 180) / Math.PI;
    if (p.shift) a = Math.round(a / 15) * 15;
    tf = { ...tf, angle: a };
  } else {
    // Scale with the opposite handle fixed, in the box's rotated frame.
    const rad = (s.angle * Math.PI) / 180;
    const cos = Math.cos(rad), sin = Math.sin(rad);
    const qx = cos * (p.x - cx) + sin * (p.y - cy);
    const qy = -sin * (p.x - cx) + cos * (p.y - cy);
    const hw = (s.box.width / 2) * s.sx, hh = (s.box.height / 2) * s.sy;
    let sx = s.sx, sy = s.sy, mx = 0, my = 0;
    if (d.hx) {
      const ax = -d.hx * hw;
      sx = ((qx - ax) * d.hx) / s.box.width;
      mx = (qx + ax) / 2;
    }
    if (d.hy) {
      const ay = -d.hy * hh;
      sy = ((qy - ay) * d.hy) / s.box.height;
      my = (qy + ay) / 2;
    }
    if (p.shift && d.hx && d.hy) {
      const k = Math.max(Math.abs(sx / s.sx), Math.abs(sy / s.sy));
      sx = Math.sign(sx || 1) * Math.abs(s.sx) * k;
      sy = Math.sign(sy || 1) * Math.abs(s.sy) * k;
      mx = -d.hx * hw + (d.hx * sx * s.box.width) / 2;
      my = -d.hy * hh + (d.hy * sy * s.box.height) / 2;
    }
    if (Math.abs(sx) < 0.01) sx = 0.01 * Math.sign(sx || 1);
    if (Math.abs(sy) < 0.01) sy = 0.01 * Math.sign(sy || 1);
    tf = { ...tf, sx, sy, tx: s.tx + cos * mx - sin * my, ty: s.ty + sin * mx + cos * my };
  }
  tfCtx = ctx;
  tfPreview.schedule();
  ctx.invalidate();
}

function transformOverlay(ctx: ToolContext, g: CanvasRenderingContext2D) {
  const t = tf!;
  const hs = handles(ctx, t);
  const corner = (hx: number, hy: number) => hs.find((h) => h[0] === hx && h[1] === hy)!;
  g.save();
  g.strokeStyle = '#2dd4bf';
  g.lineWidth = 1;
  g.beginPath();
  for (const [hx, hy] of [[-1, -1], [1, -1], [1, 1], [-1, 1]] as const) {
    const [, , x, y] = corner(hx, hy);
    if (hx === -1 && hy === -1) g.moveTo(x, y);
    else g.lineTo(x, y);
  }
  g.closePath();
  g.stroke();
  for (const [, , x, y] of hs) {
    g.fillStyle = '#fff';
    g.fillRect(x - 4, y - 4, 8, 8);
    g.strokeRect(x - 4, y - 4, 8, 8);
  }
  g.restore();
}

export const move: Tool = {
  cursor: () => (tf ? 'default' : 'move'),
  down(ctx, p) {
    if (tf) return transformDown(ctx, p);
    const layerId = ctx.tab.activeLayerId;
    if (!layerId) return;
    moveDrag = { x0: p.x, y0: p.y, dx: 0, dy: 0, layerId };
  },
  move(ctx, p) {
    if (tf) return transformMove(ctx, p);
    if (!moveDrag) return;
    moveDrag.dx = Math.round(p.x - moveDrag.x0);
    moveDrag.dy = Math.round(p.y - moveDrag.y0);
    moveCtx = ctx;
    movePreview.schedule();
  },
  up(ctx) {
    if (tf) {
      tfDrag = null;
      return;
    }
    const d = moveDrag;
    moveDrag = null;
    movePreview.cancel();
    if (d && (d.dx || d.dy)) ctx.run('layer.translate', { layerId: d.layerId, dx: d.dx, dy: d.dy });
    ctx.setPreview(null);
  },
  key(ctx, e) {
    if (tf) {
      if (e.key === 'Enter') commitTransform(ctx);
      else if (e.key === 'Escape') cancelTransform(ctx);
      else return false;
      return true;
    }
    const step = e.shiftKey ? 10 : 1;
    const delta = { ArrowLeft: [-step, 0], ArrowRight: [step, 0], ArrowUp: [0, -step], ArrowDown: [0, step] }[e.key];
    if (!delta || !ctx.tab.activeLayerId) return false;
    ctx.run('layer.translate', { layerId: ctx.tab.activeLayerId, dx: delta[0], dy: delta[1] });
    return true;
  },
  overlay(ctx, g) {
    if (tf) transformOverlay(ctx, g);
  },
  cancel(ctx) {
    moveDrag = null;
    movePreview.cancel();
    if (tf) cancelTransform(ctx);
  },
  busy: () => moveDrag !== null || tf !== null,
};

// ---------------------------------------------------------------------------
// Crop tool
// ---------------------------------------------------------------------------

/** `touched` = false while the box is still the default full canvas (dragging then draws a new box). */
let crop: { x: number; y: number; width: number; height: number; docId: string; touched: boolean } | null = null;
let cropDrag: { kind: 'new' | 'move' | 'scale'; hx: number; hy: number; start: { x: number; y: number; width: number; height: number }; px: number; py: number } | null = null;

const ASPECTS: Record<string, number | null> = { free: null, '1:1': 1, '4:5': 4 / 5, '3:2': 3 / 2, '16:9': 16 / 9, '9:16': 9 / 16 };

function aspectFor(ctx: ToolContext): number | null {
  const a = ctx.state.toolOptions.crop.aspect;
  return a === 'original' ? ctx.doc.width / ctx.doc.height : (ASPECTS[a] ?? null);
}

function ensureCrop(ctx: ToolContext) {
  if (!crop || crop.docId !== ctx.doc.id) crop = { x: 0, y: 0, width: ctx.doc.width, height: ctx.doc.height, docId: ctx.doc.id, touched: false };
  return crop;
}

function cropHandles(ctx: ToolContext): [number, number, number, number][] {
  const c = ensureCrop(ctx);
  const out: [number, number, number, number][] = [];
  for (const hy of [-1, 0, 1]) for (const hx of [-1, 0, 1]) {
    if (!hx && !hy) continue;
    out.push([hx, hy, ...ctx.toScreen(c.x + ((hx + 1) / 2) * c.width, c.y + ((hy + 1) / 2) * c.height)]);
  }
  return out;
}

export function commitCrop(ctx: Pick<ToolContext, 'run' | 'state' | 'invalidate'>) {
  if (!crop) return;
  const c = crop;
  crop = null;
  ctx.invalidate();
  const x = Math.round(c.x), y = Math.round(c.y), width = Math.round(c.width), height = Math.round(c.height);
  if (width < 1 || height < 1) return;
  ctx.run('document.crop', { x, y, width, height, angle: ctx.state.toolOptions.crop.angle });
  useEditor.getState().setToolOptions('crop', { angle: 0 });
}

export function resetCrop() {
  crop = null;
  cropDrag = null;
}

export const cropTool: Tool = {
  cursor: () => 'crosshair',
  down(ctx, p) {
    const c = ensureCrop(ctx);
    if (p.detail >= 2) return commitCrop(ctx);
    const hit = cropHandles(ctx).find(([, , x, y]) => Math.hypot(p.sx - x, p.sy - y) < 8);
    const inside = p.x >= c.x && p.y >= c.y && p.x <= c.x + c.width && p.y <= c.y + c.height;
    cropDrag = { kind: hit ? 'scale' : inside && c.touched ? 'move' : 'new', hx: hit?.[0] ?? 1, hy: hit?.[1] ?? 1, start: { ...c }, px: p.x, py: p.y };
    c.touched = true;
    if (cropDrag.kind === 'new') {
      cropDrag.start = { x: p.x, y: p.y, width: 0, height: 0 };
    }
  },
  move(ctx, p) {
    const d = cropDrag;
    if (!d) return;
    const c = ensureCrop(ctx);
    const aspect = aspectFor(ctx);
    if (d.kind === 'move') {
      c.x = d.start.x + (p.x - d.px);
      c.y = d.start.y + (p.y - d.py);
    } else {
      // Anchor = opposite edge/corner of the starting rect.
      const s = d.start;
      const ax = d.kind === 'new' ? s.x : d.hx > 0 ? s.x : s.x + s.width;
      const ay = d.kind === 'new' ? s.y : d.hy > 0 ? s.y : s.y + s.height;
      let x0 = ax, y0 = ay, x1 = d.hx || d.kind === 'new' ? p.x : s.x + s.width, y1 = d.hy || d.kind === 'new' ? p.y : s.y + s.height;
      if (d.kind === 'scale' && !d.hx) { x0 = s.x; x1 = s.x + s.width; }
      if (d.kind === 'scale' && !d.hy) { y0 = s.y; y1 = s.y + s.height; }
      let w = x1 - x0, h = y1 - y0;
      if (aspect) {
        if (Math.abs(w) / aspect > Math.abs(h)) h = (Math.sign(h) || 1) * (Math.abs(w) / aspect);
        else w = (Math.sign(w) || 1) * Math.abs(h) * aspect;
      }
      c.x = Math.min(x0, x0 + w);
      c.y = Math.min(y0, y0 + h);
      c.width = Math.abs(w);
      c.height = Math.abs(h);
    }
    ctx.invalidate();
  },
  up() {
    cropDrag = null;
  },
  key(ctx, e) {
    if (e.key === 'Enter') {
      commitCrop(ctx);
      return true;
    }
    if (e.key === 'Escape') {
      resetCrop();
      ctx.invalidate();
      return true;
    }
    return false;
  },
  overlay(ctx, g) {
    const c = ensureCrop(ctx);
    const angle = (ctx.state.toolOptions.crop.angle * Math.PI) / 180;
    const [cx, cy] = ctx.toScreen(c.x + c.width / 2, c.y + c.height / 2);
    const w = c.width * ctx.view.zoom, h = c.height * ctx.view.zoom;
    g.save();
    // Dim everything outside the (rotated) crop rect.
    g.fillStyle = 'rgba(0,0,0,0.5)';
    g.beginPath();
    g.rect(0, 0, g.canvas.width, g.canvas.height);
    g.translate(cx, cy);
    g.rotate(angle);
    g.rect(-w / 2, -h / 2, w, h);
    g.fill('evenodd');
    g.strokeStyle = '#fff';
    g.lineWidth = 1;
    g.strokeRect(-w / 2 + 0.5, -h / 2 + 0.5, w, h);
    g.strokeStyle = 'rgba(255,255,255,0.4)';
    g.beginPath();
    for (const f of [1 / 3, 2 / 3]) {
      g.moveTo(-w / 2 + w * f, -h / 2);
      g.lineTo(-w / 2 + w * f, h / 2);
      g.moveTo(-w / 2, -h / 2 + h * f);
      g.lineTo(w / 2, -h / 2 + h * f);
    }
    g.stroke();
    g.restore();
    g.save();
    for (const [, , x, y] of cropHandles(ctx)) {
      g.fillStyle = '#fff';
      g.fillRect(x - 4, y - 4, 8, 8);
      g.strokeStyle = '#000';
      g.strokeRect(x - 4, y - 4, 8, 8);
    }
    g.restore();
  },
  cancel() {
    resetCrop();
  },
};

export function getCropRect() {
  return crop;
}
