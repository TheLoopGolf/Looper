import {
  CommandError,
  DEFAULT_BRUSH,
  findLayer,
  packPoints,
  PatchBuilder,
  raster,
  strokeSource,
  StrokeAccumulator,
  unpackPoints,
  type Document,
  type LayerNode,
  type StrokePoint,
  type TileGrid,
} from '@canvas-ai/core';
import { useEditor } from '../store';
import type { Tool, ToolContext, ToolPointer } from './types';

interface Stroke {
  acc: StrokeAccumulator;
  source: TileGrid;
  preview: TileGrid;
  defaultValue: number;
  target: 'pixels' | 'mask' | 'selection';
  layerId?: string;
  mode: 'paint' | 'erase';
  color: [number, number, number, number];
  brush: typeof DEFAULT_BRUSH;
  points: StrokePoint[];
}

let stroke: Stroke | null = null;
let hover: { sx: number; sy: number } | null = null;
let lastEnd: StrokePoint | null = null;

/** Round-trips through the stored precision so the preview equals the committed command exactly. */
const quantize = (pts: StrokePoint[]) => unpackPoints(packPoints(pts));

function previewDoc(doc: Document, s: Stroke): Document {
  const b = new PatchBuilder(doc);
  if (s.target === 'selection') return b.setDocProps({ selection: s.preview.tiles.size ? { mask: s.preview } : null }).doc;
  const layer = findLayer(doc, s.layerId!)!.layer;
  if (s.target === 'mask') return b.replaceLayer({ ...layer, mask: { ...layer.mask!, tiles: s.preview } } as LayerNode).doc;
  return b.replaceLayer({ ...layer, tiles: s.preview } as LayerNode).doc;
}

function brushTool(mode: 'paint' | 'erase'): Tool {
  const optionsKey = mode === 'paint' ? 'brush' : 'eraser';
  return {
    cursor: () => 'none',
    down(ctx, p) {
      const st = ctx.state;
      const o = st.toolOptions[optionsKey];
      const layerId = ctx.tab.activeLayerId ?? undefined;
      const target = st.quickMask ? 'selection' : st.editTarget === 'mask' && layerId && findLayer(ctx.doc, layerId)?.layer.mask ? 'mask' : 'pixels';
      let src: ReturnType<typeof strokeSource>;
      try {
        src = strokeSource(ctx.doc, { layerId, target });
      } catch (e) {
        st.notify('error', e instanceof CommandError ? e.message : String(e));
        return;
      }
      const brush = { ...DEFAULT_BRUSH, size: o.size, hardness: o.hardness, opacity: o.opacity / 100, flow: o.flow / 100, spacing: o.spacing, pressureSize: o.pressureSize };
      stroke = {
        acc: new StrokeAccumulator(brush, ctx.doc.width, ctx.doc.height),
        source: src.grid,
        preview: src.grid,
        defaultValue: src.defaultValue,
        target,
        layerId,
        mode,
        color: st.colors.fg,
        brush,
        points: [],
      };
      const start = { x: p.x, y: p.y, pressure: p.pressure };
      // Shift-click draws a straight line from the end of the previous stroke.
      this.move!(ctx, p, p.shift && lastEnd ? [{ ...p, x: lastEnd.x, y: lastEnd.y, pressure: lastEnd.pressure }, p] : [p]);
      void start;
    },
    move(ctx, p, events) {
      hover = { sx: p.sx, sy: p.sy };
      const s = stroke;
      if (!s) {
        ctx.invalidate();
        return;
      }
      const pts = quantize(events.map((e) => ({ x: e.x, y: e.y, pressure: e.pressure })));
      s.points.push(...pts);
      const dirty = s.acc.addPoints(pts);
      if (dirty.size) {
        s.preview = raster.applyStroke(
          s.source,
          s.acc,
          dirty,
          { mode: s.mode, color: s.color, selection: s.target === 'selection' ? null : (ctx.doc.selection?.mask ?? null), defaultValue: s.defaultValue, value: s.target === 'selection' ? 255 : undefined },
          s.preview,
        );
        ctx.setPreview(previewDoc(ctx.doc, s));
      }
      ctx.invalidate();
    },
    up(ctx) {
      const s = stroke;
      stroke = null;
      if (!s || !s.points.length) return;
      lastEnd = s.points[s.points.length - 1];
      ctx.run('paint.stroke', {
        ...(s.layerId && s.target !== 'selection' ? { layerId: s.layerId } : {}),
        target: s.target,
        mode: s.mode,
        color: s.color,
        brush: s.brush,
        points: packPoints(s.points),
      });
      ctx.setPreview(null);
    },
    hover(ctx, p) {
      hover = { sx: p.sx, sy: p.sy };
      ctx.invalidate();
    },
    overlay(ctx, g) {
      if (!hover) return;
      const o = ctx.state.toolOptions[optionsKey];
      const r = Math.max(1, (o.size * ctx.view.zoom) / 2);
      g.save();
      g.lineWidth = 1;
      g.strokeStyle = 'rgba(0,0,0,0.8)';
      g.beginPath();
      g.arc(hover.sx, hover.sy, r, 0, Math.PI * 2);
      g.stroke();
      g.strokeStyle = 'rgba(255,255,255,0.8)';
      g.beginPath();
      g.arc(hover.sx, hover.sy, r + 1, 0, Math.PI * 2);
      g.stroke();
      g.restore();
    },
    cancel(ctx) {
      stroke = null;
      hover = null;
      ctx.setPreview(null);
    },
    busy: () => stroke !== null,
  };
}

