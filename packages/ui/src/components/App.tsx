import { useEffect } from 'react';
import { openBytes, setPlatform } from '../actions';
import type { Platform } from '../platform';
import { useGlobalShortcuts } from '../shortcuts';
import { activeTab, isTabDirty, useEditor } from '../store';
import { CanvasView } from './CanvasView';
import { HistoryPanel } from './HistoryPanel';
import { LayersPanel } from './LayersPanel';
import { MenuBar } from './MenuBar';
import { NewDocumentDialog } from './NewDocumentDialog';
import { StatusBar } from './StatusBar';
import { Tabs } from './Tabs';
import { Toasts } from './Toasts';
import { Toolbar } from './Toolbar';
import { Welcome } from './Welcome';
import { OptionsBar } from './OptionsBar';
import { PropertiesPanel } from './PropertiesPanel';
import { ParamsDialog } from './ParamsDialog';
import { ExportDialog } from './ExportDialog';
import { RecoveryDialog } from './RecoveryDialog';
import { installTextRasterizer } from '../lib/text-raster';
import { startAutosave } from '../lib/autosave';

installTextRasterizer();

export function App({ platform }: { platform?: Platform }) {
  if (platform) setPlatform(platform);
  const tab = useEditor(activeTab);
  const theme = useEditor((s) => s.theme);
  useGlobalShortcuts();

  useEffect(() => {
    document.documentElement.dataset.theme = theme;
  }, [theme]);

  useEffect(() => (typeof indexedDB === 'undefined' ? undefined : startAutosave()), []);

  // Warn before losing unsaved work.
  useEffect(() => {
    const onBeforeUnload = (e: BeforeUnloadEvent) => {
      if (useEditor.getState().tabs.some(isTabDirty)) e.preventDefault();
    };
    window.addEventListener('beforeunload', onBeforeUnload);
    return () => window.removeEventListener('beforeunload', onBeforeUnload);
  }, []);

  return (
    <div className="app">
      <MenuBar />
      <div className="workspace">
        <Toolbar />
        <main
          className="center"
          aria-label="Document"
          onDragOver={(e) => e.preventDefault()}
          onDrop={async (e) => {
            if (tab) return; // CanvasView handles drops when a document is open
            e.preventDefault();
            const file = e.dataTransfer.files[0];
            if (file) await openBytes(file.name, new Uint8Array(await file.arrayBuffer())).catch((err) => useEditor.getState().notify('error', (err as Error).message));
          }}
        >
          <Tabs />
          {tab && <OptionsBar />}
          {tab ? <CanvasView key={tab.id} tab={tab} /> : <Welcome />}
        </main>
        <aside className="dock" aria-label="Panels">
          <PropertiesPanel />
          <LayersPanel />
          <HistoryPanel />
        </aside>
      </div>
      <StatusBar />
      <NewDocumentDialog />
      <ParamsDialog />
      <ExportDialog />
      <RecoveryDialog />
      <Toasts />
    </div>
  );
}
