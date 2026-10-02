import { historyMetadata, writeCnva } from '@canvas-ai/core';
import { isTabDirty, useEditor } from '../store';

/**
 * Autosave + crash recovery. Unsaved documents are written to IndexedDB as
 * `.cnva` bytes a moment after edits stop. Entries are removed when a document
 * is saved or closed; anything left at startup came from a crash (or a closed
 * tab with unsaved work) and is offered for recovery.
 */

const DB_NAME = 'canvas-ai-recovery';
const STORE = 'documents';

export interface RecoveryEntry {
  id: string;
  name: string;
  savedAt: number;
  bytes: Uint8Array;
}

function openDb(): Promise<IDBDatabase> {
  return new Promise((resolve, reject) => {
    const req = indexedDB.open(DB_NAME, 1);
    req.onupgradeneeded = () => req.result.createObjectStore(STORE, { keyPath: 'id' });
    req.onsuccess = () => resolve(req.result);
    req.onerror = () => reject(req.error);
  });
}

async function tx<T>(mode: IDBTransactionMode, fn: (s: IDBObjectStore) => IDBRequest<T>): Promise<T> {
  const db = await openDb();
  return new Promise((resolve, reject) => {
    const t = db.transaction(STORE, mode);
    const req = fn(t.objectStore(STORE));
    t.oncomplete = () => {
      db.close();
      resolve(req.result);
    };
    t.onerror = () => {
      db.close();
      reject(t.error);
    };
  });
}

export const listRecovery = () => tx<RecoveryEntry[]>('readonly', (s) => s.getAll() as IDBRequest<RecoveryEntry[]>);
export const deleteRecovery = (id: string) => tx('readwrite', (s) => s.delete(id));
const putRecovery = (e: RecoveryEntry) => tx('readwrite', (s) => s.put(e));

/** Starts the autosave loop. `intervalMs` can be shortened with ?autosave=ms (tests). */
export function startAutosave(intervalMs = Number(new URLSearchParams(location.search).get('autosave')) || 10_000): () => void {
  const saved = new Map<string, number>(); // tab id → historyVersion written
  const changedAt = new Map<string, number>();
  let busy = false;
  const unsub = useEditor.subscribe((s, prev) => {
    for (const t of s.tabs) {
      const before = prev.tabs.find((p) => p.id === t.id);
      if (!before || before.historyVersion !== t.historyVersion) changedAt.set(t.id, Date.now());
    }
    // Closed tabs: the user chose to discard (or saved) — drop their recovery copies.
    for (const p of prev.tabs) {
      if (!s.tabs.some((t) => t.id === p.id)) {
        saved.delete(p.id);
        void deleteRecovery(p.id).catch(() => {});
      }
    }
  });
  const tick = async () => {
    if (busy) return;
    busy = true;
    try {
      for (const tab of useEditor.getState().tabs) {
        if (!isTabDirty(tab)) {
          if (saved.has(tab.id)) {
            saved.delete(tab.id);
            await deleteRecovery(tab.id);
          }
          continue;
        }
        if (saved.get(tab.id) === tab.historyVersion) continue;
        if (Date.now() - (changedAt.get(tab.id) ?? 0) < Math.min(1500, intervalMs / 2)) continue; // wait for a pause
        const bytes = await writeCnva({ document: tab.doc, history: historyMetadata(tab.bus.history.entries, tab.bus.history.cursor) });
        await putRecovery({ id: tab.id, name: tab.fileName ?? tab.doc.name, savedAt: Date.now(), bytes });
        saved.set(tab.id, tab.historyVersion);
      }
    } catch (e) {
      console.warn('Autosave failed', e);
    } finally {
      busy = false;
    }
  };
  const id = setInterval(() => void tick(), intervalMs);
  const onHide = () => document.visibilityState === 'hidden' && void tick();
  document.addEventListener('visibilitychange', onHide);
  return () => {
    clearInterval(id);
    unsub();
    document.removeEventListener('visibilitychange', onHide);
  };
}
