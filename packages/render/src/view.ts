import { TILE_SIZE, tileCols, tileKey, tileRows, type Document } from '@canvas-ai/core';

/** Maps document pixels to canvas CSS pixels: screen = doc · zoom + pan. */
export interface ViewState {
  readonly zoom: number;
  readonly panX: number;
  readonly panY: number;
}

export const MIN_ZOOM = 1 / 64;
export const MAX_ZOOM = 64;

export function clampZoom(zoom: number): number {
  return Math.min(MAX_ZOOM, Math.max(MIN_ZOOM, zoom));
}

/** View that fits the whole document into a viewport with some padding, never upscaling past 100%. */
export function fitView(doc: Pick<Document, 'width' | 'height'>, viewportW: number, viewportH: number, padding = 32): ViewState {
  const zoom = clampZoom(Math.min(1, (viewportW - padding * 2) / doc.width, (viewportH - padding * 2) / doc.height));
  return { zoom, panX: (viewportW - doc.width * zoom) / 2, panY: (viewportH - doc.height * zoom) / 2 };
}

/** Zooms around a fixed screen point (e.g. the cursor). */
export function zoomAt(view: ViewState, nextZoom: number, screenX: number, screenY: number): ViewState {
  const zoom = clampZoom(nextZoom);
  const docX = (screenX - view.panX) / view.zoom;
  const docY = (screenY - view.panY) / view.zoom;
  return { zoom, panX: screenX - docX * zoom, panY: screenY - docY * zoom };
}

export function screenToDoc(view: ViewState, x: number, y: number): [number, number] {
  return [(x - view.panX) / view.zoom, (y - view.panY) / view.zoom];
}

/** Tile keys of the document visible in a viewport of the given CSS size. */
export function visibleTileKeys(doc: Pick<Document, 'width' | 'height'>, view: ViewState, viewportW: number, viewportH: number): number[] {
  const [x0, y0] = screenToDoc(view, 0, 0);
  const [x1, y1] = screenToDoc(view, viewportW, viewportH);
  const tx0 = Math.max(0, Math.floor(x0 / TILE_SIZE));
  const ty0 = Math.max(0, Math.floor(y0 / TILE_SIZE));
  const tx1 = Math.min(tileCols(doc.width) - 1, Math.floor(x1 / TILE_SIZE));
  const ty1 = Math.min(tileRows(doc.height) - 1, Math.floor(y1 / TILE_SIZE));
  const keys: number[] = [];
  for (let ty = ty0; ty <= ty1; ty++) for (let tx = tx0; tx <= tx1; tx++) keys.push(tileKey(tx, ty));
  return keys;
}

/** Standard zoom steps used by zoom in/out shortcuts. */
export const ZOOM_STEPS = [
  1 / 64, 1 / 48, 1 / 32, 1 / 24, 1 / 16, 1 / 12, 1 / 8, 1 / 6, 1 / 4, 1 / 3, 1 / 2, 2 / 3, 1, 1.5, 2, 3, 4, 6, 8, 12, 16, 24, 32, 48, 64,
];

export function nextZoomStep(zoom: number, dir: 1 | -1): number {
  if (dir > 0) return ZOOM_STEPS.find((z) => z > zoom * 1.001) ?? MAX_ZOOM;
  return [...ZOOM_STEPS].reverse().find((z) => z < zoom / 1.001) ?? MIN_ZOOM;
}
