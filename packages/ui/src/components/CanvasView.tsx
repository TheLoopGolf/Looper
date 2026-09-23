import { createRenderer, fitView, screenToDoc, zoomAt, type RenderBackend, type Renderer, type ViewState } from '@canvas-ai/render';
import { useEffect, useRef, useState } from 'react';
import { openBytes } from '../actions';
import { useEditor, type DocTab } from '../store';

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

export function CanvasView({ tab }: { tab: DocTab }) {
  const hostRef = useRef<HTMLDivElement>(null);
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
  const setView = useEditor((s) => s.setView);
  const setViewport = useEditor((s) => s.setViewport);
  const viewRef = useRef<ViewState | undefined>(view);
  viewRef.current = view;

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

  // Renderer lifecycle. Each run gets its own <canvas>: a canvas can hold only one
  // context, and async renderer creation may outlive the effect (StrictMode remounts).
  useEffect(() => {
    let disposed = false;
    const canvas = document.createElement('canvas');
    canvas.className = 'canvas';
    canvas.setAttribute('role', 'img');
    canvas.setAttribute('aria-label', `Canvas: ${tab.doc.name}`);
    canvas.dataset.testid = 'canvas';
    hostRef.current!.appendChild(canvas);
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
        r.setDocument(useEditor.getState().tabs.find((t) => t.id === tab.id)?.doc ?? tab.doc);
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

  // Document changes → dirty tiles.
  useEffect(() => {
    if (!ready) return;
    rendererRef.current!.setDocument(tab.doc);
    requestFrame();
    // eslint-disable-next-line react-hooks/exhaustive-deps
  }, [tab.doc, ready]);

  useEffect(() => {
    if (ready) requestFrame();
    // eslint-disable-next-line react-hooks/exhaustive-deps
  }, [view, ready]);

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
      // Keep the canvas on whole device pixels; a fractional page offset makes the browser resample it.
      const rect = host.getBoundingClientRect();
      const canvas = host.querySelector('canvas');
      if (canvas) canvas.style.transform = `translate(${Math.round(rect.left) - rect.left}px, ${Math.round(rect.top) - rect.top}px)`;
      setViewport(w, h);
      rendererRef.current?.resize(w, h, devicePixelRatio);
      if (!useEditor.getState().views[tab.id]) setView(tab.id, fitView(tab.doc, w, h));
      requestFrame();
    });
    ro.observe(host);
    return () => ro.disconnect();
    // eslint-disable-next-line react-hooks/exhaustive-deps
  }, [tab.id]);

  // Space = temporary hand tool.
  useEffect(() => {
    const down = (e: KeyboardEvent) => {
      if (e.code === 'Space' && !(e.target as HTMLElement).closest('input,textarea,select,button')) {
        spaceRef.current = true;
        setSpaceDown(true);
        e.preventDefault();
      }
    };
    const up = (e: KeyboardEvent) => {
      if (e.code === 'Space') {
        spaceRef.current = false;
        setSpaceDown(false);
      }
    };
    window.addEventListener('keydown', down);
    window.addEventListener('keyup', up);
    return () => {
      window.removeEventListener('keydown', down);
      window.removeEventListener('keyup', up);
    };
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

  const onPointerDown = (e: React.PointerEvent) => {
    const v = viewRef.current;
    if (!v) return;
    const rect = hostRef.current!.getBoundingClientRect();
    const x = e.clientX - rect.left;
    const y = e.clientY - rect.top;
    const panning = e.button === 1 || spaceRef.current || tool === 'hand';
    if (tool === 'zoom' && e.button === 0 && !spaceRef.current) {
      setView(tab.id, zoomAt(v, v.zoom * (e.altKey ? 0.5 : 2), x, y));
      return;
    }
    if (!panning) return;
    e.preventDefault();
    (e.target as HTMLElement).setPointerCapture(e.pointerId);
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
  };

  const onPointerMove = (e: React.PointerEvent) => {
    const v = viewRef.current;
    if (!v) return;
    const rect = hostRef.current!.getBoundingClientRect();
    const [dx, dy] = screenToDoc(v, e.clientX - rect.left, e.clientY - rect.top);
    cursorPosition.x = Math.floor(dx);
    cursorPosition.y = Math.floor(dy);
    cursorPosition.inside = dx >= 0 && dy >= 0 && dx < tab.doc.width && dy < tab.doc.height;
  };

  const onDrop = async (e: React.DragEvent) => {
    e.preventDefault();
    const file = e.dataTransfer.files[0];
    if (file) await openBytes(file.name, new Uint8Array(await file.arrayBuffer())).catch((err) =>
      useEditor.getState().notify('error', `Could not open ${file.name}: ${(err as Error).message}`),
    );
  };

  const cursor = dragging ? 'grabbing' : spaceDown || tool === 'hand' ? 'grab' : tool === 'zoom' ? 'zoom-in' : 'default';

  return (
    <div
      ref={hostRef}
      className="canvas-host"
      style={{ cursor }}
      onPointerDown={onPointerDown}
      onPointerMove={onPointerMove}
      onDragOver={(e) => e.preventDefault()}
      onDrop={onDrop}
      data-testid="canvas-host"
    >
    </div>
  );
}

/** Last pointer position in document pixels (read by the status bar). */
export const cursorPosition = { x: 0, y: 0, inside: false };
