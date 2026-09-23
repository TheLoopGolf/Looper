import { BLEND_MODES, type Document, type LayerNode } from '@canvas-ai/core';
import { useState } from 'react';
import { activeTab, useEditor } from '../store';
import { ChevronIcon, CopyIcon, EyeIcon, EyeOffIcon, FolderIcon, LayerIcon, LockIcon, PlusIcon, TrashIcon } from './Icons';

interface Row {
  layer: LayerNode;
  depth: number;
  parentId: string | null;
  index: number;
}

/** Rows in panel order: top-most first, children under their (expanded) group. */
function panelRows(layers: readonly LayerNode[], collapsed: Set<string>, depth = 0, parentId: string | null = null): Row[] {
  const rows: Row[] = [];
  for (let i = layers.length - 1; i >= 0; i--) {
    const layer = layers[i];
    rows.push({ layer, depth, parentId, index: i });
    if (layer.type === 'group' && !collapsed.has(layer.id)) rows.push(...panelRows(layer.children, collapsed, depth + 1, layer.id));
  }
  return rows;
}

type DropPos = 'above' | 'below' | 'into';

function LayerProperties({ doc, layerId }: { doc: Document; layerId: string | null }) {
  const run = useEditor((s) => s.run);
  const layer = layerId ? findIn(doc.layers, layerId) : null;
  const disabled = !layer;
  const isGroup = layer?.type === 'group';
  return (
    <div className="layer-props">
      <label className="field">
        <span className="sr-only">Blend mode</span>
        <select
          aria-label="Blend mode"
          data-testid="blend-mode"
          disabled={disabled}
          value={layer?.blendMode ?? 'normal'}
          onChange={(e) => layer && run('layer.setBlendMode', { layerId: layer.id, blendMode: e.target.value })}
        >
          {isGroup && <option value="passThrough">Pass Through</option>}
          {BLEND_MODES.map((m, i) => (
            <option key={m.id} value={m.id} data-sep={i > 0 && BLEND_MODES[i - 1].group !== m.group ? '' : undefined}>
              {m.label}
            </option>
          ))}
        </select>
      </label>
      <PercentField label="Opacity" testId="opacity" disabled={disabled} value={(layer?.opacity ?? 1) * 100} onChange={(v) => layer && run('layer.setOpacity', { layerId: layer.id, opacity: v })} />
      {!isGroup && (
        <PercentField label="Fill" testId="fill" disabled={disabled} value={(layer?.fill ?? 1) * 100} onChange={(v) => layer && run('layer.setFill', { layerId: layer.id, fill: v })} />
      )}
    </div>
  );
}

function PercentField({ label, value, onChange, disabled, testId }: { label: string; value: number; onChange: (v: number) => void; disabled: boolean; testId: string }) {
  const rounded = Math.round(value);
  return (
    <label className="field percent">
      <span>{label}</span>
      <input
        type="range"
        min={0}
        max={100}
        step={1}
        value={rounded}
        disabled={disabled}
        aria-label={`${label} slider`}
        onChange={(e) => onChange(Number(e.target.value))}
      />
      <input
        type="number"
        min={0}
        max={100}
        value={rounded}
        disabled={disabled}
        data-testid={testId}
        aria-label={`${label} percent`}
        onChange={(e) => {
          const v = Number(e.target.value);
          if (Number.isFinite(v)) onChange(Math.min(100, Math.max(0, v)));
        }}
      />
    </label>
  );
}

function findIn(layers: readonly LayerNode[], id: string): LayerNode | null {
  for (const l of layers) {
    if (l.id === id) return l;
    if (l.type === 'group') {
      const f = findIn(l.children, id);
      if (f) return f;
    }
  }
  return null;
}

