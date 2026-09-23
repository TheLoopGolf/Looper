import { describe, expect, it } from 'vitest';
import { sameBytes } from './helpers';
import { blitImage, emptyGrid, fillRect, gridFromPixels, readRegion, TILE_SIZE, tileKey, tilesInRect } from '../src';

describe('tile grid', () => {
  it('computes intersecting tiles', () => {
    expect(tilesInRect(600, 300, { x: 250, y: 0, width: 20, height: 300 })).toEqual([
      tileKey(0, 0), tileKey(1, 0), tileKey(0, 1), tileKey(1, 1),
    ]);
    expect(tilesInRect(600, 300, { x: 700, y: 0, width: 10, height: 10 })).toEqual([]);
  });

  it('is copy-on-write: untouched tiles are shared, touched tiles are new', () => {
    const a = fillRect(emptyGrid(600, 600), { x: 0, y: 0, width: 600, height: 600 }, [10, 20, 30, 255]);
    expect(a.tiles.size).toBe(9);
    const b = fillRect(a, { x: 0, y: 0, width: 10, height: 10 }, [1, 2, 3, 4]);
    expect(b.tiles.get(tileKey(0, 0))).not.toBe(a.tiles.get(tileKey(0, 0)));
    expect(b.tiles.get(tileKey(1, 1))).toBe(a.tiles.get(tileKey(1, 1)));
    expect([...a.tiles.get(tileKey(0, 0))!.data.subarray(0, 4)]).toEqual([10, 20, 30, 255]);
    expect([...b.tiles.get(tileKey(0, 0))!.data.subarray(0, 4)]).toEqual([1, 2, 3, 4]);
  });

  it('drops tiles that become fully transparent', () => {
    const a = fillRect(emptyGrid(300, 300), { x: 0, y: 0, width: 300, height: 300 }, [1, 1, 1, 255]);
    const b = fillRect(a, { x: 0, y: 0, width: TILE_SIZE, height: TILE_SIZE }, [0, 0, 0, 0]);
    expect(b.tiles.has(tileKey(0, 0))).toBe(false);
    expect(b.tiles.size).toBe(3);
  });

  it('round-trips pixels through grids, including partial edge tiles', () => {
    const w = 300, h = 270;
    const px = new Uint8ClampedArray(w * h * 4);
    for (let i = 0; i < px.length; i++) px[i] = (i * 7) & 255;
    for (let i = 3; i < px.length; i += 4) px[i] = 255;
    const grid = gridFromPixels(px, w, h);
    expect(sameBytes(readRegion(grid, { x: 0, y: 0, width: w, height: h }), px)).toBe(true);
    const blitted = blitImage(emptyGrid(w, h), px, w, h, 0, 0);
    expect(sameBytes(readRegion(blitted, { x: 0, y: 0, width: w, height: h }), px)).toBe(true);
  });

  it('clips blits to the grid', () => {
    const img = new Uint8ClampedArray(4 * 4 * 4).fill(200);
    const g = blitImage(emptyGrid(10, 10), img, 4, 4, 8, -2);
    const region = readRegion(g, { x: 8, y: 0, width: 2, height: 2 });
    expect([...region]).toEqual(new Array(16).fill(200));
    expect(readRegion(g, { x: 0, y: 0, width: 8, height: 10 }).every((v) => v === 0)).toBe(true);
  });
});
