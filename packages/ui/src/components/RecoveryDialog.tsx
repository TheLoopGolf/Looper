import { useEffect, useRef, useState } from 'react';
import { deleteRecovery, listRecovery, type RecoveryEntry } from '../lib/autosave';
import { documentFromCnva } from '../lib/image-io';
import { useEditor } from '../store';

/** Offers documents autosaved by a previous session that ended without saving them. */
export function RecoveryDialog() {
  const [entries, setEntries] = useState<RecoveryEntry[]>([]);
  const ref = useRef<HTMLDialogElement>(null);

  useEffect(() => {
    if (typeof indexedDB === 'undefined') return;
    // Only entries that existed before this session started.
    const startedAt = Date.now();
    listRecovery()
      .then((all) => setEntries(all.filter((e) => e.savedAt < startedAt)))
      .catch(() => {});
  }, []);

  useEffect(() => {
    const d = ref.current!;
    if (entries.length && !d.open) d.showModal();
    if (!entries.length && d.open) d.close();
  }, [entries]);

  const restore = async (e: RecoveryEntry) => {
    try {
      const doc = documentFromCnva(e.bytes);
      useEditor.getState().openDocument({ ...doc, name: `${doc.name} (recovered)` });
      await deleteRecovery(e.id);
    } catch (err) {
      useEditor.getState().notify('error', `Could not recover "${e.name}": ${(err as Error).message}`);
    }
    setEntries((list) => list.filter((x) => x.id !== e.id));
  };
  const discard = async (e: RecoveryEntry) => {
    await deleteRecovery(e.id);
    setEntries((list) => list.filter((x) => x.id !== e.id));
  };

  return (
    <dialog ref={ref} className="dialog" aria-labelledby="recovery-title" onClose={() => setEntries([])} data-testid="recovery-dialog">
      <h2 id="recovery-title">Recover unsaved work</h2>
      <p className="hint">These documents had unsaved changes when Canvas AI last closed.</p>
      <ul className="recovery-list">
        {entries.map((e) => (
          <li key={e.id}>
            <span>
              <strong>{e.name}</strong>
              <small>{new Date(e.savedAt).toLocaleString()}</small>
            </span>
            <button className="btn" onClick={() => void discard(e)}>
              Discard
            </button>
            <button className="btn primary" onClick={() => void restore(e)} data-testid="recovery-restore">
              Restore
            </button>
          </li>
        ))}
      </ul>
      <div className="dialog-actions">
        <button className="btn" onClick={() => setEntries([])}>
          Decide later
        </button>
      </div>
    </dialog>
  );
}
