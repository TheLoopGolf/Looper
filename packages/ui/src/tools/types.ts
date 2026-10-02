import type { Document } from '@canvas-ai/core';
import type { ViewState } from '@canvas-ai/render';
import type { DocTab, EditorState } from '../store';

export interface ToolPointer {
  /** Document coordinates (fractional). */
  readonly x: number;
  readonly y: number;
  /** Canvas CSS coordinates. */
  readonly sx: number;
  readonly sy: number;
  /** 0..1; mouse reports 1. */
  readonly pressure: number;
  readonly shift: boolean;
  readonly alt: boolean;
  readonly mod: boolean;
  readonly button: number;
  readonly detail: number;
}

export interface ToolContext {
  readonly tab: DocTab;
  /** Current document (ignores previews). */
  readonly doc: Document;
  readonly view: ViewState;
  readonly state: EditorState;
  /** Dispatches on the active document (shows a toast and returns undefined on error). */
  run<R = unknown>(commandId: string, params?: Record<string, unknown>): R | undefined;
  setPreview(doc: Document | null): void;
  /** Redraws the overlay canvas. */
  invalidate(): void;
  toScreen(x: number, y: number): [number, number];
}

export interface Tool {
  readonly cursor: (ctx: ToolContext) => string;
  down?(ctx: ToolContext, p: ToolPointer): void;
  move?(ctx: ToolContext, p: ToolPointer, events: readonly ToolPointer[]): void;
  up?(ctx: ToolContext, p: ToolPointer): void;
  hover?(ctx: ToolContext, p: ToolPointer): void;
  /** Return true if handled. */
  key?(ctx: ToolContext, e: KeyboardEvent): boolean;
  overlay?(ctx: ToolContext, g: CanvasRenderingContext2D): void;
  /** Called when switching away (abort in-progress interactions). */
  cancel?(ctx: ToolContext): void;
  /** True while an interaction is in progress (blocks undo, tab switches…). */
  busy?(): boolean;
}

/** Selection combine mode from modifier keys held at mouse-down (Shift add, Alt subtract, both intersect). */
export function modeFromModifiers(p: Pick<ToolPointer, 'shift' | 'alt'>, fallback: 'replace' | 'add' | 'subtract' | 'intersect') {
  if (p.shift && p.alt) return 'intersect';
  if (p.shift) return 'add';
  if (p.alt) return 'subtract';
  return fallback;
}

export function dashedPath(g: CanvasRenderingContext2D, draw: () => void, offset = 0): void {
  g.save();
  g.lineWidth = 1;
  g.strokeStyle = '#fff';
  g.setLineDash([]);
  g.beginPath();
  draw();
  g.stroke();
  g.strokeStyle = '#000';
  g.setLineDash([4, 4]);
  g.lineDashOffset = -offset;
  g.beginPath();
  draw();
  g.stroke();
  g.restore();
}

export const CROSSHAIR = 'crosshair';
