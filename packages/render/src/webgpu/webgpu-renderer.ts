import { TILE_SIZE, tileCoords, type Document, type Tile } from '@canvas-ai/core';
import { raster, type MaskRef, type Plan } from '@canvas-ai/core';
import { CHECKER_SIZE, deviceRect, TiledRenderer, type RenderBackend } from '../renderer';
import type { ViewState } from '../view';
import { MIP_WGSL, PRESENT_WGSL, TILE_OPS_WGSL } from './shaders';

const TILE_BYTES = TILE_SIZE * TILE_SIZE * 4;
const MIP_LEVELS = Math.log2(TILE_SIZE) + 1;
const SLOT = 256; // minUniformBufferOffsetAlignment upper bound
const ARENA_SLOTS = 4096;

interface Target {
  tex: GPUTexture;
  view: GPUTextureView;
}

/**
 * Per-frame command recording. Uniforms for every draw are packed into one
 * arena buffer that is uploaded right before submit (queue writes are ordered
 * before the submitted commands).
 */
class Frame {
  readonly encoder: GPUCommandEncoder;
  private arena: ArrayBuffer = new ArrayBuffer(SLOT * ARENA_SLOTS);
  private used = 0;
  constructor(private device: GPUDevice, private buffer: GPUBuffer) {
    this.encoder = device.createCommandEncoder();
  }
  get full(): boolean {
    return this.used >= ARENA_SLOTS;
  }
  uniform(write: (view: DataView) => void, size: number): GPUBufferBinding {
    const offset = this.used++ * SLOT;
    write(new DataView(this.arena, offset, SLOT));
    return { buffer: this.buffer, offset, size };
  }
  submit(): void {
    if (this.used) this.device.queue.writeBuffer(this.buffer, 0, this.arena, 0, this.used * SLOT);
    this.device.queue.submit([this.encoder.finish()]);
  }
}

/** WebGPU backend: same plan execution as WebGL2, WGSL shaders, explicit mip generation. */
export class WebGPURenderer extends TiledRenderer {
  readonly backend: RenderBackend = 'webgpu';
  layerCacheBudget = 256 * 1024 * 1024;
  layerCacheMaxAge = 600;
  private context: GPUCanvasContext;
  private format: GPUTextureFormat;
  private pipelines!: Record<'blend' | 'mix' | 'adjust' | 'copy' | 'premultiply' | 'mip' | 'tile' | 'checker', GPURenderPipeline>;
  private lutTextures = new Map<raster.CompiledAdjustment, { tex: GPUTexture; view: GPUTextureView; lastUse: number }>();
  /** Bound when an op has no mask / LUT (explicit layouts need every binding). */
  private dummyMask!: GPUTextureView;
  private dummyLut!: GPUTextureView;
  private samplers!: { nearest: GPUSampler; linear: GPUSampler };
  private arenas: GPUBuffer[] = [];
  private frame: Frame | null = null;
  private frameNo = 0;
  private scratch: Target[] = [];
  private layerTextures = new Map<Tile, { tex: GPUTexture; view: GPUTextureView; lastUse: number }>();
  private display = new Map<number, { tex: GPUTexture; full: GPUTextureView; levels: GPUTextureView[] }>();
  private lost = false;
  private disposed = false;

  static async create(canvas: HTMLCanvasElement): Promise<WebGPURenderer> {
    if (!('gpu' in navigator) || !navigator.gpu) throw new Error('WebGPU is not available');
    const adapter = await navigator.gpu.requestAdapter({ powerPreference: 'high-performance' });
    if (!adapter) throw new Error('No WebGPU adapter');
    const device = await adapter.requestDevice();
    return new WebGPURenderer(canvas, device);
  }

  private constructor(
    private canvas: HTMLCanvasElement,
    private device: GPUDevice,
  ) {
    super();
    this.stats.backend = 'webgpu';
    const context = canvas.getContext('webgpu');
    if (!context) throw new Error('WebGPU canvas context unavailable');
    this.context = context;
    this.format = navigator.gpu.getPreferredCanvasFormat();
    context.configure({ device, format: this.format, alphaMode: 'opaque' });
    device.addEventListener('uncapturederror', (e) => console.error('WebGPU error:', (e as GPUUncapturedErrorEvent).error.message));
    device.lost.then((info) => {
      this.lost = true;
      if (!this.disposed) this.onLost?.(info.message || info.reason || 'device lost');
    });
    this.init();
  }

