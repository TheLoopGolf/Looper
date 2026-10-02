import { renderDocumentCPU } from '@canvas-ai/core';
import { useEffect, useRef, useState } from 'react';
import { getPlatform } from '../actions';
import { exportComposite, exportLayersZip, supportsAvif } from '../lib/export';
import { baseName, type ExportFormat } from '../lib/image-io';
import { activeTab, useEditor } from '../store';

const EXT: Record<ExportFormat, string> = { png: 'png', jpeg: 'jpg', webp: 'webp', avif: 'avif' };

export function ExportDialog() {
  const open = useEditor((s) => s.exportOpen);
  const tab = useEditor(activeTab);
  const ref = useRef<HTMLDialogElement>(null);
  const [format, setFormat] = useState<ExportFormat>('png');
  const [quality, setQuality] = useState(90);
  const [scale, setScale] = useState(100);
  const [strip, setStrip] = useState(true);
  const [avif, setAvif] = useState(false);
  const [busy, setBusy] = useState(false);
  const [size, setSize] = useState<string | null>(null);

  useEffect(() => {
    void supportsAvif().then(setAvif);
  }, []);
  useEffect(() => {
    const d = ref.current!;
    if (open && !d.open) {
      setSize(null);
      d.showModal();
    }
    if (!open && d.open) d.close();
  }, [open]);

  const close = () => useEditor.getState().setExportOpen(false);
  if (!tab) return <dialog ref={ref} className="dialog" onClose={close} />;
  const doc = tab.doc;
  const width = Math.max(1, Math.round((doc.width * scale) / 100));
  const height = Math.max(1, Math.round((doc.height * scale) / 100));
  const lossy = format !== 'png';
  const name = baseName(tab.fileName ?? doc.name);

  const encode = async () => {
    const renderer = useEditor.getState().renderer;
    const px = renderer ? await renderer.readComposite() : renderDocumentCPU(doc);
    return exportComposite(px, doc, { format, quality: quality / 100, width, height, stripMetadata: strip });
  };

  const run = async (fn: () => Promise<void>) => {
    setBusy(true);
    try {
      await fn();
    } catch (e) {
      useEditor.getState().notify('error', `Export failed: ${(e as Error).message}`);
    } finally {
      setBusy(false);
    }
  };

  return (
    <dialog ref={ref} className="dialog wide" aria-labelledby="export-title" onClose={close} data-testid="export-dialog">
      <form
        method="dialog"
        noValidate
        onSubmit={(e) => {
          e.preventDefault();
          void run(async () => {
            const bytes = await encode();
            const target = await getPlatform().saveFile(bytes, { name: `${name}.${EXT[format]}` }, [{ name: format.toUpperCase(), extensions: [EXT[format]] }]);
            if (target) {
              useEditor.getState().notify('info', `Exported ${target.name}`);
              close();
            }
          });
        }}
      >
        <h2 id="export-title">Export</h2>
        <div className="form-grid">
          <label className="form-row">
            <span>Format</span>
            <select value={format} onChange={(e) => setFormat(e.target.value as ExportFormat)} data-testid="export-format">
              <option value="png">PNG (lossless)</option>
              <option value="jpeg">JPEG</option>
              <option value="webp">WebP</option>
              <option value="avif" disabled={!avif}>
                AVIF{avif ? '' : ' (not supported by this browser)'}
              </option>
            </select>
          </label>
          <label className="form-row">
            <span>Quality {lossy ? `${quality}%` : '(lossless)'}</span>
            <input type="range" min={1} max={100} value={quality} disabled={!lossy} onChange={(e) => setQuality(Number(e.target.value))} />
          </label>
          <label className="form-row">
            <span>Scale (%)</span>
            <input type="number" min={1} max={800} value={scale} onChange={(e) => setScale(Math.min(800, Math.max(1, Number(e.target.value) || 100)))} data-testid="export-scale" />
          </label>
          <div className="form-row">
            <span>Size</span>
            <output data-testid="export-size">
              {width} × {height} px
            </output>
          </div>
          <label className="form-row">
            <span>Color profile</span>
            <select defaultValue="srgb">
              <option value="srgb">sRGB</option>
              <option value="p3" disabled>
                Display P3 (needs color management)
              </option>
            </select>
          </label>
          <label className="sf-check form-row-check">
            <input type="checkbox" checked={strip} onChange={(e) => setStrip(e.target.checked)} /> Strip metadata
          </label>
        </div>
        <p className="hint">
          {strip ? 'No metadata is written.' : 'Writes a Software tag (PNG and JPEG). Location and camera data are never written.'}
          {size && ` Estimated file: ${size}.`}
        </p>
        <div className="dialog-actions">
          <button
            type="button"
            className="btn"
            disabled={busy}
            onClick={() =>
              void run(async () => {
                const bytes = await exportLayersZip(doc, { width, height, stripMetadata: strip });
                const target = await getPlatform().saveFile(bytes, { name: `${name}-layers.zip` }, [{ name: 'Zip', extensions: ['zip'] }]);
                if (target) useEditor.getState().notify('info', `Exported ${doc.layers.length} layers to ${target.name}`);
              })
            }
            data-testid="export-layers"
          >
            Export layers…
          </button>
          <button type="button" className="btn" disabled={busy} onClick={() => void run(async () => setSize(`${((await encode()).length / 1024).toFixed(1)} KB`))}>
            Estimate size
          </button>
          <span className="spacer" />
          <button type="button" className="btn" onClick={close}>
            Cancel
          </button>
          <button type="submit" className="btn primary" disabled={busy} data-testid="export-submit">
            {busy ? 'Exporting…' : 'Export'}
          </button>
        </div>
      </form>
    </dialog>
  );
}
