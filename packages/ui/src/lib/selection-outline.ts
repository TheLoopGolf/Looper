import { raster, type TileGrid } from '@canvas-ai/core';

/** Boundary of a selection mask as merged horizontal/vertical runs in document pixels: [x0, y0, x1, y1, …]. */
export function selectionOutline(mask: TileGrid, maxSegments = 200_000): Float32Array {
  const bounds = raster.gridBounds(mask);
  if (!bounds) return new Float32Array(0);
  const r = raster.readGrid(mask, { x: bounds.x - 1, y: bounds.y - 1, width: bounds.width + 2, height: bounds.height + 2 });
  const { width: w, height: h, data } = r;
  const inside = (x: number, y: number) => x >= 0 && y >= 0 && x < w && y < h && data[y * w + x] >= 128;
  const segs: number[] = [];
  // Horizontal edges between rows y-1 and y.
  for (let y = 1; y < h && segs.length < maxSegments * 4; y++) {
    let start = -1;
    for (let x = 0; x <= w; x++) {
      const edge = x < w && inside(x, y) !== inside(x, y - 1);
      if (edge && start < 0) start = x;
      if (!edge && start >= 0) {
        segs.push(r.x + start, r.y + y, r.x + x, r.y + y);
        start = -1;
      }
    }
  }
  // Vertical edges between columns x-1 and x.
  for (let x = 1; x < w && segs.length < maxSegments * 4; x++) {
    let start = -1;
    for (let y = 0; y <= h; y++) {
      const edge = y < h && inside(x, y) !== inside(x - 1, y);
      if (edge && start < 0) start = y;
      if (!edge && start >= 0) {
        segs.push(r.x + x, r.y + start, r.x + x, r.y + y);
        start = -1;
      }
    }
  }
  return Float32Array.from(segs);
}
