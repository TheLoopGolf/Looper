export * from './cpu-renderer';
export * from './create-renderer';
export * from './dirty';
export * from './gl/webgl2-renderer';
export * from './renderer';
export * from './view';
export * from './webgpu/webgpu-renderer';

// Compositing semantics live in @canvas-ai/core; re-exported for existing imports.
export { blendBuffers, blendRGB, mixBuffers, adjustBuffers, buildTilePlan, documentTilePlan, executePlanCPU, renderDocumentCPU, compositeTileCPU, downscaleRGBA, renderThumbnailCPU } from '@canvas-ai/core';
export type { Plan, PlanItem, MaskRef } from '@canvas-ai/core';
