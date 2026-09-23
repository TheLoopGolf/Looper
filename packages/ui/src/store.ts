import {
  CommandBus,
  CommandError,
  createDefaultRegistry,
  createDocument,
  createPixelLayer,
  fillRect,
  findLayer,
  type BusEvent,
  type CommandRegistry,
  type Document,
} from '@canvas-ai/core';
import type { Renderer, ViewState } from '@canvas-ai/render';
import { create } from 'zustand';
import { newDocId } from './lib/image-io';

export type Tool = 'move' | 'hand' | 'zoom';
export type Theme = 'dark' | 'light';

/** One open document. The bus owns the document; the store mirrors it for React. */
export interface DocTab {
  readonly id: string;
  readonly bus: CommandBus;
  readonly doc: Document;
  /** Bumped on every history change so panels re-render. */
  readonly historyVersion: number;
  readonly activeLayerId: string | null;
  readonly fileName: string | null;
  readonly fileHandle?: unknown;
  /** History entry id at last save (null = never saved); compared to detect unsaved changes. */
  readonly savedAt: number | null;
}

export interface Toast {
  id: number;
  kind: 'error' | 'info';
  message: string;
}

export interface EditorState {
  registry: CommandRegistry;
  tabs: DocTab[];
  activeTabId: string | null;
  tool: Tool;
  theme: Theme;
  toasts: Toast[];
  /** Renderer of the active canvas (set by CanvasView). */
  renderer: Renderer | null;
  /** View per tab, kept outside DocTab so pan/zoom doesn't re-render document panels. */
  views: Record<string, ViewState>;
  /** CSS size of the canvas viewport. */
  viewport: { width: number; height: number };
  newDocumentOpen: boolean;

  openDocument(doc: Document, file?: { name: string; handle?: unknown }): string;
  newDocument(opts: { name?: string; width: number; height: number; background: 'white' | 'transparent' | 'black' }): string;
  closeTab(id: string): void;
  setActiveTab(id: string): void;
  setActiveLayer(layerId: string | null): void;
  setView(tabId: string, view: ViewState): void;
  setViewport(width: number, height: number): void;
  setTool(tool: Tool): void;
  setTheme(theme: Theme): void;
  setRenderer(renderer: Renderer | null): void;
  setNewDocumentOpen(open: boolean): void;
  markSaved(tabId: string, file: { name: string; handle?: unknown }): void;
  /** Dispatches on the active document. Returns the command result, or undefined if it failed (a toast is shown). */
  run<R = unknown>(commandId: string, params?: Record<string, unknown>): R | undefined;
  undo(): void;
  redo(): void;
  jumpTo(cursor: number): void;
  notify(kind: Toast['kind'], message: string): void;
  dismissToast(id: number): void;
}

let toastId = 0;

export function activeTab(state: Pick<EditorState, 'tabs' | 'activeTabId'>): DocTab | null {
  return state.tabs.find((t) => t.id === state.activeTabId) ?? null;
}

export function isTabDirty(tab: DocTab): boolean {
  const { entries, cursor } = tab.bus.history;
  const current = cursor > 0 ? entries[cursor - 1].id : 0;
  return current !== (tab.savedAt ?? 0);
}

function topLayerId(doc: Document): string | null {
  return doc.layers.length ? doc.layers[doc.layers.length - 1].id : null;
}

const prefersLight = typeof matchMedia !== 'undefined' && matchMedia('(prefers-color-scheme: light)').matches;