export const brush = brushTool('paint');
export const eraser = brushTool('erase');

/** Pixel layer id for single-click pixel tools, or a toast explaining why not. */
function requireActive(ctx: ToolContext): string | null {
  const id = ctx.tab.activeLayerId;
  if (!id) {
    ctx.state.notify('error', 'Select a layer first');
    return null;
  }
  return id;
}

export const bucket: Tool = {
  cursor: () => 'crosshair',
  down(ctx, p) {
    const layerId = requireActive(ctx);
    if (!layerId) return;
    const o = ctx.state.toolOptions.bucket;
    ctx.run('pixels.floodFill', {
      layerId,
      x: Math.floor(p.x),
      y: Math.floor(p.y),
      color: ctx.state.colors.fg,
      tolerance: o.tolerance,
      contiguous: o.contiguous,
      sampleMerged: o.sampleMerged,
      opacity: o.opacity,
    });
  },
};

let gradientDrag: { x0: number; y0: number; x1: number; y1: number } | null = null;

export const gradient: Tool = {
  cursor: () => 'crosshair',
  down(_ctx, p) {
    gradientDrag = { x0: p.x, y0: p.y, x1: p.x, y1: p.y };
  },
  move(ctx, p) {
    if (!gradientDrag) return;
    let { x, y } = p;
    if (p.shift) {
      // Snap to 45° increments.
      const a = Math.round(Math.atan2(y - gradientDrag.y0, x - gradientDrag.x0) / (Math.PI / 4)) * (Math.PI / 4);
      const len = Math.hypot(x - gradientDrag.x0, y - gradientDrag.y0);
      x = gradientDrag.x0 + Math.cos(a) * len;
      y = gradientDrag.y0 + Math.sin(a) * len;
    }
    gradientDrag = { ...gradientDrag, x1: x, y1: y };
    ctx.invalidate();
  },
  up(ctx) {
    const d = gradientDrag;
    gradientDrag = null;
    ctx.invalidate();
    if (!d || Math.hypot(d.x1 - d.x0, d.y1 - d.y0) < 1) return;
    const layerId = requireActive(ctx);
    if (!layerId) return;
    const o = ctx.state.toolOptions.gradient;
    ctx.run('pixels.gradient', { layerId, kind: o.kind, ...d, colorA: ctx.state.colors.fg, colorB: ctx.state.colors.bg, opacity: o.opacity });
  },
  overlay(ctx, g) {
    if (!gradientDrag) return;
    const [ax, ay] = ctx.toScreen(gradientDrag.x0, gradientDrag.y0);
    const [bx, by] = ctx.toScreen(gradientDrag.x1, gradientDrag.y1);
    g.save();
    g.lineWidth = 3;
    g.strokeStyle = 'rgba(0,0,0,0.6)';
    g.beginPath();
    g.moveTo(ax, ay);
    g.lineTo(bx, by);
    g.stroke();
    g.lineWidth = 1;
    g.strokeStyle = '#fff';
    g.stroke();
    for (const [x, y] of [[ax, ay], [bx, by]]) {
      g.fillStyle = '#fff';
      g.fillRect(x - 3, y - 3, 6, 6);
      g.strokeStyle = '#000';
      g.strokeRect(x - 3, y - 3, 6, 6);
    }
    g.restore();
  },
  cancel() {
    gradientDrag = null;
  },
  busy: () => gradientDrag !== null,
};

export const eyedropper: Tool = {
  cursor: () => 'crosshair',
  down(ctx, p) {
    const x = Math.floor(p.x), y = Math.floor(p.y);
    if (x < 0 || y < 0 || x >= ctx.doc.width || y >= ctx.doc.height) return;
    const which = p.alt ? 'bg' : 'fg';
    const renderer = ctx.state.renderer;
    void (renderer
      ? renderer.readComposite({ x, y, width: 1, height: 1 })
      : Promise.resolve(new Uint8ClampedArray([0, 0, 0, 0]))
    ).then((px) => {
      if (px[3] === 0) return;
      useEditor.getState().setColor(which, [px[0], px[1], px[2], 255]);
    });
  },
};

export type { ToolPointer };
