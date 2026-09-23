import { TILE_SIZE, tileCoords, type Document, type Tile } from '@canvas-ai/core';
import type { Plan } from '../plan';
import { CHECKER_SIZE, deviceRect, TiledRenderer, type RenderBackend } from '../renderer';
import type { ViewState } from '../view';
import { BLEND_FS, CHECKER_FS, COPY_FS, MIX_FS, PREMULTIPLY_FS, PRESENT_FS, QUAD_VS, TILE_VS } from './shaders';

interface Program {
  program: WebGLProgram;
  u: Record<string, WebGLUniformLocation | null>;
}

interface Target {
  tex: WebGLTexture;
  fbo: WebGLFramebuffer;
}

const TILE_BYTES = TILE_SIZE * TILE_SIZE * 4;
const MIP_LEVELS = Math.log2(TILE_SIZE) + 1;

/** WebGL2 backend: RGBA8 ping-pong compositing per tile, mipmapped premultiplied display tiles. */
export class WebGL2Renderer extends TiledRenderer {
  readonly backend: RenderBackend = 'webgl2';
  /** Max bytes of cached layer-tile textures before LRU eviction. */
  layerCacheBudget = 256 * 1024 * 1024;
  /** Layer textures unused for this many frames are released even under budget. */
  layerCacheMaxAge = 600;
  private gl: WebGL2RenderingContext;
  private programs!: Record<'blend' | 'mix' | 'premultiply' | 'copy' | 'present' | 'checker', Program>;
  private vao!: WebGLVertexArrayObject;
  private scratch: Target[] = [];
  private layerTextures = new Map<Tile, { tex: WebGLTexture; lastUse: number }>();
  private display = new Map<number, Target>();
  private frame = 0;
  private lost = false;

  constructor(private canvas: HTMLCanvasElement) {
    super();
    this.stats.backend = 'webgl2';
    const gl = canvas.getContext('webgl2', { alpha: false, antialias: false, premultipliedAlpha: true, preserveDrawingBuffer: true });
    if (!gl) throw new Error('WebGL2 is not available');
    this.gl = gl;
    canvas.addEventListener('webglcontextlost', this.handleContextLost);
    canvas.addEventListener('webglcontextrestored', this.handleContextRestored);
    this.init();
  }

  private handleContextLost = (e: Event) => {
    e.preventDefault();
    this.lost = true;
  };

  private handleContextRestored = () => {
    this.lost = false;
    this.layerTextures.clear();
    this.display.clear();
    this.scratch = [];
    this.init();
    this.invalidateAll();
  };

  private init(): void {
    const gl = this.gl;
    const make = (vs: string, fs: string, uniforms: string[]): Program => {
      const program = gl.createProgram()!;
      for (const [type, src] of [[gl.VERTEX_SHADER, vs], [gl.FRAGMENT_SHADER, fs]] as const) {
        const s = gl.createShader(type)!;
        gl.shaderSource(s, src);
        gl.compileShader(s);
        if (!gl.getShaderParameter(s, gl.COMPILE_STATUS)) throw new Error(`Shader compile failed: ${gl.getShaderInfoLog(s)}`);
        gl.attachShader(program, s);
      }
      gl.linkProgram(program);
      if (!gl.getProgramParameter(program, gl.LINK_STATUS)) throw new Error(`Program link failed: ${gl.getProgramInfoLog(program)}`);
      const u: Program['u'] = {};
      for (const name of uniforms) u[name] = gl.getUniformLocation(program, name);
      return { program, u };
    };
    this.programs = {
      blend: make(TILE_VS, BLEND_FS, ['u_backdrop', 'u_src', 'u_mode', 'u_opacity']),
      mix: make(TILE_VS, MIX_FS, ['u_backdrop', 'u_src', 'u_opacity']),
      premultiply: make(TILE_VS, PREMULTIPLY_FS, ['u_src', 'u_validMax']),
      copy: make(TILE_VS, COPY_FS, ['u_src']),
      present: make(QUAD_VS, PRESENT_FS, ['u_rect', 'u_viewport', 'u_uvScale', 'u_tex']),
      checker: make(QUAD_VS, CHECKER_FS, ['u_rect', 'u_viewport', 'u_uvScale', 'u_light', 'u_dark', 'u_size']),
    };
    this.vao = gl.createVertexArray()!;
    gl.pixelStorei(gl.UNPACK_COLORSPACE_CONVERSION_WEBGL, gl.NONE);
    gl.pixelStorei(gl.UNPACK_PREMULTIPLY_ALPHA_WEBGL, false);
  }

