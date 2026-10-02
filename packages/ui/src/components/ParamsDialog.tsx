import { useEffect, useRef, useState } from 'react';
import { previewCommand } from '../tools/preview';
import { activeTab, useEditor } from '../store';
import { SchemaForm } from './SchemaForm';

export function ParamsDialog() {
  const spec = useEditor((s) => s.paramsDialog);
  const close = () => {
    useEditor.getState().setPreview(null);
    useEditor.getState().openParamsDialog(null);
  };
  const ref = useRef<HTMLDialogElement>(null);
  const [values, setValues] = useState<Record<string, unknown>>({});
  const [livePreview, setLivePreview] = useState(true);

  useEffect(() => {
    const d = ref.current!;
    if (spec) {
      setValues(spec.initial);
      if (!d.open) d.showModal();
    } else if (d.open) d.close();
  }, [spec]);

  // Debounced live preview through the real command, without touching history.
  useEffect(() => {
    if (!spec?.preview) return;
    const st = useEditor.getState();
    if (!livePreview) {
      st.setPreview(null);
      return;
    }
    const t = setTimeout(() => {
      const tab = activeTab(useEditor.getState());
      if (!tab) return;
      const doc = previewCommand({ state: useEditor.getState(), doc: tab.doc }, spec.commandId, spec.build(values));
      useEditor.getState().setPreview(doc);
    }, 120);
    return () => clearTimeout(t);
  }, [spec, values, livePreview]);

  return (
    <dialog ref={ref} className="dialog" aria-labelledby="params-title" onClose={close} data-testid="params-dialog">
      {spec && (
        <form
          method="dialog"
          noValidate
          onSubmit={(e) => {
            e.preventDefault();
            const params = spec.build(values);
            useEditor.getState().setPreview(null);
            useEditor.getState().openParamsDialog(null);
            useEditor.getState().run(spec.commandId, params);
          }}
        >
          <h2 id="params-title">{spec.title}</h2>
          <SchemaForm fields={spec.fields} values={values} onChange={(patch) => setValues((v) => ({ ...v, ...patch }))} />
          {spec.preview && (
            <label className="sf-check">
              <input type="checkbox" checked={livePreview} onChange={(e) => setLivePreview(e.target.checked)} /> Preview
            </label>
          )}
          <div className="dialog-actions">
            <button type="button" className="btn" onClick={close}>
              Cancel
            </button>
            <button type="submit" className="btn primary" data-testid="params-ok">
              OK
            </button>
          </div>
        </form>
      )}
    </dialog>
  );
}
