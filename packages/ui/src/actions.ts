import { ADJUSTMENTS, CNVA_MIME, FILTERS, findLayer, type JSONSchema } from '@canvas-ai/core';
import { toolContext } from './components/CanvasView';
import { beginTransform, commitCrop } from './tools/transform-tools';
import { fitView, nextZoomStep, renderDocumentCPU, zoomAt } from '@canvas-ai/render';
import type { FileFilter, Platform } from './platform';
import { webPlatform } from './platform';
import { baseName, documentFromCnva, documentFromImage, encodeImage, IMAGE_EXTENSIONS, saveCnva, type ExportFormat } from './lib/image-io';
import { activeTab, useEditor } from './store';

/** App-level actions shared by menus, shortcuts and (later) agents' UI tools. */

let platform: Platform = webPlatform;
export const setPlatform = (p: Platform) => {
  platform = p;
};
export const getPlatform = () => platform;

const CNVA_FILTER: FileFilter = { name: 'Canvas AI Project', extensions: ['cnva'] };
const IMAGE_FILTER: FileFilter = { name: 'Images', extensions: IMAGE_EXTENSIONS };

const editor = () => useEditor.getState();

async function withErrors(label: string, fn: () => Promise<void>): Promise<void> {
  try {
    await fn();
  } catch (e) {
    console.error(e);
    editor().notify('error', `${label} failed: ${(e as Error).message}`);
  }
}

/** Opens bytes as a project or image, picking the format from the extension. */
export async function openBytes(name: string, bytes: Uint8Array, handle?: unknown): Promise<void> {
  const isProject = name.toLowerCase().endsWith('.cnva');
  const doc = isProject ? documentFromCnva(bytes) : await documentFromImage(bytes, name);
  editor().openDocument(doc, isProject ? { name, handle } : undefined);
}

export function openFile(): Promise<void> {
  return withErrors('Open', async () => {
    const file = await platform.openFile([{ name: 'All supported', extensions: ['cnva', ...IMAGE_EXTENSIONS] }, CNVA_FILTER, IMAGE_FILTER]);
    if (file) await openBytes(file.name, file.bytes, file.handle);
  });
}

export function saveDocument(saveAs = false): Promise<void> {
  return withErrors('Save', async () => {
    const tab = activeTab(editor());
    if (!tab) return;
    const bytes = await saveCnva(tab.bus, editor().renderer);
    const name = tab.fileName ?? `${tab.doc.name}.cnva`;
    const target = await platform.saveFile(bytes, { name, handle: saveAs ? undefined : tab.fileHandle }, [CNVA_FILTER]);
    if (target) {
      editor().markSaved(tab.id, target);
      editor().notify('info', `Saved ${target.name}`);
    }
  });
}

export function openExportDialog(): void {
  if (activeTab(editor())) editor().setExportOpen(true);
}

export function exportImage(format: ExportFormat, quality = 0.92): Promise<void> {
  return withErrors('Export', async () => {
    const tab = activeTab(editor());
    if (!tab) return;
    const doc = tab.doc;
    const renderer = editor().renderer;
    const pixels = renderer ? await renderer.readComposite() : renderDocumentCPU(doc);
    const bytes = await encodeImage(pixels, doc.width, doc.height, format, quality);
    const ext = format === 'jpeg' ? 'jpg' : format;
    const name = `${baseName(tab.fileName ?? tab.doc.name)}.${ext}`;
    const target = await platform.saveFile(bytes, { name }, [{ name: format.toUpperCase(), extensions: [ext] }]);
    if (target) editor().notify('info', `Exported ${target.name}`);
  });
}

export function fitToScreen(): void {
  const s = editor();
  const tab = activeTab(s);
  if (tab) s.setView(tab.id, fitView(tab.doc, s.viewport.width, s.viewport.height));
}

export function actualPixels(): void {
  const s = editor();
  const tab = activeTab(s);
  if (!tab) return;
  const { width, height } = s.viewport;
  s.setView(tab.id, { zoom: 1, panX: Math.round((width - tab.doc.width) / 2), panY: Math.round((height - tab.doc.height) / 2) });
}

export function zoomStep(dir: 1 | -1): void {
  const s = editor();
  const tab = activeTab(s);
  const view = tab && s.views[tab.id];
  if (!tab || !view) return;
  s.setView(tab.id, zoomAt(view, nextZoomStep(view.zoom, dir), s.viewport.width / 2, s.viewport.height / 2));
}

export { CNVA_MIME };