  private createTexture(mips = false): WebGLTexture {
    const gl = this.gl;
    const tex = gl.createTexture()!;
    gl.bindTexture(gl.TEXTURE_2D, tex);
    gl.texStorage2D(gl.TEXTURE_2D, mips ? MIP_LEVELS : 1, gl.RGBA8, TILE_SIZE, TILE_SIZE);
    gl.texParameteri(gl.TEXTURE_2D, gl.TEXTURE_MIN_FILTER, gl.NEAREST);
    gl.texParameteri(gl.TEXTURE_2D, gl.TEXTURE_MAG_FILTER, gl.NEAREST);
    gl.texParameteri(gl.TEXTURE_2D, gl.TEXTURE_WRAP_S, gl.CLAMP_TO_EDGE);
    gl.texParameteri(gl.TEXTURE_2D, gl.TEXTURE_WRAP_T, gl.CLAMP_TO_EDGE);
    return tex;
  }

  private createTarget(mips = false): Target {
    const gl = this.gl;
    const tex = this.createTexture(mips);
    const fbo = gl.createFramebuffer()!;
    gl.bindFramebuffer(gl.FRAMEBUFFER, fbo);
    gl.framebufferTexture2D(gl.FRAMEBUFFER, gl.COLOR_ATTACHMENT0, gl.TEXTURE_2D, tex, 0);
    return { tex, fbo };
  }

  private acquire(): Target {
    return this.scratch.pop() ?? this.createTarget();
  }

  private release(t: Target): void {
    this.scratch.push(t);
  }

  private layerTexture(tile: Tile): WebGLTexture {
    let entry = this.layerTextures.get(tile);
    if (!entry) {
      const gl = this.gl;
      const tex = this.createTexture();
      gl.texSubImage2D(gl.TEXTURE_2D, 0, 0, 0, TILE_SIZE, TILE_SIZE, gl.RGBA, gl.UNSIGNED_BYTE, new Uint8Array(tile.data.buffer, tile.data.byteOffset, TILE_BYTES));
      entry = { tex, lastUse: this.frame };
      this.layerTextures.set(tile, entry);
    }
    entry.lastUse = this.frame;
    return entry.tex;
  }

  private evictLayerTextures(): void {
    const max = Math.floor(this.layerCacheBudget / TILE_BYTES);
    if (this.frame % 60 !== 0 && this.layerTextures.size <= max) return;
    const entries = [...this.layerTextures.entries()].sort((a, b) => a[1].lastUse - b[1].lastUse);
    let excess = this.layerTextures.size - max;
    for (const [tile, e] of entries) {
      if (excess <= 0 && e.lastUse >= this.frame - this.layerCacheMaxAge) break;
      if (e.lastUse >= this.frame - 1) break; // used by the current frame
      this.gl.deleteTexture(e.tex);
      this.layerTextures.delete(tile);
      excess--;
    }
  }

  private tilePass(program: Program, target: Target, textures: [string, WebGLTexture][]): void {
    const gl = this.gl;
    gl.bindFramebuffer(gl.FRAMEBUFFER, target.fbo);
    gl.viewport(0, 0, TILE_SIZE, TILE_SIZE);
    gl.useProgram(program.program);
    textures.forEach(([name, tex], i) => {
      gl.activeTexture(gl.TEXTURE0 + i);
      gl.bindTexture(gl.TEXTURE_2D, tex);
      gl.uniform1i(program.u[name], i);
    });
    gl.drawArrays(gl.TRIANGLES, 0, 3);
  }

