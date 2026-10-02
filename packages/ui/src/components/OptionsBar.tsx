import { findLayer, type TextLayer } from '@canvas-ai/core';
import { actualPixels, applyCrop, fitToScreen, freeTransform, activeLayerRun } from '../actions';
import { activeTab, useEditor, type SelectMode, type ShapeKind, type ToolOptions } from '../store';
import { cancelTransform, commitTransform, getTransform, resetCrop, updateTransform } from '../tools/transform-tools';
import { toolContext } from './CanvasView';
import { AlignCenterIcon, AlignLeftIcon, AlignRightIcon, CheckIcon, CloseIcon } from './Icons';

const FONTS = ['sans-serif', 'serif', 'monospace', 'Arial', 'Helvetica', 'Georgia', 'Times New Roman', 'Courier New', 'Verdana', 'Trebuchet MS', 'Impact'];

function Num({ label, value, min, max, step = 1, suffix, onChange, testId }: { label: string; value: number; min: number; max: number; step?: number; suffix?: string; onChange: (v: number) => void; testId?: string }) {
  return (
    <label className="opt">
      <span>{label}</span>
      <input
        type="number"
        min={min}
        max={max}
        step={step === 1 ? 1 : 'any'}
        value={+value.toFixed(2)}
        data-testid={testId}
        onChange={(e) => {
          const v = Number(e.target.value);
          if (Number.isFinite(v)) onChange(Math.min(max, Math.max(min, v)));
        }}
      />
      {suffix && <span className="unit">{suffix}</span>}
    </label>
  );
}

function Check({ label, checked, onChange }: { label: string; checked: boolean; onChange: (v: boolean) => void }) {
  return (
    <label className="opt check">
      <input type="checkbox" checked={checked} onChange={(e) => onChange(e.target.checked)} /> {label}
    </label>
  );
}

function Segmented<T extends string>({ label, value, options, onChange }: { label: string; value: T; options: { id: T; label: string; icon?: React.ReactNode }[]; onChange: (v: T) => void }) {
  return (
    <div className="segmented" role="radiogroup" aria-label={label}>
      {options.map((o) => (
        <button key={o.id} role="radio" aria-checked={value === o.id} className={value === o.id ? 'on' : ''} onClick={() => onChange(o.id)} title={o.label} aria-label={o.icon ? o.label : undefined}>
          {o.icon ?? o.label}
        </button>
      ))}
    </div>
  );
}

const MODES: { id: SelectMode; label: string }[] = [
  { id: 'replace', label: 'New' },
  { id: 'add', label: 'Add' },
  { id: 'subtract', label: 'Subtract' },
  { id: 'intersect', label: 'Intersect' },
];

