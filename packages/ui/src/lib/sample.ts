import { createDocument, createGroupLayer, createPixelLayer, gridFromPixels, type Document, type LayerNode } from '@canvas-ai/core';
import { newDocId } from './image-io';

/** Procedurally generated sample document (original artwork) for onboarding and demos. */
export function createSampleDocument(width = 1600, height = 1000): Document {
  const doc = createDocument({ id: newDocId(), name: 'Sample — Dusk', width, height });
  const paint = (f: (x: number, y: number) => [number, number, number, number] | null) => {
    const px = new Uint8ClampedArray(width * height * 4);
    for (let y = 0; y < height; y++) {
      for (let x = 0; x < width; x++) {
        const c = f(x, y);
        if (c) px.set(c, (y * width + x) * 4);
      }
    }
    return gridFromPixels(px, width, height);
  };
  const mix = (a: number, b: number, t: number) => Math.round(a + (b - a) * t);
  const layer = (id: string, name: string, tiles: ReturnType<typeof paint>, extra: Partial<LayerNode> = {}) =>
    ({ ...createPixelLayer(doc, `${id}_${doc.id}`, name, tiles), ...extra }) as LayerNode;

  const sky = layer('sky', 'Sky', paint((_x, y) => {
    const t = y / height;
    return [mix(24, 250, t), mix(28, 140, t), mix(72, 110, t), 255];
  }));
  const sunX = width * 0.68, sunY = height * 0.52, sunR = height * 0.16;
  const sun = layer('sun', 'Sun glow', paint((x, y) => {
    const d = Math.hypot(x - sunX, y - sunY) / sunR;
    if (d > 3) return null;
    const a = d < 1 ? 255 : Math.round(255 * Math.pow(1 - (d - 1) / 2, 2));
    return [255, 214, 150, a];
  }), { blendMode: 'screen' });
  const hill = (seed: number, base: number, amp: number, color: [number, number, number]) =>
    paint((x, y) => {
      const h = height * base + Math.sin(x / (140 + seed * 30) + seed) * amp + Math.sin(x / 57 + seed * 2) * amp * 0.25;
      return y > h ? [...color, 255] : null;
    });
  const farHills = layer('far', 'Far hills', hill(1, 0.62, 40, [92, 64, 110]), { opacity: 0.9 });
  const nearHills = layer('near', 'Near hills', hill(3, 0.74, 55, [38, 30, 58]));
  const foreground = { ...createGroupLayer(`fg_${doc.id}`, 'Landscape', [farHills, nearHills]), blendMode: 'passThrough' as const };
  const grade = layer('grade', 'Warm grade', paint((x, y) => [255, 120, 60, Math.round(60 + 40 * (x / width) * (1 - y / height))]), {
    blendMode: 'softLight',
    opacity: 0.7,
  });
  return { ...doc, layers: [sky, sun, foreground, grade] };
}
