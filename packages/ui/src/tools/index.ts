import type { Tool as ToolId } from '../store';
import { brush, bucket, eraser, eyedropper, gradient } from './paint-tools';
import { ellipseMarquee, lasso, marquee, polyLasso, wand } from './select-tools';
import { shape, text } from './shape-text-tools';
import { cropTool, move } from './transform-tools';
import type { Tool } from './types';

/** Canvas tools by id (hand and zoom are handled by the canvas view itself). */
export const TOOLS: Partial<Record<ToolId, Tool>> = {
  move,
  marquee,
  ellipseMarquee,
  lasso,
  polyLasso,
  wand,
  crop: cropTool,
  eyedropper,
  brush,
  eraser,
  bucket,
  gradient,
  text,
  shape,
};

export * from './transform-tools';
export type { Tool, ToolContext, ToolPointer } from './types';