  private init(): void {
    const d = this.device;
    const tileModule = d.createShaderModule({ code: TILE_OPS_WGSL });
    const mipModule = d.createShaderModule({ code: MIP_WGSL });
    const presentModule = d.createShaderModule({ code: PRESENT_WGSL });
    const tileLayout = d.createBindGroupLayout({
      entries: [
        { binding: 0, visibility: GPUShaderStage.FRAGMENT, texture: { sampleType: 'unfilterable-float' } },
        { binding: 1, visibility: GPUShaderStage.FRAGMENT, texture: { sampleType: 'unfilterable-float' } },
        { binding: 2, visibility: GPUShaderStage.FRAGMENT, buffer: { type: 'uniform' } },
        { binding: 3, visibility: GPUShaderStage.FRAGMENT, texture: { sampleType: 'unfilterable-float' } },
        { binding: 4, visibility: GPUShaderStage.FRAGMENT, texture: { sampleType: 'unfilterable-float' } },
      ],
    });
    const tilePipelineLayout = d.createPipelineLayout({ bindGroupLayouts: [tileLayout] });
    const tileOp = (entryPoint: string, format: GPUTextureFormat = 'rgba8unorm') =>
      d.createRenderPipeline({
        layout: tilePipelineLayout,
        vertex: { module: tileModule, entryPoint: 'vs' },
        fragment: { module: tileModule, entryPoint, targets: [{ format }] },
        primitive: { topology: 'triangle-list' },
      });
    const premulBlend: GPUBlendState = {
      color: { srcFactor: 'one', dstFactor: 'one-minus-src-alpha', operation: 'add' },
      alpha: { srcFactor: 'one', dstFactor: 'one-minus-src-alpha', operation: 'add' },
    };
    const presentLayout = d.createBindGroupLayout({
      entries: [
        { binding: 0, visibility: GPUShaderStage.VERTEX | GPUShaderStage.FRAGMENT, buffer: { type: 'uniform' } },
        { binding: 1, visibility: GPUShaderStage.FRAGMENT, texture: { sampleType: 'float' } },
        { binding: 2, visibility: GPUShaderStage.FRAGMENT, sampler: { type: 'filtering' } },
      ],
    });
    const checkerLayout = d.createBindGroupLayout({
      entries: [{ binding: 0, visibility: GPUShaderStage.VERTEX | GPUShaderStage.FRAGMENT, buffer: { type: 'uniform' } }],
    });
    this.pipelines = {
      blend: tileOp('blend'),
      mix: tileOp('mixOp'),
      adjust: tileOp('adjust'),
      copy: tileOp('copyOp'),
      premultiply: tileOp('premultiply'),
      mip: d.createRenderPipeline({
        layout: 'auto',
        vertex: { module: mipModule, entryPoint: 'vs' },
        fragment: { module: mipModule, entryPoint: 'fs', targets: [{ format: 'rgba8unorm' }] },
      }),
      tile: d.createRenderPipeline({
        layout: d.createPipelineLayout({ bindGroupLayouts: [presentLayout] }),
        vertex: { module: presentModule, entryPoint: 'vs' },
        fragment: { module: presentModule, entryPoint: 'tile', targets: [{ format: this.format, blend: premulBlend }] },
      }),
      checker: d.createRenderPipeline({
        layout: d.createPipelineLayout({ bindGroupLayouts: [checkerLayout] }),
        vertex: { module: presentModule, entryPoint: 'vs' },
        fragment: { module: presentModule, entryPoint: 'checker', targets: [{ format: this.format }] },
      }),
    };
    const dummyMask = d.createTexture({ size: [TILE_SIZE, TILE_SIZE], format: 'r8unorm', usage: GPUTextureUsage.TEXTURE_BINDING | GPUTextureUsage.COPY_DST });
    d.queue.writeTexture({ texture: dummyMask }, new Uint8Array(TILE_SIZE * TILE_SIZE).fill(255), { bytesPerRow: TILE_SIZE }, [TILE_SIZE, TILE_SIZE]);
    this.dummyMask = dummyMask.createView();
    this.dummyLut = this.createLut(raster.IDENTITY_LUT).createView();
    this.samplers = {
      nearest: d.createSampler({ magFilter: 'nearest', minFilter: 'nearest' }),
      linear: d.createSampler({ magFilter: 'linear', minFilter: 'linear', mipmapFilter: 'linear', maxAnisotropy: 1 }),
    };
  }

  // -- frame / resource management -----------------------------------------