  private clear(target: Target): void {
    const gl = this.gl;
    gl.bindFramebuffer(gl.FRAMEBUFFER, target.fbo);
    gl.clearColor(0, 0, 0, 0);
    gl.clear(gl.COLOR_BUFFER_BIT);
  }

  /** Runs a plan on top of `cur` and returns the target holding the result (ownership transfers). */
  private execute(plan: Plan, cur: Target): Target {
    const gl = this.gl;
    const { blend, mix, copy } = this.programs;
    for (const item of plan) {
      if (item.kind === 'passThrough' && item.opacity >= 1) {
        cur = this.execute(item.children, cur);
        continue;
      }
      let src: WebGLTexture;
      let inner: Target | null = null;
      if (item.kind === 'layer') {
        src = this.layerTexture(item.tile);
      } else if (item.kind === 'isolated') {
        inner = this.acquire();
        this.clear(inner);
        inner = this.execute(item.children, inner);
        src = inner.tex;
      } else {
        inner = this.acquire();
        gl.useProgram(copy.program);
        this.tilePass(copy, inner, [['u_src', cur.tex]]);
        inner = this.execute(item.children, inner);
        src = inner.tex;
      }
      const out = this.acquire();
      const program = item.kind === 'passThrough' ? mix : blend;
      gl.useProgram(program.program);
      if (item.kind !== 'passThrough') gl.uniform1i(blend.u.u_mode, item.modeIndex);
      gl.uniform1f(program.u.u_opacity, item.opacity);
      this.tilePass(program, out, [['u_backdrop', cur.tex], ['u_src', src]]);
      this.release(cur);
      if (inner) this.release(inner);
      cur = out;
    }
    return cur;
  }

  private runPlan(plan: Plan): Target {
    const gl = this.gl;
    gl.disable(gl.BLEND);
    gl.bindVertexArray(this.vao);
    const start = this.acquire();
    this.clear(start);
    return this.execute(plan, start);
  }

  protected compositeDisplayTile(key: number, plan: Plan): void {
    if (this.lost) return;
    const gl = this.gl;
    const result = this.runPlan(plan);
    let disp = this.display.get(key);
    if (!disp) {
      disp = this.createTarget(true);
      this.display.set(key, disp);
    }
    const [tx, ty] = tileCoords(key);
    const doc = this.doc!;
    gl.useProgram(this.programs.premultiply.program);
    gl.uniform2i(this.programs.premultiply.u.u_validMax, Math.min(TILE_SIZE, doc.width - tx * TILE_SIZE) - 1, Math.min(TILE_SIZE, doc.height - ty * TILE_SIZE) - 1);
    this.tilePass(this.programs.premultiply, disp, [['u_src', result.tex]]);
    this.release(result);
    gl.bindTexture(gl.TEXTURE_2D, disp.tex);
    gl.generateMipmap(gl.TEXTURE_2D);
  }

  protected async compositeToBuffer(plan: Plan): Promise<Uint8ClampedArray> {
    if (this.lost) throw new Error('The WebGL context was lost');
    const gl = this.gl;
    const result = this.runPlan(plan);
    const out = new Uint8ClampedArray(TILE_BYTES);
    gl.bindFramebuffer(gl.FRAMEBUFFER, result.fbo);
    gl.readPixels(0, 0, TILE_SIZE, TILE_SIZE, gl.RGBA, gl.UNSIGNED_BYTE, out);
    this.release(result);
    return out;
  }

  protected releaseDisplayTile(key: number): void {
    const t = this.display.get(key);
    if (!t) return;
    this.gl.deleteTexture(t.tex);
    this.gl.deleteFramebuffer(t.fbo);
    this.display.delete(key);
  }

