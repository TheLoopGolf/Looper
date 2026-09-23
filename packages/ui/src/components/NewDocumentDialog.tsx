import { MAX_DOCUMENT_SIZE } from '@canvas-ai/core';
import { useEffect, useRef, useState } from 'react';
import { useEditor } from '../store';

const PRESETS = [
  { label: 'Full HD 1920 × 1080', width: 1920, height: 1080 },
  { label: 'Instagram portrait 1080 × 1350', width: 1080, height: 1350 },
  { label: 'Square 2048 × 2048', width: 2048, height: 2048 },
  { label: 'A4 @ 300 dpi 2480 × 3508', width: 2480, height: 3508 },
  { label: 'Photo 6000 × 4000', width: 6000, height: 4000 },
];

export function NewDocumentDialog() {
  const open = useEditor((s) => s.newDocumentOpen);
  const setOpen = useEditor((s) => s.setNewDocumentOpen);
  const newDocument = useEditor((s) => s.newDocument);
  const ref = useRef<HTMLDialogElement>(null);
  const [name, setName] = useState('Untitled');
  const [width, setWidth] = useState(1920);
  const [height, setHeight] = useState(1080);
  const [background, setBackground] = useState<'white' | 'transparent' | 'black'>('white');

  useEffect(() => {
    const d = ref.current!;
    if (open && !d.open) d.showModal();
    if (!open && d.open) d.close();
  }, [open]);

  const valid = Number.isInteger(width) && Number.isInteger(height) && width >= 1 && height >= 1 && width <= MAX_DOCUMENT_SIZE && height <= MAX_DOCUMENT_SIZE;

  return (
    <dialog ref={ref} className="dialog" aria-labelledby="new-doc-title" onClose={() => setOpen(false)} data-testid="new-document-dialog">
      <form
        method="dialog"
        onSubmit={(e) => {
          e.preventDefault();
          if (!valid) return;
          newDocument({ name, width, height, background });
          setOpen(false);
        }}
      >
        <h2 id="new-doc-title">New document</h2>
        <label className="form-row">
          <span>Name</span>
          <input value={name} onChange={(e) => setName(e.target.value)} />
        </label>
        <label className="form-row">
          <span>Preset</span>
          <select
            onChange={(e) => {
              const p = PRESETS[Number(e.target.value)];
              if (p) {
                setWidth(p.width);
                setHeight(p.height);
              }
            }}
            defaultValue=""
          >
            <option value="" disabled>
              Choose a preset…
            </option>
            {PRESETS.map((p, i) => (
              <option key={p.label} value={i}>
                {p.label}
              </option>
            ))}
          </select>
        </label>
        <div className="form-grid">
          <label className="form-row">
            <span>Width (px)</span>
            <input type="number" min={1} max={MAX_DOCUMENT_SIZE} value={width} onChange={(e) => setWidth(Number(e.target.value))} data-testid="new-width" />
          </label>
          <label className="form-row">
            <span>Height (px)</span>
            <input type="number" min={1} max={MAX_DOCUMENT_SIZE} value={height} onChange={(e) => setHeight(Number(e.target.value))} data-testid="new-height" />
          </label>
        </div>
        <label className="form-row">
          <span>Background</span>
          <select value={background} onChange={(e) => setBackground(e.target.value as typeof background)}>
            <option value="white">White</option>
            <option value="black">Black</option>
            <option value="transparent">Transparent</option>
          </select>
        </label>
        {!valid && <p className="form-error">Size must be between 1 and {MAX_DOCUMENT_SIZE} px.</p>}
        <div className="dialog-actions">
          <button type="button" className="btn" onClick={() => setOpen(false)}>
            Cancel
          </button>
          <button type="submit" className="btn primary" disabled={!valid} data-testid="create-document">
            Create
          </button>
        </div>
      </form>
    </dialog>
  );
}