export const useEditor = create<EditorState>()((set, get) => {
  const updateTab = (id: string, update: (t: DocTab) => Partial<DocTab>) =>
    set((s) => ({ tabs: s.tabs.map((t) => (t.id === id ? { ...t, ...update(t) } : t)) }));

  const attach = (bus: CommandBus, tabId: string) =>
    bus.subscribe((event: BusEvent, doc) => {
      updateTab(tabId, (t) => {
        let activeLayerId = t.activeLayerId;
        // Keep a valid selection: undo may remove the active layer.
        if (!activeLayerId || !findLayer(doc, activeLayerId)) activeLayerId = topLayerId(doc);
        return { doc, historyVersion: t.historyVersion + 1, activeLayerId };
      });
      if (event.type === 'reset') get().notify('info', 'Document reloaded');
    });

  const guard = <R,>(fn: () => R): R | undefined => {
    try {
      return fn();
    } catch (e) {
      get().notify('error', e instanceof CommandError ? e.message : `Unexpected error: ${(e as Error).message}`);
      if (!(e instanceof CommandError)) console.error(e);
      return undefined;
    }
  };

  return {
    registry: createDefaultRegistry(),
    tabs: [],
    activeTabId: null,
    tool: 'move',
    theme: prefersLight ? 'light' : 'dark',
    toasts: [],
    renderer: null,
    views: {},
    viewport: { width: 800, height: 600 },
    newDocumentOpen: false,

    openDocument(doc, file) {
      const bus = new CommandBus(get().registry, doc);
      const tab: DocTab = {
        id: doc.id,
        bus,
        doc,
        historyVersion: 0,
        activeLayerId: topLayerId(doc),
        fileName: file?.name ?? null,
        fileHandle: file?.handle,
        savedAt: file ? 0 : null,
      };
      attach(bus, tab.id);
      set((s) => ({ tabs: [...s.tabs, tab], activeTabId: tab.id }));
      return tab.id;
    },

    newDocument({ name, width, height, background }) {
      let doc = createDocument({ id: newDocId(), name: name || 'Untitled', width, height });
      if (background !== 'transparent') {
        const color = background === 'white' ? [255, 255, 255, 255] : [0, 0, 0, 255];
        const bg = createPixelLayer(doc, `layer_bg_${doc.id}`, 'Background');
        doc = { ...doc, layers: [{ ...bg, tiles: fillRect(bg.tiles, { x: 0, y: 0, width, height }, color) }] };
      }
      return get().openDocument(doc);
    },

    closeTab(id) {
      const tab = get().tabs.find((t) => t.id === id);
      if (!tab) return;
      set((s) => {
        const tabs = s.tabs.filter((t) => t.id !== id);
        const idx = s.tabs.findIndex((t) => t.id === id);
        const activeTabId = s.activeTabId === id ? (tabs[Math.min(idx, tabs.length - 1)]?.id ?? null) : s.activeTabId;
        const { [id]: _removed, ...views } = s.views;
        return { tabs, activeTabId, views };
      });
    },

    setActiveTab: (id) => set({ activeTabId: id }),
    setActiveLayer(layerId) {
      const tab = activeTab(get());
      if (tab) updateTab(tab.id, () => ({ activeLayerId: layerId }));
    },
    setView: (tabId, view) => set((s) => ({ views: { ...s.views, [tabId]: view } })),
    setViewport: (width, height) => set({ viewport: { width, height } }),
    setTool: (tool) => set({ tool }),
    setTheme: (theme) => set({ theme }),
    setRenderer: (renderer) => set({ renderer }),
    setNewDocumentOpen: (open) => set({ newDocumentOpen: open }),
    markSaved(tabId, file) {
      updateTab(tabId, (t) => {
        const { entries, cursor } = t.bus.history;
        return { fileName: file.name, fileHandle: file.handle, savedAt: cursor > 0 ? entries[cursor - 1].id : 0 };
      });
    },

    run<R>(commandId: string, params: Record<string, unknown> = {}) {
      const tab = activeTab(get());
      if (!tab) return undefined;
      const result = guard(() => tab.bus.dispatch<R>(commandId, params));
      const created = (result as { layerId?: string; groupId?: string } | undefined) ?? {};
      const newId = created.layerId ?? created.groupId;
      if (newId) get().setActiveLayer(newId);
      return result;
    },
    undo() {
      const tab = activeTab(get());
      if (tab) guard(() => tab.bus.undo());
    },
    redo() {
      const tab = activeTab(get());
      if (tab) guard(() => tab.bus.redo());
    },
    jumpTo(cursor) {
      const tab = activeTab(get());
      if (tab) guard(() => tab.bus.jumpTo(cursor));
    },
    notify(kind, message) {
      const id = ++toastId;
      set((s) => ({ toasts: [...s.toasts.slice(-3), { id, kind, message }] }));
      setTimeout(() => get().dismissToast(id), kind === 'error' ? 6000 : 3000);
    },
    dismissToast: (id) => set((s) => ({ toasts: s.toasts.filter((t) => t.id !== id) })),
  };
});