  protected onResize(w: number, h: number): void {
    this.canvas.width = w;
    this.canvas.height = h;
  }

  protected draw(doc: Document | null, view: ViewState, keys: number[]): void {
    if (this.lost) return;
    const gl = this.gl;
    const { workspace, checkerLight, checkerDark } = this.colors;
    const W = this.canvas.width;
    const H = this.canvas.height;
    gl.bindFramebuffer(gl.FRAMEBUFFER, null);
    gl.viewport(0, 0, W, H);
    gl.clearColor(workspace[0], workspace[1], workspace[2], 1);
    gl.clear(gl.COLOR_BUFFER_BIT);
    if (!doc) return;
    gl.bindVertexArray(this.vao);
    const r = deviceRect(doc, view, this.dpr);

    const { checker, present } = this.programs;
    gl.disable(gl.BLEND);
    gl.useProgram(checker.program);
    gl.uniform4f(checker.u.u_rect, r.x, r.y, r.width, r.height);
    gl.uniform2f(checker.u.u_viewport, W, H);
    gl.uniform2f(checker.u.u_uvScale, 1, 1);
    gl.uniform3f(checker.u.u_light, ...checkerLight);
    gl.uniform3f(checker.u.u_dark, ...checkerDark);
    gl.uniform1f(checker.u.u_size, CHECKER_SIZE * this.dpr);
    gl.drawArrays(gl.TRIANGLES, 0, 6);

    gl.enable(gl.BLEND);
    gl.blendFunc(gl.ONE, gl.ONE_MINUS_SRC_ALPHA);
    gl.useProgram(present.program);
    gl.uniform2f(present.u.u_viewport, W, H);
    gl.uniform1i(present.u.u_tex, 0);
    gl.activeTexture(gl.TEXTURE0);
    const minFilter = view.zoom < 1 ? gl.LINEAR_MIPMAP_LINEAR : gl.NEAREST;
    const magFilter = view.zoom < 1 ? gl.LINEAR : gl.NEAREST;
    for (const key of keys) {
      const disp = this.display.get(key);
      if (!disp) continue;
      const [tx, ty] = tileCoords(key);
      const w = Math.min(TILE_SIZE, doc.width - tx * TILE_SIZE);
      const h = Math.min(TILE_SIZE, doc.height - ty * TILE_SIZE);
      gl.bindTexture(gl.TEXTURE_2D, disp.tex);
      gl.texParameteri(gl.TEXTURE_2D, gl.TEXTURE_MIN_FILTER, minFilter);
      gl.texParameteri(gl.TEXTURE_2D, gl.TEXTURE_MAG_FILTER, magFilter);
      gl.uniform4f(present.u.u_rect, r.x + tx * TILE_SIZE * r.sx, r.y + ty * TILE_SIZE * r.sy, w * r.sx, h * r.sy);
      gl.uniform2f(present.u.u_uvScale, w / TILE_SIZE, h / TILE_SIZE);
      gl.drawArrays(gl.TRIANGLES, 0, 6);
    }
    gl.disable(gl.BLEND);
  }

  protected override afterFrame(): void {
    this.frame++;
    this.evictLayerTextures();
    this.stats.cachedLayerTiles = this.layerTextures.size;
  }

  dispose(): void {
    const gl = this.gl;
    this.canvas.removeEventListener('webglcontextlost', this.handleContextLost);
    this.canvas.removeEventListener('webglcontextrestored', this.handleContextRestored);
    for (const { tex } of this.layerTextures.values()) gl.deleteTexture(tex);
    for (const t of [...this.display.values(), ...this.scratch]) {
      gl.deleteTexture(t.tex);
      gl.deleteFramebuffer(t.fbo);
    }
    this.layerTextures.clear();
    this.display.clear();
    this.scratch = [];
  }
}
