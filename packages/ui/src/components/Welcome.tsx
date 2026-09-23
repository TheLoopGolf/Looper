import { openFile } from '../actions';
import { createSampleDocument } from '../lib/sample';
import { useEditor } from '../store';
import { Logo } from './Icons';

export function Welcome() {
  const setNewDocumentOpen = useEditor((s) => s.setNewDocumentOpen);
  const openDocument = useEditor((s) => s.openDocument);
  return (
    <div className="welcome">
      <Logo size={56} />
      <h1>Canvas AI</h1>
      <p>A layer-based image editor. Drop an image anywhere, or start below.</p>
      <div className="welcome-actions">
        <button className="btn primary" onClick={() => setNewDocumentOpen(true)} data-testid="welcome-new">
          New document
        </button>
        <button className="btn" onClick={() => void openFile()}>
          Open…
        </button>
        <button className="btn" onClick={() => openDocument(createSampleDocument())} data-testid="welcome-sample">
          Open sample
        </button>
      </div>
    </div>
  );
}