export function OptionsBar() {
  const tool = useEditor((s) => s.tool);
  const o = useEditor((s) => s.toolOptions);
  const set = useEditor((s) => s.setToolOptions);
  const tab = useEditor(activeTab);
  const editTarget = useEditor((s) => s.editTarget);
  const quickMask = useEditor((s) => s.quickMask);
  useEditor((s) => s.transformVersion);
  const transform = getTransform();
  const setOpt = <K extends keyof ToolOptions>(k: K) => (patch: Partial<ToolOptions[K]>) => set(k, patch);

  let content: React.ReactNode = null;
  if (tool === 'brush' || tool === 'eraser') {
    const b = o[tool];
    const s = setOpt(tool);
    content = (
      <>
        <Num label="Size" value={b.size} min={1} max={1000} suffix="px" onChange={(size) => s({ size })} testId="opt-size" />
        <Num label="Hardness" value={Math.round(b.hardness * 100)} min={0} max={100} suffix="%" onChange={(v) => s({ hardness: v / 100 })} />
        <Num label="Opacity" value={b.opacity} min={1} max={100} suffix="%" onChange={(opacity) => s({ opacity })} />
        <Num label="Flow" value={b.flow} min={1} max={100} suffix="%" onChange={(flow) => s({ flow })} />
        <Check label="Pressure → size" checked={b.pressureSize} onChange={(pressureSize) => s({ pressureSize })} />
        <span className="opt-note">{quickMask ? 'Painting the quick mask' : editTarget === 'mask' ? 'Painting the layer mask' : ''}</span>
      </>
    );
  } else if (tool === 'marquee' || tool === 'ellipseMarquee' || tool === 'lasso' || tool === 'polyLasso') {
    const s = setOpt('select');
    content = (
      <>
        <Segmented label="Selection mode" value={o.select.mode} options={MODES} onChange={(mode) => s({ mode })} />
        <Num label="Feather" value={o.select.feather} min={0} max={250} suffix="px" onChange={(feather) => s({ feather })} />
        <Check label="Anti-alias" checked={o.select.antiAlias} onChange={(antiAlias) => s({ antiAlias })} />
        {tool === 'polyLasso' && <span className="opt-note">Click to add points · double-click or Enter to close</span>}
      </>
    );
  } else if (tool === 'wand') {
    const s = setOpt('wand');
    content = (
      <>
        <Segmented label="Selection mode" value={o.select.mode} options={MODES} onChange={(mode) => set('select', { mode })} />
        <Num label="Tolerance" value={o.wand.tolerance} min={0} max={255} onChange={(tolerance) => s({ tolerance })} />
        <Check label="Contiguous" checked={o.wand.contiguous} onChange={(contiguous) => s({ contiguous })} />
        <Check label="Sample all layers" checked={o.wand.sampleMerged} onChange={(sampleMerged) => s({ sampleMerged })} />
      </>
    );
  } else if (tool === 'bucket') {
    const s = setOpt('bucket');
    content = (
      <>
        <Num label="Tolerance" value={o.bucket.tolerance} min={0} max={255} onChange={(tolerance) => s({ tolerance })} />
        <Num label="Opacity" value={o.bucket.opacity} min={1} max={100} suffix="%" onChange={(opacity) => s({ opacity })} />
        <Check label="Contiguous" checked={o.bucket.contiguous} onChange={(contiguous) => s({ contiguous })} />
        <Check label="Sample all layers" checked={o.bucket.sampleMerged} onChange={(sampleMerged) => s({ sampleMerged })} />
      </>
    );
  } else if (tool === 'gradient') {
    const s = setOpt('gradient');
    content = (
      <>
        <Segmented
          label="Gradient type"
          value={o.gradient.kind}
          options={(['linear', 'radial', 'angle', 'reflected', 'diamond'] as const).map((id) => ({ id, label: id[0].toUpperCase() + id.slice(1) }))}
          onChange={(kind) => s({ kind })}
        />
        <Num label="Opacity" value={o.gradient.opacity} min={1} max={100} suffix="%" onChange={(opacity) => s({ opacity })} />
        <span className="opt-note">Foreground → background · Shift snaps to 45°</span>
      </>
    );
  } else if (tool === 'shape') {
    const s = setOpt('shape');
    content = (
      <>
        <Segmented<ShapeKind>
          label="Shape"
          value={o.shape.kind}
          options={[
            { id: 'rect', label: 'Rectangle' },
            { id: 'ellipse', label: 'Ellipse' },
            { id: 'polygon', label: 'Polygon' },
            { id: 'line', label: 'Line' },
            { id: 'pen', label: 'Pen' },
          ]}
          onChange={(kind) => s({ kind })}
        />
        <Check label="Fill" checked={o.shape.fill} onChange={(fill) => s({ fill })} />
        <Check label="Stroke" checked={o.shape.stroke} onChange={(stroke) => s({ stroke })} />
        <Num label="Stroke" value={o.shape.strokeWidth} min={1} max={200} suffix="px" onChange={(strokeWidth) => s({ strokeWidth })} />
        {o.shape.kind === 'rect' && <Num label="Radius" value={o.shape.radius} min={0} max={1000} suffix="px" onChange={(radius) => s({ radius })} />}
        {o.shape.kind === 'polygon' && <Num label="Sides" value={o.shape.sides} min={3} max={64} onChange={(sides) => s({ sides })} />}
      </>
    );
  } else if (tool === 'text') {
    const s = setOpt('text');
    const active = tab?.activeLayerId ? findLayer(tab.doc, tab.activeLayerId)?.layer : null;
    const textLayer = active?.type === 'text' ? (active as TextLayer) : null;
    // Options apply to the selected text layer too.
    const apply = (patch: Partial<ToolOptions['text']>) => {
      s(patch);
      if (textLayer) useEditor.getState().run('layer.updateText', { layerId: textLayer.id, style: patch });
    };
    const t = textLayer ? { ...o.text, ...textLayer.style } : o.text;
    content = (
      <>
        <label className="opt">
          <span>Font</span>
          <select value={t.fontFamily} onChange={(e) => apply({ fontFamily: e.target.value })} aria-label="Font family">
            {[...new Set([t.fontFamily, ...FONTS])].map((f) => (
              <option key={f} value={f}>
                {f}
              </option>
            ))}
          </select>
        </label>
        <Num label="Size" value={t.fontSize} min={1} max={2000} suffix="px" onChange={(fontSize) => apply({ fontSize })} />
        <label className="opt">
          <span>Weight</span>
          <select value={t.fontWeight} onChange={(e) => apply({ fontWeight: Number(e.target.value) })} aria-label="Font weight">
            {[300, 400, 500, 600, 700, 800, 900].map((w) => (
              <option key={w} value={w}>
                {w}
              </option>
            ))}
          </select>
        </label>
        <Check label="Italic" checked={t.italic} onChange={(italic) => apply({ italic })} />
        <Segmented
          label="Alignment"
          value={t.align}
          options={[
            { id: 'left', label: 'Align left', icon: <AlignLeftIcon /> },
            { id: 'center', label: 'Align center', icon: <AlignCenterIcon /> },
            { id: 'right', label: 'Align right', icon: <AlignRightIcon /> },
          ]}
          onChange={(align) => apply({ align })}
        />
        <span className="opt-note">Click to type · Ctrl+Enter to finish</span>
      </>
    );
  } else if (tool === 'crop') {
    const s = setOpt('crop');
    content = (
      <>
        <label className="opt">
          <span>Ratio</span>
          <select value={o.crop.aspect} onChange={(e) => s({ aspect: e.target.value as ToolOptions['crop']['aspect'] })} aria-label="Crop aspect ratio" data-testid="crop-aspect">
            <option value="free">Free</option>
            <option value="original">Original</option>
            <option value="1:1">1:1 (square)</option>
            <option value="4:5">4:5 (portrait post)</option>
            <option value="3:2">3:2 (photo)</option>
            <option value="16:9">16:9 (video)</option>
            <option value="9:16">9:16 (story)</option>
          </select>
        </label>
        <label className="opt">
          <span>Straighten</span>
          <input type="range" min={-45} max={45} step={0.1} value={o.crop.angle} onChange={(e) => s({ angle: Number(e.target.value) })} aria-label="Straighten angle" />
          <span className="unit">{o.crop.angle.toFixed(1)}°</span>
        </label>
        <button className="btn small primary" onClick={applyCrop} data-testid="crop-apply">
          <CheckIcon /> Crop
        </button>
        <button
          className="btn small"
          onClick={() => {
            resetCrop();
            s({ angle: 0 });
          }}
        >
          Reset
        </button>
      </>
    );
  } else if (tool === 'move') {
    if (transform && tab) {
      const ctx = () => toolContext(tab);
      content = (
        <>
          <Num label="W" value={transform.sx * 100} min={-10000} max={10000} suffix="%" onChange={(v) => updateTransform(ctx(), { sx: v / 100 })} />
          <Num label="H" value={transform.sy * 100} min={-10000} max={10000} suffix="%" onChange={(v) => updateTransform(ctx(), { sy: v / 100 })} />
          <Num label="Angle" value={transform.angle} min={-360} max={360} step={0.1} suffix="°" onChange={(angle) => updateTransform(ctx(), { angle })} />
          <button className="btn small primary" onClick={() => commitTransform(ctx())} data-testid="transform-apply">
            <CheckIcon /> Apply
          </button>
          <button className="btn small" onClick={() => cancelTransform(ctx())}>
            <CloseIcon /> Cancel
          </button>
        </>
      );
    } else {
      content = (
        <>
          <button className="btn small" onClick={freeTransform}>
            Free Transform
          </button>
          <button className="btn small" onClick={() => activeLayerRun('layer.flip', { axis: 'horizontal' })}>
            Flip Horizontal
          </button>
          <button className="btn small" onClick={() => activeLayerRun('layer.flip', { axis: 'vertical' })}>
            Flip Vertical
          </button>
          <span className="opt-note">Drag to move the layer · arrows nudge (Shift ×10)</span>
        </>
      );
    }
  } else if (tool === 'eyedropper') {
    content = <span className="opt-note">Click to pick the foreground color · Alt-click for background</span>;
  } else if (tool === 'hand' || tool === 'zoom') {
    content = (
      <>
        <button className="btn small" onClick={fitToScreen}>
          Fit on Screen
        </button>
        <button className="btn small" onClick={actualPixels}>
          100%
        </button>
      </>
    );
  }
  return (
    <div className="options-bar" role="toolbar" aria-label="Tool options" data-testid="options-bar">
      {content}
    </div>
  );
}