export function LayersPanel() {
  const tab = useEditor(activeTab);
  const run = useEditor((s) => s.run);
  const setActiveLayer = useEditor((s) => s.setActiveLayer);
  const [collapsed, setCollapsed] = useState<Set<string>>(new Set());
  const [renaming, setRenaming] = useState<string | null>(null);
  const [drag, setDrag] = useState<{ id: string; over?: string; pos?: DropPos } | null>(null);

  if (!tab) return <section className="panel" aria-label="Layers"><h2 className="panel-title">Layers</h2></section>;
  const { doc, activeLayerId } = tab;
  const rows = panelRows(doc.layers, collapsed);

  const toggleCollapsed = (id: string) =>
    setCollapsed((c) => {
      const n = new Set(c);
      if (n.has(id)) n.delete(id);
      else n.add(id);
      return n;
    });

  const drop = (target: Row, pos: DropPos) => {
    if (!drag || drag.id === target.layer.id) return;
    if (pos === 'into') run('layer.move', { layerId: drag.id, parentId: target.layer.id });
    else run('layer.move', { layerId: drag.id, [pos]: target.layer.id });
  };

  const onKeyDown = (e: React.KeyboardEvent) => {
    if (renaming) return;
    const i = rows.findIndex((r) => r.layer.id === activeLayerId);
    if (e.key === 'ArrowDown' || e.key === 'ArrowUp') {
      e.preventDefault();
      const next = rows[Math.min(rows.length - 1, Math.max(0, i + (e.key === 'ArrowDown' ? 1 : -1)))];
      if (next) setActiveLayer(next.layer.id);
    } else if (e.key === 'Enter' && activeLayerId) {
      e.preventDefault();
      setRenaming(activeLayerId);
    } else if ((e.key === 'ArrowLeft' || e.key === 'ArrowRight') && i >= 0 && rows[i].layer.type === 'group') {
      const isCollapsed = collapsed.has(rows[i].layer.id);
      if ((e.key === 'ArrowLeft') !== isCollapsed) toggleCollapsed(rows[i].layer.id);
    }
  };

  return (
    <section className="panel layers-panel" aria-label="Layers">
      <h2 className="panel-title">Layers</h2>
      <LayerProperties doc={doc} layerId={activeLayerId} />
      <div
        className="layer-list"
        role="tree"
        aria-label="Layer stack"
        tabIndex={0}
        onKeyDown={onKeyDown}
        aria-activedescendant={activeLayerId ? `layer-row-${activeLayerId}` : undefined}
        data-testid="layer-list"
      >
        {rows.length === 0 && <p className="empty">No layers. Create one with the + button.</p>}
        {rows.map((row) => {
          const { layer } = row;
          const selected = layer.id === activeLayerId;
          const isGroup = layer.type === 'group';
          const dropPos = drag?.over === layer.id ? drag.pos : undefined;
          return (
            <div
              key={layer.id}
              id={`layer-row-${layer.id}`}
              role="treeitem"
              aria-selected={selected}
              aria-level={row.depth + 1}
              aria-expanded={isGroup ? !collapsed.has(layer.id) : undefined}
              aria-label={layer.name}
              data-testid="layer-row"
              data-layer-id={layer.id}
              className={`layer-row${selected ? ' selected' : ''}${layer.visible ? '' : ' hidden-layer'}${dropPos ? ` drop-${dropPos}` : ''}`}
              style={{ paddingLeft: 6 + row.depth * 16 }}
              draggable={renaming !== layer.id}
              onClick={() => setActiveLayer(layer.id)}
              onDoubleClick={() => setRenaming(layer.id)}
              onDragStart={(e) => {
                e.dataTransfer.effectAllowed = 'move';
                e.dataTransfer.setData('text/plain', layer.id);
                setDrag({ id: layer.id });
              }}
              onDragOver={(e) => {
                if (!drag) return;
                e.preventDefault();
                const r = (e.currentTarget as HTMLElement).getBoundingClientRect();
                const f = (e.clientY - r.top) / r.height;
                const pos: DropPos = isGroup && f > 0.3 && f < 0.7 ? 'into' : f < 0.5 ? 'above' : 'below';
                if (drag.over !== layer.id || drag.pos !== pos) setDrag({ ...drag, over: layer.id, pos });
              }}
              onDrop={(e) => {
                e.preventDefault();
                if (drag?.pos) drop(row, drag.pos);
                setDrag(null);
              }}
              onDragEnd={() => setDrag(null)}
            >
              <button
                className="icon-btn"
                aria-label={layer.visible ? `Hide ${layer.name}` : `Show ${layer.name}`}
                aria-pressed={layer.visible}
                data-testid="visibility-toggle"
                onClick={(e) => {
                  e.stopPropagation();
                  run('layer.setVisible', { layerId: layer.id, visible: !layer.visible });
                }}
              >
                {layer.visible ? <EyeIcon /> : <EyeOffIcon />}
              </button>
              {isGroup ? (
                <button
                  className={`icon-btn chevron${collapsed.has(layer.id) ? '' : ' open'}`}
                  aria-label={collapsed.has(layer.id) ? `Expand ${layer.name}` : `Collapse ${layer.name}`}
                  onClick={(e) => {
                    e.stopPropagation();
                    toggleCollapsed(layer.id);
                  }}
                >
                  <ChevronIcon />
                </button>
              ) : (
                <span className="layer-kind" aria-hidden="true">
                  <LayerIcon />
                </span>
              )}
              {isGroup && <FolderIcon className="folder" />}
              {renaming === layer.id ? (
                <input
                  className="rename"
                  autoFocus
                  defaultValue={layer.name}
                  aria-label="Layer name"
                  onClick={(e) => e.stopPropagation()}
                  onBlur={(e) => {
                    const name = e.target.value.trim();
                    if (name && name !== layer.name) run('layer.rename', { layerId: layer.id, name });
                    setRenaming(null);
                  }}
                  onKeyDown={(e) => {
                    if (e.key === 'Enter') (e.target as HTMLInputElement).blur();
                    if (e.key === 'Escape') setRenaming(null);
                  }}
                />
              ) : (
                <span className="layer-name">{layer.name}</span>
              )}
              <span className="layer-meta">
                {layer.blendMode !== 'normal' && layer.blendMode !== 'passThrough' && <span className="badge">{BLEND_MODES.find((m) => m.id === layer.blendMode)?.label}</span>}
                {layer.opacity < 1 && <span className="badge">{Math.round(layer.opacity * 100)}%</span>}
              </span>
              <button
                className={`icon-btn lock${layer.locked ? ' on' : ''}`}
                aria-label={layer.locked ? `Unlock ${layer.name}` : `Lock ${layer.name}`}
                aria-pressed={layer.locked}
                onClick={(e) => {
                  e.stopPropagation();
                  run('layer.setLocked', { layerId: layer.id, locked: !layer.locked });
                }}
              >
                <LockIcon />
              </button>
            </div>
          );
        })}
      </div>
      <div className="panel-footer" role="toolbar" aria-label="Layer actions">
        <button className="icon-btn" aria-label="New layer" title="New layer" data-testid="new-layer" onClick={() => run('layer.create', activeLayerId ? { above: activeLayerId } : {})}>
          <PlusIcon />
        </button>
        <button className="icon-btn" aria-label="New group" title="New group" onClick={() => run('layer.create', activeLayerId ? { kind: 'group', above: activeLayerId } : { kind: 'group' })}>
          <FolderIcon />
        </button>
        <button className="icon-btn" aria-label="Duplicate layer" title="Duplicate layer" disabled={!activeLayerId} onClick={() => activeLayerId && run('layer.duplicate', { layerId: activeLayerId })}>
          <CopyIcon />
        </button>
        <button className="icon-btn" aria-label="Delete layer" title="Delete layer" data-testid="delete-layer" disabled={!activeLayerId} onClick={() => activeLayerId && run('layer.delete', { layerId: activeLayerId })}>
          <TrashIcon />
        </button>
      </div>
    </section>
  );
}
