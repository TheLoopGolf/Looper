import { useEffect } from 'react';
import { actualPixels, fillSelection, fitToScreen, freeTransform, openExportDialog, openFile, saveDocument, zoomStep } from './actions';
import { activeTab, useEditor, type Tool } from './store';
import { findLayer } from '@canvas-ai/core';

export interface Shortcut {
  /** e.g. "mod+shift+z"; "mod" is Cmd on macOS and Ctrl elsewhere. */
  keys: string;
  label: string;
  run: () => void;
}

const isMac = typeof navigator !== 'undefined' && /Mac|iPhone|iPad/.test(navigator.platform);

export function formatKeys(keys: string): string {
  return keys
    .split('+')
    .map((k) => {
      if (k === 'mod') return isMac ? '⌘' : 'Ctrl';
      if (k === 'shift') return isMac ? '⇧' : 'Shift';
      if (k === 'alt') return isMac ? '⌥' : 'Alt';
      if (k === 'backspace') return isMac ? '⌫' : 'Backspace';
      return k.length === 1 ? k.toUpperCase() : k[0].toUpperCase() + k.slice(1);
    })
    .join(isMac ? '' : '+');
}

function activeLayerCommand(commandId: string, params: (layerId: string) => Record<string, unknown>) {
  return () => {
    const s = useEditor.getState();
    const tab = activeTab(s);
    if (tab?.activeLayerId) s.run(commandId, params(tab.activeLayerId));
  };
}

/** Delete clears selected pixels when there is a selection on a pixel layer, otherwise deletes the layer. */
function deleteOrClear() {
  const s = useEditor.getState();
  const tab = activeTab(s);
  if (!tab?.activeLayerId) return;
  const layer = findLayer(tab.doc, tab.activeLayerId)?.layer;
  if (tab.doc.selection && (layer?.type === 'pixel' || layer?.type === 'ai')) s.run('pixels.clear', { layerId: layer.id });
  else s.run('layer.delete', { layerId: tab.activeLayerId });
}

function cycleTool(tools: Tool[]) {
  const s = useEditor.getState();
  const i = tools.indexOf(s.tool);
  s.setTool(i < 0 ? tools[0] : tools[(i + 1) % tools.length]);
}

function resizeBrush(f: number) {
  const s = useEditor.getState();
  const key = s.tool === 'eraser' ? 'eraser' : 'brush';
  s.setToolOptions(key, { size: Math.min(1000, Math.max(1, Math.round(s.toolOptions[key].size * f))) });
}

function moveActiveLayer(delta: 1 | -1) {
  const s = useEditor.getState();
  const tab = activeTab(s);
  if (!tab?.activeLayerId) return;
  const loc = findLayer(tab.doc, tab.activeLayerId);
  if (!loc) return;
  const index = loc.index + delta;
  if (index < 0) return;
  s.run('layer.move', { layerId: tab.activeLayerId, parentId: loc.parentId, index });
}

