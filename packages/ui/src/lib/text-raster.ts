import { setTextRasterizer, type TextLayer } from '@canvas-ai/core';

type Ctx2D = OffscreenCanvasRenderingContext2D;

function fontString(l: TextLayer) {
  const s = l.style;
  const family = /[\s,]/.test(s.fontFamily) && !/^["']/.test(s.fontFamily) && !s.fontFamily.includes(',') ? `"${s.fontFamily}"` : s.fontFamily;
  return `${s.italic ? 'italic ' : ''}${s.fontWeight} ${s.fontSize}px ${family}`;
}

function wrap(g: Ctx2D, text: string, width: number | null): string[] {
  const lines: string[] = [];
  for (const para of text.split('\n')) {
    if (!width) {
      lines.push(para);
      continue;
    }
    let line = '';
    for (const word of para.split(/(\s+)/)) {
      const next = line + word;
      if (line && g.measureText(next).width > width) {
        lines.push(line.trimEnd());
        line = word.trimStart();
      } else line = next;
    }
    lines.push(line);
  }
  return lines;
}

/** Text rendering through the browser's font engine (kerning from the font, tracking via letterSpacing). */
export function installTextRasterizer(): void {
  if (typeof OffscreenCanvas === 'undefined') return;
  const measure = new OffscreenCanvas(1, 1).getContext('2d')!;
  setTextRasterizer((layer, docW, docH) => {
    const s = layer.style;
    measure.font = fontString(layer);
    (measure as unknown as { letterSpacing: string }).letterSpacing = `${s.letterSpacing}px`;
    const lines = wrap(measure, layer.text, layer.boxWidth);
    const metrics = measure.measureText('Mg');
    const ascent = metrics.fontBoundingBoxAscent ?? s.fontSize * 0.8;
    const descent = metrics.fontBoundingBoxDescent ?? s.fontSize * 0.2;
    const lh = s.fontSize * s.lineHeight;
    const widths = lines.map((l) => measure.measureText(l).width);
    const boxW = layer.boxWidth ?? Math.max(1, ...widths);
    const pad = Math.ceil(s.fontSize * 0.25);
    const x0 = Math.floor(layer.x) - pad, y0 = Math.floor(layer.y) - pad;
    const w = Math.ceil(boxW + pad * 2 + Math.abs(s.letterSpacing) * 2);
    const h = Math.ceil((lines.length - 1) * lh + ascent + descent + pad * 2);
    // Clip to the document.
    const cx0 = Math.max(0, x0), cy0 = Math.max(0, y0);
    const cx1 = Math.min(docW, x0 + w), cy1 = Math.min(docH, y0 + h);
    if (cx1 <= cx0 || cy1 <= cy0 || w > 30000 || h > 30000) return null;
    const canvas = new OffscreenCanvas(w, h);
    const g = canvas.getContext('2d')!;
    g.font = fontString(layer);
    (g as unknown as { letterSpacing: string }).letterSpacing = `${s.letterSpacing}px`;
    g.fontKerning = 'normal';
    g.textBaseline = 'alphabetic';
    g.fillStyle = `rgba(${s.color[0]},${s.color[1]},${s.color[2]},${s.color[3] / 255})`;
    const fx = layer.x - x0, fy = layer.y - y0;
    lines.forEach((line, i) => {
      const lw = widths[i];
      const ox = s.align === 'center' ? (boxW - lw) / 2 : s.align === 'right' ? boxW - lw : 0;
      g.fillText(line, fx + ox, fy + ascent + i * lh);
    });
    const img = g.getImageData(cx0 - x0, cy0 - y0, cx1 - cx0, cy1 - cy0);
    return { x: cx0, y: cy0, width: cx1 - cx0, height: cy1 - cy0, channels: 4, data: img.data };
  });
}
