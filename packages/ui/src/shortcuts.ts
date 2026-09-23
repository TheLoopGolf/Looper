import { useEffect } from 'react';
import { actualPixels, exportImage, fitToScreen, openFile, saveDocument, zoomStep } from './actions';
import { activeTab, useEditor } from './store';
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
  exportPng: { keys: 'mod+shift+e', label: 'Export PNG…', run: () => void exportImage('png') },
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
  deleteLayer: { keys: 'delete', label: 'Delete Layer', run: activeLayerCommand('layer.delete', (layerId) => ({ layerId })) },
  deleteLayerAlt: { keys: 'backspace', label: 'Delete Layer', run: activeLayerCommand('layer.delete', (layerId) => ({ layerId })) },
  layerUp: { keys: 'mod+]', label: 'Bring Forward', run: () => moveActiveLayer(1) },
  layerDown: { keys: 'mod+[', label: 'Send Backward', run: () => moveActiveLayer(-1) },
  zoomIn: { keys: 'mod+=', label: 'Zoom In', run: () => zoomStep(1) },
  zoomInAlt: { keys: 'mod++', label: 'Zoom In', run: () => zoomStep(1) },
  zoomOut: { keys: 'mod+-', label: 'Zoom Out', run: () => zoomStep(-1) },
  fit: { keys: 'mod+0', label: 'Fit on Screen', run: fitToScreen },
  actualPixels: { keys: 'mod+1', label: '100%', run: actualPixels },
  toolMove: { keys: 'v', label: 'Move Tool', run: () => useEditor.getState().setTool('move') },
  toolHand: { keys: 'h', label: 'Hand Tool', run: () => useEditor.getState().setTool('hand') },
  toolZoom: { keys: 'z', label: 'Zoom Tool', run: () => useEditor.getState().setTool('zoom') },
};

export function eventToKeys(e: KeyboardEvent): string {
  const parts: string[] = [];
  if (e.metaKey || e.ctrlKey) parts.push('mod');
  if (e.altKey) parts.push('alt');
  if (e.shiftKey && e.key.length > 1) parts.push('shift');
  else if (e.shiftKey && /^[a-z]$/i.test(e.key)) parts.push('shift');
  parts.push(e.key.toLowerCase());
  return parts.join('+');
}

function isEditableTarget(target: EventTarget | null): boolean {
  const el = target as HTMLElement | null;
  if (!el) return false;
  return el.isContentEditable || el.tagName === 'INPUT' || el.tagName === 'TEXTAREA' || el.tagName === 'SELECT';
}

export function useGlobalShortcuts(): void {
  useEffect(() => {
    const byKeys = new Map(Object.values(SHORTCUTS).map((s) => [s.keys, s]));
    const onKey = (e: KeyboardEvent) => {
      if (e.defaultPrevented || isEditableTarget(e.target)) return;
      if (useEditor.getState().newDocumentOpen) return;
      const shortcut = byKeys.get(eventToKeys(e));
      if (!shortcut) return;
      e.preventDefault();
      shortcut.run();
    };
    window.addEventListener('keydown', onKey);
    return () => window.removeEventListener('keydown', onKey);
  }, []);
}