/** Default keymap. Customizable shortcuts land in M3; this table is the single source for menus and handling. */
export const SHORTCUTS: Record<string, Shortcut> = {
  newDocument: { keys: 'mod+n', label: 'New…', run: () => useEditor.getState().setNewDocumentOpen(true) },
  open: { keys: 'mod+o', label: 'Open…', run: () => void openFile() },
  save: { keys: 'mod+s', label: 'Save', run: () => void saveDocument() },
  saveAs: { keys: 'mod+shift+s', label: 'Save As…', run: () => void saveDocument(true) },
  exportAs: { keys: 'mod+shift+e', label: 'Export…', run: openExportDialog },
  undo: { keys: 'mod+z', label: 'Undo', run: () => useEditor.getState().undo() },
  redo: { keys: 'mod+shift+z', label: 'Redo', run: () => useEditor.getState().redo() },
  redoAlt: { keys: 'mod+y', label: 'Redo', run: () => useEditor.getState().redo() },
  newLayer: {
    keys: 'mod+shift+n',
    label: 'New Layer',
    run: () => {
      const s = useEditor.getState();
      const tab = activeTab(s);
      s.run('layer.create', tab?.activeLayerId ? { above: tab.activeLayerId } : {});
    },
  },
  duplicateLayer: { keys: 'mod+j', label: 'Duplicate Layer', run: activeLayerCommand('layer.duplicate', (layerId) => ({ layerId })) },
  groupLayer: { keys: 'mod+g', label: 'Group Layer', run: activeLayerCommand('layer.group', (layerId) => ({ layerIds: [layerId] })) },
  ungroup: {
    keys: 'mod+shift+g',
    label: 'Ungroup',
    run: activeLayerCommand('layer.ungroup', (groupId) => ({ groupId })),
  },
  deleteLayer: { keys: 'delete', label: 'Delete', run: deleteOrClear },
  deleteLayerAlt: { keys: 'backspace', label: 'Delete', run: deleteOrClear },
  mergeDown: { keys: 'mod+e', label: 'Merge Down', run: activeLayerCommand('layer.mergeDown', (layerId) => ({ layerId })) },
  selectAll: { keys: 'mod+a', label: 'All', run: () => useEditor.getState().run('selection.all', {}) },
  deselect: { keys: 'mod+d', label: 'Deselect', run: () => useEditor.getState().run('selection.deselect', {}) },
  inverse: { keys: 'mod+shift+i', label: 'Inverse', run: () => useEditor.getState().run('selection.invert', {}) },
  freeTransform: { keys: 'mod+t', label: 'Free Transform', run: freeTransform },
  fillFg: { keys: 'alt+backspace', label: 'Fill with Foreground', run: () => fillSelection('fg') },
  fillBg: { keys: 'mod+backspace', label: 'Fill with Background', run: () => fillSelection('bg') },
  swapColors: { keys: 'x', label: 'Swap Colors', run: () => useEditor.getState().swapColors() },
  resetColors: { keys: 'd', label: 'Default Colors', run: () => useEditor.getState().resetColors() },
  quickMask: { keys: 'q', label: 'Quick Mask', run: () => useEditor.getState().setQuickMask(!useEditor.getState().quickMask) },
  brushSmaller: { keys: '[', label: 'Smaller Brush', run: () => resizeBrush(1 / 1.25) },
  brushBigger: { keys: ']', label: 'Bigger Brush', run: () => resizeBrush(1.25) },
  layerUp: { keys: 'mod+]', label: 'Bring Forward', run: () => moveActiveLayer(1) },
  layerDown: { keys: 'mod+[', label: 'Send Backward', run: () => moveActiveLayer(-1) },
  zoomIn: { keys: 'mod+=', label: 'Zoom In', run: () => zoomStep(1) },
  zoomInAlt: { keys: 'mod++', label: 'Zoom In', run: () => zoomStep(1) },
  zoomOut: { keys: 'mod+-', label: 'Zoom Out', run: () => zoomStep(-1) },
  fit: { keys: 'mod+0', label: 'Fit on Screen', run: fitToScreen },
  actualPixels: { keys: 'mod+1', label: '100%', run: actualPixels },
  toolMove: { keys: 'v', label: 'Move Tool', run: () => useEditor.getState().setTool('move') },
  toolMarquee: { keys: 'm', label: 'Marquee Tool', run: () => cycleTool(['marquee', 'ellipseMarquee']) },
  toolLasso: { keys: 'l', label: 'Lasso Tool', run: () => cycleTool(['lasso', 'polyLasso']) },
  toolWand: { keys: 'w', label: 'Magic Wand Tool', run: () => useEditor.getState().setTool('wand') },
  toolCrop: { keys: 'c', label: 'Crop Tool', run: () => useEditor.getState().setTool('crop') },
  toolEyedropper: { keys: 'i', label: 'Eyedropper Tool', run: () => useEditor.getState().setTool('eyedropper') },
  toolBrush: { keys: 'b', label: 'Brush Tool', run: () => useEditor.getState().setTool('brush') },
  toolEraser: { keys: 'e', label: 'Eraser Tool', run: () => useEditor.getState().setTool('eraser') },
  toolBucket: { keys: 'k', label: 'Paint Bucket Tool', run: () => useEditor.getState().setTool('bucket') },
  toolGradient: { keys: 'g', label: 'Gradient Tool', run: () => useEditor.getState().setTool('gradient') },
  toolText: { keys: 't', label: 'Text Tool', run: () => useEditor.getState().setTool('text') },
  toolShape: { keys: 'u', label: 'Shape Tool', run: () => useEditor.getState().setTool('shape') },
  toolHand: { keys: 'h', label: 'Hand Tool', run: () => useEditor.getState().setTool('hand') },
  toolZoom: { keys: 'z', label: 'Zoom Tool', run: () => useEditor.getState().setTool('zoom') },
};

export function eventToKeys(e: KeyboardEvent): string {
  const parts: string[] = [];
  if (e.metaKey || e.ctrlKey) parts.push('mod');
  if (e.altKey) parts.push('alt');
  if (e.shiftKey && (e.key.length > 1 || /^[a-z]$/i.test(e.key))) parts.push('shift');
  parts.push(e.key.toLowerCase());
  return parts.join('+');
}

const NON_TEXT_INPUTS = new Set(['checkbox', 'radio', 'button', 'submit', 'reset', 'range', 'color', 'file']);

/** Fields that consume typed keys (checkboxes, buttons and sliders don't block tool shortcuts). */
export function isEditableTarget(target: EventTarget | null): boolean {
  const el = target as HTMLElement | null;
  if (!el) return false;
  if (el.tagName === 'INPUT') return !NON_TEXT_INPUTS.has((el as HTMLInputElement).type);
  return el.isContentEditable || el.tagName === 'TEXTAREA' || el.tagName === 'SELECT';
}

export function useGlobalShortcuts(): void {
  useEffect(() => {
    const byKeys = new Map(Object.values(SHORTCUTS).map((s) => [s.keys, s]));
    const onKey = (e: KeyboardEvent) => {
      if (e.defaultPrevented || isEditableTarget(e.target)) return;
      const st = useEditor.getState();
      if (st.newDocumentOpen || st.paramsDialog || st.exportOpen || st.textEdit) return;
      const shortcut = byKeys.get(eventToKeys(e));
      if (!shortcut) return;
      e.preventDefault();
      shortcut.run();
    };
    window.addEventListener('keydown', onKey);
    return () => window.removeEventListener('keydown', onKey);
  }, []);
}