  private currentFrame(): Frame {
    if (this.frame?.full) this.flush();
    if (!this.frame) {
      const buffer =
        this.arenas.pop() ?? this.device.createBuffer({ size: SLOT * ARENA_SLOTS, usage: GPUBufferUsage.UNIFORM | GPUBufferUsage.COPY_DST });
      this.frame = new Frame(this.device, buffer);
      this.inFlight.push(buffer);
    }
    return this.frame;
  }

  private inFlight: GPUBuffer[] = [];

  private flush(): void {
    if (!this.frame) return;
    this.frame.submit();
    this.frame = null;
    const buffers = this.inFlight;
    this.inFlight = [];
    this.device.queue.onSubmittedWorkDone().then(() => this.arenas.push(...buffers));
  }

  private createTarget(): Target {
    const tex = this.device.createTexture({
      size: [TILE_SIZE, TILE_SIZE],
      format: 'rgba8unorm',
      usage: GPUTextureUsage.RENDER_ATTACHMENT | GPUTextureUsage.TEXTURE_BINDING | GPUTextureUsage.COPY_SRC,
    });
    return { tex, view: tex.createView() };
  }

  private acquire(): Target {
    return this.scratch.pop() ?? this.createTarget();
  }

  private release(t: Target): void {
    this.scratch.push(t);
  }

  private createLut(lut: Uint8Array): GPUTexture {
    const tex = this.device.createTexture({ size: [256, 1], format: 'rgba8unorm', usage: GPUTextureUsage.TEXTURE_BINDING | GPUTextureUsage.COPY_DST });
    this.device.queue.writeTexture({ texture: tex }, lut as Uint8Array<ArrayBuffer>, { bytesPerRow: 1024 }, [256, 1]);
    return tex;
  }

  private lutTexture(adj: raster.CompiledAdjustment): GPUTextureView {
    let entry = this.lutTextures.get(adj);
    if (!entry) {
      const tex = this.createLut(adj.lut);
      entry = { tex, view: tex.createView(), lastUse: this.frameNo };
      this.lutTextures.set(adj, entry);
    }
    entry.lastUse = this.frameNo;
    return entry.view;
  }

  private layerTexture(tile: Tile): GPUTextureView {
    let entry = this.layerTextures.get(tile);
    if (!entry) {
      const mask = tile.channels === 1;
      const tex = this.device.createTexture({
        size: [TILE_SIZE, TILE_SIZE],
        format: mask ? 'r8unorm' : 'rgba8unorm',
        usage: GPUTextureUsage.TEXTURE_BINDING | GPUTextureUsage.COPY_DST,
      });
      this.device.queue.writeTexture({ texture: tex }, tile.data as Uint8ClampedArray<ArrayBuffer>, { bytesPerRow: TILE_SIZE * tile.channels }, [TILE_SIZE, TILE_SIZE]);
      entry = { tex, view: tex.createView(), lastUse: this.frameNo };
      this.layerTextures.set(tile, entry);
    }
    entry.lastUse = this.frameNo;
    return entry.view;
  }

  private pass(target: GPUTextureView, clear: boolean): GPURenderPassEncoder {
    return this.currentFrame().encoder.beginRenderPass({
      colorAttachments: [{ view: target, loadOp: clear ? 'clear' : 'load', storeOp: 'store', clearValue: [0, 0, 0, 0] }],
    });
  }

  private tileOp(
    pipeline: GPURenderPipeline,
    target: Target,
    backdrop: GPUTextureView,
    src: GPUTextureView,
    o: { mode?: number; opacity?: number; validMax?: [number, number]; mask?: MaskRef | null; adjustment?: raster.CompiledAdjustment } = {},
  ): void {
    const frame = this.currentFrame();
    const validMax = o.validMax ?? [TILE_SIZE - 1, TILE_SIZE - 1];
    const uniform = frame.uniform((v) => {
      v.setUint32(0, o.mode ?? 0, true);
      v.setFloat32(4, o.opacity ?? 1, true);
      v.setUint32(8, validMax[0], true);
      v.setUint32(12, validMax[1], true);
      v.setUint32(16, o.mask ? 1 : 0, true);
      v.setFloat32(20, o.mask?.density ?? 1, true);
      v.setUint32(24, o.adjustment?.kernel ?? 0, true);
      if (o.adjustment) o.adjustment.params.forEach((x, i) => v.setFloat32(32 + i * 4, x, true));
    }, 96);
    const bind = this.device.createBindGroup({
      layout: pipeline.getBindGroupLayout(0),
      entries: [
        { binding: 0, resource: backdrop },
        { binding: 1, resource: src },
        { binding: 2, resource: uniform },
        { binding: 3, resource: o.mask ? this.layerTexture(o.mask.tile) : this.dummyMask },
        { binding: 4, resource: o.adjustment ? this.lutTexture(o.adjustment) : this.dummyLut },
      ],
    });
    const pass = this.pass(target.view, false);
    pass.setPipeline(pipeline);
    pass.setBindGroup(0, bind);
    pass.draw(3);
    pass.end();
  }

