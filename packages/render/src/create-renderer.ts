import { CpuRenderer } from './cpu-renderer';
import { WebGL2Renderer } from './gl/webgl2-renderer';
import type { RenderBackend, Renderer } from './renderer';
import { WebGPURenderer } from './webgpu/webgpu-renderer';

/**
 * Creates the best available renderer: WebGPU → WebGL2 → CPU. A canvas can
 * only ever hold one context type, so each attempt that fails before
 * acquiring a context falls through to the next one on the same canvas;
 * the caller should pass a fresh canvas when forcing a different backend.
 */
export async function createRenderer(canvas: HTMLCanvasElement, prefer: RenderBackend | 'auto' = 'auto'): Promise<Renderer> {
  const order: RenderBackend[] = prefer === 'auto' ? ['webgpu', 'webgl2', 'cpu'] : [prefer, 'webgl2', 'cpu'];
  const errors: string[] = [];
  for (const backend of [...new Set(order)]) {
    try {
      if (backend === 'webgpu') return await WebGPURenderer.create(canvas);
      if (backend === 'webgl2') return new WebGL2Renderer(canvas);
      return new CpuRenderer(canvas);
    } catch (e) {
      errors.push(`${backend}: ${(e as Error).message}`);
    }
  }
  throw new Error(`No renderer available (${errors.join('; ')})`);
}
