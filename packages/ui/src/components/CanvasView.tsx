import type { Document } from '@canvas-ai/core';
import { createRenderer, fitView, screenToDoc, zoomAt, type RenderBackend, type Renderer, type ViewState } from '@canvas-ai/render';
import { useEffect, useRef, useState } from 'react';
import { openBytes } from '../actions';
import { selectionOutline } from '../lib/selection-outline';
import { useEditor, type DocTab } from '../store';
import { TOOLS, type ToolContext, type ToolPointer } from '../tools';
import { TextEditorOverlay } from './TextEditorOverlay';
import { isEditableTarget } from '../shortcuts';

/** Renderer backend override for testing/debugging: ?renderer=webgpu|webgl2|cpu */
function requestedBackend(): RenderBackend | 'auto' {
  const p = new URLSearchParams(location.search).get('renderer');
  return p === 'webgpu' || p === 'webgl2' || p === 'cpu' ? p : 'auto';
}

export interface FrameStats {
  fps: number;
  frameMs: number;
}

/** Rolling frame statistics readable by the status bar without re-rendering React on every frame. */
export const frameStats: FrameStats = { fps: 0, frameMs: 0 };

/** Last pointer position in document pixels (read by the status bar). */
export const cursorPosition = { x: 0, y: 0, inside: false };

/** Builds a tool context for the active tab (also used by menu actions that drive tool sessions). */
export function toolContext(tab: DocTab, invalidate: () => void = () => {}): ToolContext {
  const state = useEditor.getState();
  const view = state.views[tab.id] ?? { zoom: 1, panX: 0, panY: 0 };
  return {
    tab,
    doc: tab.doc,
    view,
    state,
    run: (id, params) => useEditor.getState().run(id, params),
    setPreview: (doc) => useEditor.getState().setPreview(doc),
    invalidate,
    toScreen: (x, y) => [x * view.zoom + view.panX, y * view.zoom + view.panY],
  };
}

