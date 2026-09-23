/**
 * Render test harness (dev/test only, not linked from the app). Playwright
 * loads this page to run the GPU backends against the CPU reference and to
 * benchmark large documents.
 */
import { BLEND_MODE_IDS, CommandBus, createDefaultRegistry, createDocument, type BlendMode, type Document } from '@canvas-ai/core';
import { createRenderer, renderDocumentCPU, type RenderBackend, type Renderer } from '@canvas-ai/render';
import { blendScene, groupScene } from '@canvas-ai/render/testing';
import { createSampleDocument } from '@canvas-ai/ui';

function maxDiff(a: Uint8ClampedArray, b: Uint8ClampedArray): { max: number; at: number } {
  let max = 0;
  let at = -1;
  for (let i = 0; i < a.length; i++) {
    const d = Math.abs(a[i] - b[i]);
    if (d > max) {
      max = d;
      at = i;
    }
  }
  return { max, at };
}

async function makeRenderer(backend: RenderBackend, width = 512, height = 512): Promise<{ renderer: Renderer; canvas: HTMLCanvasElement }> {
  const canvas = document.createElement('canvas');
  canvas.style.cssText = `width:${width}px;height:${height}px;display:block`;
  document.body.appendChild(canvas);
  const renderer = await createRenderer(canvas, backend);
  if (renderer.backend !== backend) throw new Error(`Requested ${backend}, got ${renderer.backend}`);
  renderer.resize(width, height, 1);
  return { renderer, canvas };
}

async function compare(backend: RenderBackend, docs: Document[]) {
  const { renderer, canvas } = await makeRenderer(backend);
  const results: { name: string; max: number; at: number; detail?: string }[] = [];
  try {
    for (const doc of docs) {
      renderer.setDocument(doc);
      const gpu = await renderer.readComposite();
      const cpu = renderDocumentCPU(doc);
      const { max, at } = maxDiff(gpu, cpu);
      const px = Math.floor(at / 4);
      results.push({
        name: doc.id,
        max,
        at,
        detail: max > 1 ? `pixel ${px % doc.width},${Math.floor(px / doc.width)} gpu=${[...gpu.subarray(px * 4, px * 4 + 4)]} cpu=${[...cpu.subarray(px * 4, px * 4 + 4)]}` : undefined,
      });
    }
  } finally {
    renderer.dispose();
    canvas.remove();
  }
  return results;
}

/** Builds a large benchmark document through the command bus (like a user would). */
function benchmarkDocument(width: number, height: number, layers: number): Document {
  const bus = new CommandBus(createDefaultRegistry(), createDocument({ id: 'bench', width, height }));
  bus.dispatch('layer.create', { name: 'Background', color: [240, 236, 228, 255] });
  const modes: BlendMode[] = ['normal', 'multiply', 'screen', 'overlay', 'softLight', 'difference', 'color', 'luminosity'];
  for (let i = 1; i < layers; i++) {
    const { layerId } = bus.dispatch<{ layerId: string }>('layer.create', { name: `Layer ${i}` });
    const w = Math.round(width * (0.25 + ((i * 37) % 50) / 100));
    const h = Math.round(height * (0.25 + ((i * 53) % 50) / 100));
    const x = Math.round(((width - w) * ((i * 29) % 100)) / 100);
    const y = Math.round(((height - h) * ((i * 71) % 100)) / 100);
    bus.dispatch('pixels.fillRect', { layerId, x, y, width: w, height: h, color: [(i * 47) % 256, (i * 91) % 256, (i * 13) % 256, 160 + (i % 4) * 30] });
    bus.dispatch('layer.setBlendMode', { layerId, blendMode: modes[i % modes.length] });
    bus.dispatch('layer.setOpacity', { layerId, opacity: 60 + (i % 5) * 10 });
  }
  return bus.document;
}

/** Renders until no tiles are pending, then measures pan/zoom frame times. */
async function benchmark(backend: RenderBackend, opts: { width: number; height: number; layers: number; frames: number }) {
  const t0 = performance.now();
  const doc = benchmarkDocument(opts.width, opts.height, opts.layers);
  const buildMs = performance.now() - t0;
  const vw = 1280;
  const vh = 800;
  const { renderer, canvas } = await makeRenderer(backend, vw, vh);
  renderer.frameBudgetMs = 1e9;
  renderer.setDocument(doc);
  const fit = Math.min(vw / opts.width, vh / opts.height);
  let view = { zoom: fit, panX: (vw - opts.width * fit) / 2, panY: (vh - opts.height * fit) / 2 };
  const t1 = performance.now();
  while (renderer.render(view));
  await renderer.readComposite({ x: 0, y: 0, width: 1, height: 1 }); // wait for GPU to finish
  const compositeAllMs = performance.now() - t1;

  // Pan/zoom: every frame draws cached tiles only.
  const times: number[] = [];
  const nextFrame = () => new Promise<number>((r) => requestAnimationFrame(r));
  let last = await nextFrame();
  for (let i = 0; i < opts.frames; i++) {
    const zoom = fit * (1 + 0.5 * Math.sin(i / 20));
    view = { zoom, panX: view.panX + Math.sin(i / 7) * 10, panY: view.panY + Math.cos(i / 9) * 10 };
    renderer.render(view);
    const now = await nextFrame();
    times.push(now - last);
    last = now;
  }
  const renderCpuMs = renderer.stats.frameMs;
  renderer.dispose();
  canvas.remove();
  times.sort((a, b) => a - b);
  return {
    backend,
    buildMs,
    compositeAllMs,
    tiles: Math.ceil(opts.width / 256) * Math.ceil(opts.height / 256),
    frameMsP50: times[Math.floor(times.length / 2)],
    frameMsP95: times[Math.floor(times.length * 0.95)],
    fps: 1000 / times[Math.floor(times.length / 2)],
    lastFrameCpuMs: renderCpuMs,
  };
}

/** Drives a renderer with rAF like the app does (time-budgeted, progressive compositing). */
async function renderLoop(backend: RenderBackend, docName: 'sample' | 'bench', budgetMs = 8, frames = 60) {
  const doc = docName === 'sample' ? createSampleDocument() : benchmarkDocument(2048, 2048, 8);
  const { renderer, canvas } = await makeRenderer(backend, 1000, 700);
  renderer.frameBudgetMs = budgetMs;
  renderer.setDocument(doc);
  const view = { zoom: 0.5, panX: 10, panY: 10 };
  let pendingFrames = 0;
  for (let i = 0; i < frames; i++) {
    if (i % 5 === 2) renderer.resize(1000 + (i % 2), 700, 1);
    if (renderer.render(view)) pendingFrames++;
    await new Promise((r) => requestAnimationFrame(r));
  }
  while (renderer.render(view));
  // WebGPU canvases are cleared after presentation, so capture in the same task as the draw.
  const snapshot = canvas.toDataURL('image/png');
  const px = await renderer.readComposite({ x: 1000, y: 700, width: 1, height: 1 });
  renderer.dispose();
  canvas.remove();
  return { pendingFrames, px: [...px], snapshot };
}

const harness = {
  renderLoop,
  blendModes: BLEND_MODE_IDS,
  compareBlendModes: (backend: RenderBackend) => compare(backend, BLEND_MODE_IDS.map((m) => blendScene(m))),
  compareGroups: (backend: RenderBackend) => compare(backend, (['multiply', 'normal', 'passThrough'] as const).map((m) => ({ ...groupScene(m), id: `groups-${m}` }))),
  benchmark,
};

(window as unknown as { harness: typeof harness }).harness = harness;
document.title = 'harness ready';