  private clearTarget(t: Target): void {
    this.pass(t.view, true).end();
  }

  // -- plan execution (mirrors WebGL2Renderer.execute) ------------------------

  private execute(plan: Plan, cur: Target): Target {
    for (const item of plan) {
      if (item.kind === 'passThrough' && item.opacity >= 1 && !item.mask) {
        cur = this.execute(item.children, cur);
        continue;
      }
      const out = this.acquire();
      if (item.kind === 'adjust') {
        this.tileOp(this.pipelines.adjust, out, cur.view, cur.view, { mode: item.modeIndex, opacity: item.opacity, mask: item.mask, adjustment: item.adjustment });
        this.release(cur);
        cur = out;
        continue;
      }
      let src: GPUTextureView;
      let inner: Target | null = null;
      if (item.kind === 'layer') {
        src = this.layerTexture(item.tile);
      } else if (item.kind === 'isolated') {
        inner = this.acquire();
        this.clearTarget(inner);
        inner = this.execute(item.children, inner);
        src = inner.view;
      } else {
        inner = this.acquire();
        this.tileOp(this.pipelines.copy, inner, cur.view, cur.view);
        inner = this.execute(item.children, inner);
        src = inner.view;
      }
      if (item.kind === 'passThrough') this.tileOp(this.pipelines.mix, out, cur.view, src, { opacity: item.opacity, mask: item.mask });
      else this.tileOp(this.pipelines.blend, out, cur.view, src, { mode: item.modeIndex, opacity: item.opacity, mask: item.mask });
      this.release(cur);
      if (inner) this.release(inner);
      cur = out;
    }
    return cur;
  }

  private runPlan(plan: Plan): Target {
    const start = this.acquire();
    this.clearTarget(start);
    return this.execute(plan, start);
  }

  protected compositeDisplayTile(key: number, plan: Plan): void {
    if (this.lost) return;
    const result = this.runPlan(plan);
    let disp = this.display.get(key);
    if (!disp) {
      const tex = this.device.createTexture({
        size: [TILE_SIZE, TILE_SIZE],
        format: 'rgba8unorm',
        mipLevelCount: MIP_LEVELS,
        usage: GPUTextureUsage.RENDER_ATTACHMENT | GPUTextureUsage.TEXTURE_BINDING,
      });
      const levels = Array.from({ length: MIP_LEVELS }, (_, i) => tex.createView({ baseMipLevel: i, mipLevelCount: 1 }));
      disp = { tex, full: tex.createView(), levels };
      this.display.set(key, disp);
    }
    const [tx, ty] = tileCoords(key);
    const doc = this.doc!;
    const validMax: [number, number] = [Math.min(TILE_SIZE, doc.width - tx * TILE_SIZE) - 1, Math.min(TILE_SIZE, doc.height - ty * TILE_SIZE) - 1];
    this.tileOp(this.pipelines.premultiply, { tex: disp.tex, view: disp.levels[0] }, result.view, result.view, { validMax });
    this.release(result);
    for (let i = 1; i < MIP_LEVELS; i++) {
      const bind = this.device.createBindGroup({
        layout: this.pipelines.mip.getBindGroupLayout(0),
        entries: [{ binding: 0, resource: disp.levels[i - 1] }],
      });
      const pass = this.pass(disp.levels[i], false);
      pass.setPipeline(this.pipelines.mip);
      pass.setBindGroup(0, bind);
      pass.draw(3);
      pass.end();
    }
  }

  protected async compositeToBuffer(plan: Plan): Promise<Uint8ClampedArray> {
    if (this.lost) throw new Error('The GPU device was lost');
    this.flush();
    const result = this.runPlan(plan);
    const buffer = this.device.createBuffer({ size: TILE_BYTES, usage: GPUBufferUsage.COPY_DST | GPUBufferUsage.MAP_READ });
    this.currentFrame().encoder.copyTextureToBuffer({ texture: result.tex }, { buffer, bytesPerRow: TILE_SIZE * 4 }, [TILE_SIZE, TILE_SIZE]);
    this.release(result);
    this.flush();
    await buffer.mapAsync(GPUMapMode.READ);
    const out = new Uint8ClampedArray(buffer.getMappedRange().slice(0));
    buffer.destroy();
    return out;
  }