export function CanvasView({ tab }: { tab: DocTab }) {
  const hostRef = useRef<HTMLDivElement>(null);
  const overlayRef = useRef<HTMLCanvasElement>(null);
  const rendererRef = useRef<Renderer | null>(null);
  const frameRef = useRef(0);
  const spaceRef = useRef(false);
  const [ready, setReady] = useState(false);
  // Backend to use; switches to WebGL2 if a WebGPU device is lost.
  const [backend, setBackend] = useState<RenderBackend | 'auto'>(requestedBackend);
  const [dragging, setDragging] = useState(false);
  const [spaceDown, setSpaceDown] = useState(false);
  const tool = useEditor((s) => s.tool);
  const theme = useEditor((s) => s.theme);
  const view = useEditor((s) => s.views[tab.id]);
  const preview = useEditor((s) => (s.preview?.tabId === tab.id ? s.preview.doc : null));
  const quickMask = useEditor((s) => s.quickMask);
  const setView = useEditor((s) => s.setView);
  const setViewport = useEditor((s) => s.setViewport);
  const viewRef = useRef<ViewState | undefined>(view);
  viewRef.current = view;
  const shownDoc: Document = preview ?? tab.doc;
  const shownRef = useRef(shownDoc);
  shownRef.current = shownDoc;
  const antsRef = useRef<{ mask: unknown; segs: Float32Array }>({ mask: null, segs: new Float32Array(0) });
  const antsOffset = useRef(0);

  const ctx = (): ToolContext => toolContext(useEditor.getState().tabs.find((t) => t.id === tab.id) ?? tab, drawOverlay);

  const requestFrame = () => {
    if (frameRef.current) return;
    let last = performance.now();
    const tick = (now: number) => {
      frameRef.current = 0;
      const r = rendererRef.current;
      const v = viewRef.current;
      if (!r || !v) return;
      const more = r.render(v);
      const dt = now - last;
      last = now;
      frameStats.frameMs = r.stats.frameMs;
      if (dt > 0) frameStats.fps = frameStats.fps * 0.9 + (1000 / dt) * 0.1;
      if (more) frameRef.current = requestAnimationFrame(tick);
    };
    frameRef.current = requestAnimationFrame(tick);
  };

  /** Selection marching ants + quick mask tint + the active tool's guides. */
  function drawOverlay() {
    const canvas = overlayRef.current;
    const host = hostRef.current;
    const v = viewRef.current;
    if (!canvas || !host || !v) return;
    const dpr = devicePixelRatio;
    const w = host.clientWidth, h = host.clientHeight;
    if (canvas.width !== Math.round(w * dpr) || canvas.height !== Math.round(h * dpr)) {
      canvas.width = Math.round(w * dpr);
      canvas.height = Math.round(h * dpr);
    }
    const g = canvas.getContext('2d')!;
    g.setTransform(1, 0, 0, 1, 0, 0);
    g.clearRect(0, 0, canvas.width, canvas.height);
    g.setTransform(dpr, 0, 0, dpr, 0, 0);
    const doc = shownRef.current;
    const st = useEditor.getState();
    const sel = doc.selection?.mask ?? null;
    if (st.quickMask) {
      // Unselected areas tinted red.
      g.save();
      g.fillStyle = 'rgba(255,0,0,0.35)';
      g.beginPath();
      g.rect(v.panX, v.panY, doc.width * v.zoom, doc.height * v.zoom);
      g.fill();
      g.restore();
    }
    if (sel) {
      if (antsRef.current.mask !== sel) antsRef.current = { mask: sel, segs: selectionOutline(sel) };
      const segs = antsRef.current.segs;
      const path = () => {
        for (let i = 0; i < segs.length; i += 4) {
          g.moveTo(Math.round(segs[i] * v.zoom + v.panX) + 0.5, Math.round(segs[i + 1] * v.zoom + v.panY) + 0.5);
          g.lineTo(Math.round(segs[i + 2] * v.zoom + v.panX) + 0.5, Math.round(segs[i + 3] * v.zoom + v.panY) + 0.5);
        }
      };
      g.save();
      g.lineWidth = 1;
      g.strokeStyle = '#fff';
      g.beginPath();
      path();
      g.stroke();
      g.strokeStyle = '#000';
      g.setLineDash([4, 4]);
      g.lineDashOffset = -antsOffset.current;
      g.beginPath();
      path();
      g.stroke();
      g.restore();
    }
    TOOLS[st.tool]?.overlay?.(ctx(), g);
  }

  // Renderer lifecycle. Each run gets its own <canvas>: a canvas can hold only one
  // context, and async renderer creation may outlive the effect (StrictMode remounts).
  useEffect(() => {
    let disposed = false;
    const canvas = document.createElement('canvas');
    canvas.className = 'canvas';
    canvas.setAttribute('role', 'img');
    canvas.setAttribute('aria-label', `Canvas: ${tab.doc.name}`);
    canvas.dataset.testid = 'canvas';
    hostRef.current!.prepend(canvas);
    createRenderer(canvas, backend)
      .then((r) => {
        if (disposed) {
          r.dispose();
          return;
        }
        rendererRef.current = r;
        r.onLost = (reason) => {
          useEditor.getState().notify('error', `Graphics device lost (${reason}); switching renderer.`);
          setBackend(r.backend === 'webgpu' ? 'webgl2' : 'cpu');
        };
        const host = hostRef.current!;
        r.resize(host.clientWidth, host.clientHeight, devicePixelRatio);
        r.setDocument(shownRef.current);
        useEditor.getState().setRenderer(r);
        setReady(true);
        requestFrame();
      })
      .catch((e) => useEditor.getState().notify('error', `Could not start the renderer: ${(e as Error).message}`));
    return () => {
      disposed = true;
      cancelAnimationFrame(frameRef.current);
      frameRef.current = 0;
      if (rendererRef.current) {
        if (useEditor.getState().renderer === rendererRef.current) useEditor.getState().setRenderer(null);
        rendererRef.current.dispose();
        rendererRef.current = null;
      }
      setReady(false);
      canvas.remove();
    };
    // eslint-disable-next-line react-hooks/exhaustive-deps
  }, [tab.id, backend]);

  // Document (or preview) changes → dirty tiles + overlay.
  useEffect(() => {
    if (!ready) return;
    rendererRef.current!.setDocument(shownDoc);
    requestFrame();
    drawOverlay();
    // eslint-disable-next-line react-hooks/exhaustive-deps
  }, [shownDoc, ready]);

  useEffect(() => {
    if (ready) requestFrame();
    drawOverlay();
    // eslint-disable-next-line react-hooks/exhaustive-deps
  }, [view, ready, tool, quickMask]);

  // Marching ants animation.
  useEffect(() => {
    const id = setInterval(() => {
      if (!shownRef.current.selection) return;
      antsOffset.current = (antsOffset.current + 1) % 8;
      drawOverlay();
    }, 120);
    return () => clearInterval(id);
    // eslint-disable-next-line react-hooks/exhaustive-deps
  }, []);

  // Switching tools aborts the previous tool's interaction.
  const prevTool = useRef(tool);
  useEffect(() => {
    if (prevTool.current !== tool) TOOLS[prevTool.current]?.cancel?.(ctx());
    prevTool.current = tool;
    // eslint-disable-next-line react-hooks/exhaustive-deps
  }, [tool]);

  // Workspace color follows the theme's --bg token.
  useEffect(() => {
    const r = rendererRef.current;
    if (!ready || !r) return;
    const rgb = getComputedStyle(document.documentElement).getPropertyValue('--bg').trim().match(/^#([0-9a-f]{6})$/i);
    if (rgb) {
      const n = parseInt(rgb[1], 16);
      r.colors = { ...r.colors, workspace: [(n >> 16) / 255, ((n >> 8) & 255) / 255, (n & 255) / 255] };
    }
    requestFrame();
    // eslint-disable-next-line react-hooks/exhaustive-deps
  }, [theme, ready]);

  // Size tracking; the first measurement also fits the document.
  useEffect(() => {
    const host = hostRef.current!;
    const ro = new ResizeObserver(() => {
      const w = host.clientWidth;
      const h = host.clientHeight;
      // Keep the canvases on whole device pixels; a fractional page offset makes the browser resample them.
      const rect = host.getBoundingClientRect();
      const shift = `translate(${Math.round(rect.left) - rect.left}px, ${Math.round(rect.top) - rect.top}px)`;
      host.querySelectorAll('canvas').forEach((c) => (c.style.transform = shift));
      setViewport(w, h);
      rendererRef.current?.resize(w, h, devicePixelRatio);
      if (!useEditor.getState().views[tab.id]) setView(tab.id, fitView(tab.doc, w, h));
      requestFrame();
      drawOverlay();
    });
    ro.observe(host);
    return () => ro.disconnect();
    // eslint-disable-next-line react-hooks/exhaustive-deps
  }, [tab.id]);

  // Keys: space = temporary hand; tool keys (Enter/Escape/arrows) go to the active tool.
  useEffect(() => {
    const isField = isEditableTarget;
    const down = (e: KeyboardEvent) => {
      if (isField(e.target)) return;
      if (e.code === 'Space' && !(e.target as HTMLElement).closest('button')) {
        spaceRef.current = true;
        setSpaceDown(true);
        e.preventDefault();
        return;
      }
      if (e.metaKey || e.ctrlKey) return;
      const t = TOOLS[useEditor.getState().tool];
      if (t?.key?.(ctx(), e)) {
        e.preventDefault();
        e.stopImmediatePropagation();
      }
    };
    const up = (e: KeyboardEvent) => {
      if (e.code === 'Space') {
        spaceRef.current = false;
        setSpaceDown(false);
      }
    };
    // Capture so tools see Enter/Escape/arrows before global shortcuts.
    window.addEventListener('keydown', down, true);
    window.addEventListener('keyup', up);
    return () => {
      window.removeEventListener('keydown', down, true);
      window.removeEventListener('keyup', up);
    };
    // eslint-disable-next-line react-hooks/exhaustive-deps
  }, []);

  // Wheel: pan by default, zoom with Ctrl/Cmd/Alt (trackpad pinch arrives as ctrl+wheel).
  useEffect(() => {
    const host = hostRef.current!;
    const onWheel = (e: WheelEvent) => {
      e.preventDefault();
      const v = viewRef.current;
      if (!v) return;
      const rect = host.getBoundingClientRect();
      const x = e.clientX - rect.left;
      const y = e.clientY - rect.top;
      const scale = e.deltaMode === 1 ? 16 : 1;
      if (e.ctrlKey || e.metaKey || e.altKey) {
        const factor = Math.exp(-e.deltaY * scale * (e.ctrlKey && Math.abs(e.deltaY) < 20 ? 0.01 : 0.0025));
        setView(tab.id, zoomAt(v, v.zoom * factor, x, y));
      } else {
        const dx = e.shiftKey && !e.deltaX ? e.deltaY : e.deltaX;
        const dy = e.shiftKey && !e.deltaX ? 0 : e.deltaY;
        setView(tab.id, { ...v, panX: v.panX - dx * scale, panY: v.panY - dy * scale });
      }
    };
    host.addEventListener('wheel', onWheel, { passive: false });
    return () => host.removeEventListener('wheel', onWheel);
  }, [tab.id, setView]);

  const pointerInfo = (e: PointerEvent | React.PointerEvent): ToolPointer => {
    const v = viewRef.current!;
    const rect = hostRef.current!.getBoundingClientRect();
    const sx = e.clientX - rect.left;
    const sy = e.clientY - rect.top;
    const [x, y] = screenToDoc(v, sx, sy);
    return {
      x,
      y,
      sx,
      sy,
      // Mouse reports 0.5 while pressed; only pens/touch carry real pressure.
      pressure: e.pointerType === 'pen' ? Math.max(0.01, e.pressure) : 1,
      shift: e.shiftKey,
      alt: e.altKey,
      mod: e.metaKey || e.ctrlKey,
      button: e.button,
      detail: (e as PointerEvent).detail ?? 0,
    };
  };

  const onPointerDown = (e: React.PointerEvent) => {
    const v = viewRef.current;
    if (!v) return;
    // Working on the canvas takes keyboard focus away from option fields (so Enter/shortcuts reach the tools).
    const focused = document.activeElement as HTMLElement | null;
    if (focused && focused !== document.body && !focused.classList.contains('text-editor')) focused.blur();
    const p = pointerInfo(e);
    const panning = e.button === 1 || spaceRef.current || tool === 'hand';
    if (tool === 'zoom' && e.button === 0 && !spaceRef.current) {
      setView(tab.id, zoomAt(v, v.zoom * (e.altKey ? 0.5 : 2), p.sx, p.sy));
      return;
    }
    (e.target as HTMLElement).setPointerCapture(e.pointerId);
    if (panning) {
      e.preventDefault();
      setDragging(true);
      const start = { x: e.clientX, y: e.clientY, panX: v.panX, panY: v.panY };
      const move = (ev: PointerEvent) => {
        const cur = viewRef.current!;
        setView(tab.id, { ...cur, panX: start.panX + ev.clientX - start.x, panY: start.panY + ev.clientY - start.y });
      };
      const up = () => {
        setDragging(false);
        window.removeEventListener('pointermove', move);
        window.removeEventListener('pointerup', up);
        window.removeEventListener('pointercancel', up);
      };
      window.addEventListener('pointermove', move);
      window.addEventListener('pointerup', up);
      window.addEventListener('pointercancel', up);
      return;
    }
    if (e.button !== 0) return;
    const t = TOOLS[tool];
    if (!t) return;
    e.preventDefault();
    t.down?.(ctx(), p);
    const move = (ev: PointerEvent) => {
      const events = typeof ev.getCoalescedEvents === 'function' ? ev.getCoalescedEvents() : [];
      const pts = (events.length ? events : [ev]).map((c) => pointerInfo(c));
      t.move?.(ctx(), pointerInfo(ev), pts);
    };
    const up = (ev: PointerEvent) => {
      window.removeEventListener('pointermove', move);
      window.removeEventListener('pointerup', up);
      window.removeEventListener('pointercancel', up);
      t.up?.(ctx(), pointerInfo(ev));
      drawOverlay();
    };
    window.addEventListener('pointermove', move);
    window.addEventListener('pointerup', up);
    window.addEventListener('pointercancel', up);
  };

  const onPointerMove = (e: React.PointerEvent) => {
    const v = viewRef.current;
    if (!v) return;
    const p = pointerInfo(e);
    cursorPosition.x = Math.floor(p.x);
    cursorPosition.y = Math.floor(p.y);
    cursorPosition.inside = p.x >= 0 && p.y >= 0 && p.x < tab.doc.width && p.y < tab.doc.height;
    if (e.buttons === 0) TOOLS[tool]?.hover?.(ctx(), p);
  };

  const onDrop = async (e: React.DragEvent) => {
    e.preventDefault();
    const file = e.dataTransfer.files[0];
    if (file)
      await openBytes(file.name, new Uint8Array(await file.arrayBuffer())).catch((err) =>
        useEditor.getState().notify('error', `Could not open ${file.name}: ${(err as Error).message}`),
      );
  };

  const toolCursor = TOOLS[tool]?.cursor(ctx()) ?? 'default';
  const cursor = dragging ? 'grabbing' : spaceDown || tool === 'hand' ? 'grab' : tool === 'zoom' ? 'zoom-in' : toolCursor;

  return (
    <div
      ref={hostRef}
      className="canvas-host"
      style={{ cursor }}
      onPointerDown={onPointerDown}
      onPointerMove={onPointerMove}
      onPointerLeave={() => {
        cursorPosition.inside = false;
      }}
      onDragOver={(e) => e.preventDefault()}
      onDrop={onDrop}
      data-testid="canvas-host"
    >
      <canvas ref={overlayRef} className="canvas overlay" aria-hidden="true" data-testid="overlay" />
      <TextEditorOverlay tab={tab} />
    </div>
  );
}