// ---------------------------------------------------------------------------
// M2 actions: dialogs, transforms, masks, adjustments
// ---------------------------------------------------------------------------


const stateTab = () => activeTab(editor());

/** Active layer id if it is a pixel layer (for filters/direct adjustments), else a toast. */
function activePixelLayer(action: string): string | null {
  const tab = stateTab();
  const layer = tab?.activeLayerId ? findLayer(tab.doc, tab.activeLayerId)?.layer : null;
  if (!layer) {
    editor().notify('error', `Select a layer to ${action}`);
    return null;
  }
  if (layer.type !== 'pixel' && layer.type !== 'ai') {
    editor().notify('error', `Cannot ${action}: "${layer.name}" is not a pixel layer${layer.type === 'text' || layer.type === 'shape' ? ' (rasterize it first)' : ''}`);
    return null;
  }
  return layer.id;
}

function commandFields(commandId: string, omit: string[] = []): Record<string, JSONSchema> {
  const schema = editor().registry.get(commandId)?.schema;
  const props = { ...(schema?.properties ?? {}) } as Record<string, JSONSchema>;
  for (const k of omit) delete props[k];
  return props;
}

export function openFilter(kind: keyof typeof FILTERS): void {
  const layerId = activePixelLayer(`apply ${FILTERS[kind].label}`);
  if (!layerId) return;
  editor().openParamsDialog({
    title: FILTERS[kind].label,
    commandId: `filter.${kind}`,
    fields: FILTERS[kind].schema as Record<string, JSONSchema>,
    initial: { ...FILTERS[kind].defaults },
    build: (v) => ({ ...v, layerId }),
    preview: true,
  });
}

export function openDirectAdjustment(kind: keyof typeof ADJUSTMENTS): void {
  const layerId = activePixelLayer(`adjust`);
  if (!layerId) return;
  const def = ADJUSTMENTS[kind];
  const fields = def.schema as Record<string, JSONSchema>;
  if (!Object.keys(fields).length) {
    editor().run('adjust.apply', { layerId, kind });
    return;
  }
  editor().openParamsDialog({
    title: def.label,
    commandId: 'adjust.apply',
    fields,
    initial: { ...def.defaults },
    build: (v) => ({ layerId, kind, params: v }),
    preview: true,
  });
}

export function newAdjustmentLayer(kind: string): void {
  const tab = stateTab();
  if (!tab) return;
  editor().run('layer.createAdjustment', { kind, ...(tab.activeLayerId ? { above: tab.activeLayerId } : {}) });
}

export function openCommandDialog(commandId: string, title: string, initial: Record<string, unknown> = {}, omit: string[] = [], preview = false): void {
  if (!stateTab()) return;
  editor().openParamsDialog({ title, commandId, fields: commandFields(commandId, omit), initial, build: (v) => v, preview });
}

export function imageSizeDialog(): void {
  const tab = stateTab();
  if (tab) openCommandDialog('document.resizeImage', 'Image Size', { width: tab.doc.width, height: tab.doc.height });
}

export function canvasSizeDialog(): void {
  const tab = stateTab();
  if (tab) openCommandDialog('document.resizeCanvas', 'Canvas Size', { width: tab.doc.width, height: tab.doc.height, anchor: 'c' });
}

export function colorRangeDialog(): void {
  const fg = editor().colors.fg;
  editor().openParamsDialog({
    title: 'Color Range',
    commandId: 'selection.colorRange',
    fields: commandFields('selection.colorRange', ['layerId']),
    initial: { color: fg, fuzziness: 40, sampleMerged: true, mode: 'replace' },
    build: (v) => v,
    preview: true,
  });
}

export function freeTransform(): void {
  const tab = stateTab();
  if (!tab) return;
  editor().setTool('move');
  if (beginTransform(toolContext(tab))) editor().notify('info', 'Drag handles to scale, outside to rotate. Enter applies, Esc cancels.');
}

export function applyCrop(): void {
  const tab = stateTab();
  if (tab) commitCrop(toolContext(tab));
}

export function activeLayerRun(commandId: string, extra: Record<string, unknown> = {}): void {
  const tab = stateTab();
  if (!tab?.activeLayerId) {
    editor().notify('error', 'Select a layer first');
    return;
  }
  editor().run(commandId, { layerId: tab.activeLayerId, ...extra });
}

export function fillSelection(which: 'fg' | 'bg'): void {
  const id = activePixelLayer('fill');
  if (id) editor().run('pixels.fill', { layerId: id, color: editor().colors[which] });
}