  protected releaseDisplayTile(key: number): void {
    const d = this.display.get(key);
    if (!d) return;
    d.tex.destroy();
    this.display.delete(key);
  }

  protected onResize(w: number, h: number): void {
    this.canvas.width = w;
    this.canvas.height = h;
  }

  protected draw(doc: Document | null, view: ViewState, keys: number[]): void {
    if (this.lost) return;
    const frame = this.currentFrame();
    const W = this.canvas.width;
    const H = this.canvas.height;
    const [wr, wg, wb] = this.colors.workspace;
    const pass = frame.encoder.beginRenderPass({
      colorAttachments: [
        { view: this.context.getCurrentTexture().createView(), loadOp: 'clear', storeOp: 'store', clearValue: [wr, wg, wb, 1] },
      ],
    });
    if (doc) {
      const r = deviceRect(doc, view, this.dpr);
      const quad = (rect: number[], uvScale: [number, number]) =>
        frame.uniform((v) => {
          const f = [...rect, W, H, ...uvScale, ...this.colors.checkerLight, 1, ...this.colors.checkerDark, 1, CHECKER_SIZE * this.dpr];
          f.forEach((x, i) => v.setFloat32(i * 4, x, true));
        }, 80);

      pass.setPipeline(this.pipelines.checker);
      pass.setBindGroup(
        0,
        this.device.createBindGroup({
          layout: this.pipelines.checker.getBindGroupLayout(0),
          entries: [{ binding: 0, resource: quad([r.x, r.y, r.width, r.height], [1, 1]) }],
        }),
      );
      pass.draw(6);

      pass.setPipeline(this.pipelines.tile);
      const sampler = view.zoom < 1 ? this.samplers.linear : this.samplers.nearest;
      for (const key of keys) {
        const disp = this.display.get(key);
        if (!disp) continue;
        const [tx, ty] = tileCoords(key);
        const w = Math.min(TILE_SIZE, doc.width - tx * TILE_SIZE);
        const h = Math.min(TILE_SIZE, doc.height - ty * TILE_SIZE);
        const bind = this.device.createBindGroup({
          layout: this.pipelines.tile.getBindGroupLayout(0),
          entries: [
            { binding: 0, resource: quad([r.x + tx * TILE_SIZE * r.sx, r.y + ty * TILE_SIZE * r.sy, w * r.sx, h * r.sy], [w / TILE_SIZE, h / TILE_SIZE]) },
            { binding: 1, resource: disp.full },
            { binding: 2, resource: sampler },
          ],
        });
        pass.setBindGroup(0, bind);
        pass.draw(6);
      }
    }
    pass.end();
  }

  protected override afterFrame(): void {
    this.flush();
    this.frameNo++;
    const max = Math.floor(this.layerCacheBudget / TILE_BYTES);
    if (this.frameNo % 60 === 0 || this.layerTextures.size > max) {
      const entries = [...this.layerTextures.entries()].sort((a, b) => a[1].lastUse - b[1].lastUse);
      let excess = this.layerTextures.size - max;
      for (const [tile, e] of entries) {
        if (excess <= 0 && e.lastUse >= this.frameNo - this.layerCacheMaxAge) break;
        if (e.lastUse >= this.frameNo - 1) break;
        e.tex.destroy();
        this.layerTextures.delete(tile);
        excess--;
      }
    }
    for (const [adj, e] of this.lutTextures) {
      if (e.lastUse < this.frameNo - this.layerCacheMaxAge) {
        e.tex.destroy();
        this.lutTextures.delete(adj);
      }
    }
    this.stats.cachedLayerTiles = this.layerTextures.size;
  }

  dispose(): void {
    this.disposed = true;
    for (const e of this.layerTextures.values()) e.tex.destroy();
    for (const e of this.lutTextures.values()) e.tex.destroy();
    this.lutTextures.clear();
    for (const d of this.display.values()) d.tex.destroy();
    for (const t of this.scratch) t.tex.destroy();
    this.layerTextures.clear();
    this.display.clear();
    this.scratch = [];
    this.device.destroy();
  }
}
