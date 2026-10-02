import { ADJUSTMENTS, findLayer, type JSONSchema, type LayerNode } from '@canvas-ai/core';
import { activeTab, useEditor } from '../store';
import { fromHex, toHex } from '../lib/color';
import { SchemaForm } from './SchemaForm';

function MaskSection({ layer }: { layer: LayerNode }) {
  const run = useEditor((s) => s.run);
  const editTarget = useEditor((s) => s.editTarget);
  const setEditTarget = useEditor((s) => s.setEditTarget);
  const m = layer.mask;
  if (!m) {
    return (
      <section className="props-section">
        <h3>Mask</h3>
        <div className="props-buttons">
          <button className="btn small" onClick={() => run('layer.addMask', { layerId: layer.id })} data-testid="add-mask">
            Add mask
          </button>
          <button className="btn small" onClick={() => run('layer.addMask', { layerId: layer.id, from: 'hideAll' })}>
            Add hide-all mask
          </button>
        </div>
      </section>
    );
  }
  return (
    <section className="props-section">
      <h3>Mask</h3>
      <label className="sf-check">
        <input type="checkbox" checked={editTarget === 'mask'} onChange={(e) => setEditTarget(e.target.checked ? 'mask' : 'pixels')} data-testid="edit-mask" /> Paint on mask
      </label>
      <label className="sf-check">
        <input type="checkbox" checked={m.enabled} onChange={(e) => run('layer.setMaskEnabled', { layerId: layer.id, enabled: e.target.checked })} /> Enabled
      </label>
      <SchemaForm fields={{ density: { type: 'number', minimum: 0, maximum: 100, title: 'Density (%)' } }} values={{ density: m.density * 100 }} onChange={(p) => run('layer.setMaskDensity', { layerId: layer.id, density: p.density })} />
      <div className="props-buttons">
        <button className="btn small" onClick={() => run('layer.invertMask', { layerId: layer.id })}>
          Invert
        </button>
        {(layer.type === 'pixel' || layer.type === 'ai') && (
          <button className="btn small" onClick={() => run('layer.applyMask', { layerId: layer.id })}>
            Apply
          </button>
        )}
        <button
          className="btn small"
          onClick={() => {
            run('layer.deleteMask', { layerId: layer.id });
            setEditTarget('pixels');
          }}
        >
          Delete
        </button>
      </div>
    </section>
  );
}

export function PropertiesPanel() {
  const tab = useEditor(activeTab);
  const run = useEditor((s) => s.run);
  const layer = tab?.activeLayerId ? findLayer(tab.doc, tab.activeLayerId)?.layer : null;
  let body: React.ReactNode = <p className="empty">{tab ? 'Select a layer to see its properties.' : ''}</p>;
  if (layer) {
    const sections: React.ReactNode[] = [];
    if (layer.type === 'adjustment') {
      const def = ADJUSTMENTS[layer.adjustment.kind as keyof typeof ADJUSTMENTS];
      if (def) {
        sections.push(
          <section key="adj" className="props-section" data-testid="adjustment-props">
            <h3>{def.label}</h3>
            {Object.keys(def.schema).length ? (
              <SchemaForm
                fields={def.schema as Record<string, JSONSchema>}
                values={layer.adjustment.params as Record<string, unknown>}
                onChange={(patch) => run('layer.setAdjustment', { layerId: layer.id, params: patch })}
              />
            ) : (
              <p className="hint">No settings.</p>
            )}
          </section>,
        );
      }
    } else if (layer.type === 'text') {
      sections.push(
        <section key="text" className="props-section">
          <h3>Text</h3>
          <textarea
            key={layer.id}
            className="props-text"
            defaultValue={layer.text}
            aria-label="Text content"
            onBlur={(e) => e.target.value.trim() && e.target.value !== layer.text && run('layer.updateText', { layerId: layer.id, text: e.target.value })}
          />
          <SchemaForm
            fields={{
              fontSize: { type: 'number', minimum: 1, maximum: 2000, title: 'Size (px)' },
              lineHeight: { type: 'number', minimum: 0.5, maximum: 5, title: 'Line height' },
              letterSpacing: { type: 'number', minimum: -50, maximum: 500, title: 'Tracking (px)' },
            }}
            values={layer.style as unknown as Record<string, unknown>}
            onChange={(patch) => run('layer.updateText', { layerId: layer.id, style: patch })}
          />
          <div className="sf-row">
            <label htmlFor="text-color">Color</label>
            <input id="text-color" type="color" value={toHex(layer.style.color)} onChange={(e) => run('layer.updateText', { layerId: layer.id, style: { color: fromHex(e.target.value, layer.style.color[3]) } })} />
          </div>
          <button className="btn small" onClick={() => run('layer.rasterize', { layerId: layer.id })}>
            Rasterize
          </button>
        </section>,
      );
    } else if (layer.type === 'shape') {
      const colorRow = (key: 'fillColor' | 'strokeColor', title: string) => {
        const c = layer[key];
        return (
          <div className="sf-row" key={key}>
            <label>
              <input type="checkbox" checked={!!c} onChange={(e) => run('layer.updateShape', { layerId: layer.id, [key]: e.target.checked ? [0, 0, 0, 255] : null })} /> {title}
            </label>
            <input type="color" disabled={!c} value={toHex(c ?? [0, 0, 0, 255])} aria-label={`${title} color`} onChange={(e) => run('layer.updateShape', { layerId: layer.id, [key]: fromHex(e.target.value) })} />
          </div>
        );
      };
      sections.push(
        <section key="shape" className="props-section">
          <h3>Shape ({layer.shape.kind})</h3>
          {colorRow('fillColor', 'Fill')}
          {colorRow('strokeColor', 'Stroke')}
          <SchemaForm
            fields={{ strokeWidth: { type: 'number', minimum: 0, maximum: 200, title: 'Stroke width (px)' } }}
            values={{ strokeWidth: layer.strokeWidth }}
            onChange={(p) => run('layer.updateShape', { layerId: layer.id, strokeWidth: p.strokeWidth })}
          />
          {layer.shape.kind === 'rect' && (
            <SchemaForm
              fields={{ radius: { type: 'number', minimum: 0, maximum: 1000, title: 'Corner radius (px)' } }}
              values={{ radius: layer.shape.radius }}
              onChange={(p) => run('layer.updateShape', { layerId: layer.id, shape: { radius: p.radius } })}
            />
          )}
          <button className="btn small" onClick={() => run('layer.rasterize', { layerId: layer.id })}>
            Rasterize
          </button>
        </section>,
      );
    }
    if (layer.type !== 'group' || layer.mask) sections.push(<MaskSection key="mask" layer={layer} />);
    body = sections;
  }
  return (
    <section className="panel props-panel" aria-label="Properties" data-testid="properties-panel">
      <h2 className="panel-title">Properties{layer ? ` · ${layer.name}` : ''}</h2>
      <div className="props-body">{body}</div>
    </section>
  );
}
