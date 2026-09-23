import { CNVA_MIME } from '@canvas-ai/core';
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
