import { ADJUSTMENT_KINDS, ADJUSTMENTS, FILTER_KINDS, FILTERS, findLayer } from '@canvas-ai/core';
import { useEffect, useRef, useState } from 'react';
import {
  activeLayerRun,
  actualPixels,
  canvasSizeDialog,
  colorRangeDialog,
  fitToScreen,
  imageSizeDialog,
  newAdjustmentLayer,
  openCommandDialog,
  openDirectAdjustment,
  openFile,
  openFilter,
  saveDocument,
  zoomStep,
} from '../actions';
import { formatKeys, SHORTCUTS } from '../shortcuts';
import { activeTab, useEditor } from '../store';
import { ChevronIcon, Logo } from './Icons';

export interface MenuItem {
  label: string;
  keys?: string;
  run?: () => void;
  disabled?: boolean;
  separatorBefore?: boolean;
  children?: MenuItem[];
  testId?: string;
}

function MenuList({ items, label, onClose, nested = false }: { items: MenuItem[]; label: string; onClose: () => void; nested?: boolean }) {
  const ref = useRef<HTMLDivElement>(null);
  const [sub, setSub] = useState<string | null>(null);
  useEffect(() => {
    if (!nested) ref.current?.querySelector<HTMLButtonElement>(':scope > div > [role=menuitem]:not(:disabled)')?.focus();
  }, [nested]);
  const onKeyDown = (e: React.KeyboardEvent) => {
    const buttons = [...(ref.current?.querySelectorAll<HTMLButtonElement>(':scope > div > [role=menuitem]:not(:disabled)') ?? [])];
    const i = buttons.indexOf(document.activeElement as HTMLButtonElement);
    if (i < 0) return;
    if (e.key === 'ArrowDown') buttons[(i + 1) % buttons.length]?.focus();
    else if (e.key === 'ArrowUp') buttons[(i - 1 + buttons.length) % buttons.length]?.focus();
    else if (e.key === 'ArrowRight' && buttons[i].dataset.sub) {
      setSub(buttons[i].dataset.sub!);
      setTimeout(() => ref.current?.querySelector<HTMLButtonElement>('.submenu [role=menuitem]:not(:disabled)')?.focus());
    } else if (e.key === 'ArrowLeft' && nested) {
      e.stopPropagation();
      (ref.current?.parentElement?.querySelector('[role=menuitem]') as HTMLElement | null)?.focus();
      return;
    } else if (e.key === 'Escape') onClose();
    else return;
    e.preventDefault();
    e.stopPropagation();
  };
  return (
    <div ref={ref} className={nested ? 'menu-popup submenu' : 'menu-popup'} role="menu" aria-label={label} onKeyDown={onKeyDown}>
      {items.map((item) => (
        <div key={item.label} className="menu-entry" onMouseEnter={() => setSub(item.children ? item.label : null)}>
          {item.separatorBefore && <div className="menu-sep" role="separator" />}
          <button
            role="menuitem"
            className="menu-item"
            disabled={item.disabled}
            aria-haspopup={item.children ? 'menu' : undefined}
            aria-expanded={item.children ? sub === item.label : undefined}
            data-sub={item.children ? item.label : undefined}
            data-testid={item.testId}
            onClick={() => {
              if (item.children) {
                setSub(item.label);
                return;
              }
              onClose();
              item.run?.();
            }}
          >
            <span>{item.label}</span>
            {item.keys && <kbd>{formatKeys(item.keys)}</kbd>}
            {item.children && <ChevronIcon width={12} height={12} />}
          </button>
          {item.children && sub === item.label && <MenuList items={item.children} label={item.label} onClose={onClose} nested />}
        </div>
      ))}
    </div>
  );
}

function Menu({ label, items, open, onOpen, onClose }: { label: string; items: MenuItem[]; open: boolean; onOpen: () => void; onClose: () => void }) {
  return (
    <div className="menu">
      <button
        className={`menu-trigger${open ? ' open' : ''}`}
        aria-haspopup="menu"
        aria-expanded={open}
        onClick={() => (open ? onClose() : onOpen())}
        onMouseEnter={(e) => e.currentTarget.closest('.menus')?.getAttribute('data-any-open') === 'true' && onOpen()}
      >
        {label}
      </button>
      {open && <MenuList items={items} label={label} onClose={onClose} />}
    </div>
  );
}

export function MenuBar() {
  const [open, setOpen] = useState<string | null>(null);
  const tab = useEditor(activeTab);
  const theme = useEditor((s) => s.theme);
  const setTheme = useEditor((s) => s.setTheme);
  const hasDoc = !!tab;
  const hasSel = !!tab?.doc.selection;
  const layer = tab?.activeLayerId ? findLayer(tab.doc, tab.activeLayerId)?.layer : null;
  const isPixel = layer?.type === 'pixel' || layer?.type === 'ai';
  const canUndo = !!tab?.bus.history.canUndo;
  const canRedo = !!tab?.bus.history.canRedo;
  const run = (id: string, params: Record<string, unknown> = {}) => useEditor.getState().run(id, params);
  const sc = (id: keyof typeof SHORTCUTS, extra: Partial<MenuItem> = {}): MenuItem => ({ label: SHORTCUTS[id].label, keys: SHORTCUTS[id].keys, run: SHORTCUTS[id].run, ...extra });

  useEffect(() => {
    if (!open) return;
    const close = (e: MouseEvent) => !(e.target as HTMLElement).closest('.menubar') && setOpen(null);
    window.addEventListener('mousedown', close);
    return () => window.removeEventListener('mousedown', close);
  }, [open]);

  const channels = tab?.doc.channels ?? [];
  const menus: Record<string, MenuItem[]> = {
    File: [
      sc('newDocument'),
      { label: 'Open…', keys: SHORTCUTS.open.keys, run: () => void openFile() },
      { label: 'Save', keys: SHORTCUTS.save.keys, run: () => void saveDocument(), disabled: !hasDoc, separatorBefore: true },
      { label: 'Save As…', keys: SHORTCUTS.saveAs.keys, run: () => void saveDocument(true), disabled: !hasDoc },
      { label: 'Export…', keys: SHORTCUTS.exportAs.keys, run: SHORTCUTS.exportAs.run, disabled: !hasDoc, separatorBefore: true, testId: 'menu-export' },
    ],
    Edit: [
      sc('undo', { disabled: !canUndo }),
      sc('redo', { disabled: !canRedo }),
      sc('fillFg', { disabled: !isPixel, separatorBefore: true, label: 'Fill with Foreground' }),
      sc('fillBg', { disabled: !isPixel, label: 'Fill with Background' }),
      { label: 'Clear', keys: 'delete', run: () => layer && run('pixels.clear', { layerId: layer.id }), disabled: !isPixel || !hasSel },
      sc('freeTransform', { disabled: !layer, separatorBefore: true }),
      { label: 'Flip Horizontal', run: () => activeLayerRun('layer.flip', { axis: 'horizontal' }), disabled: !layer },
      { label: 'Flip Vertical', run: () => activeLayerRun('layer.flip', { axis: 'vertical' }), disabled: !layer },
    ],
    Image: [
      {
        label: 'Adjustments',
        disabled: !isPixel,
        children: ADJUSTMENT_KINDS.map((k) => ({ label: `${ADJUSTMENTS[k as keyof typeof ADJUSTMENTS].label}…`, run: () => openDirectAdjustment(k as keyof typeof ADJUSTMENTS) })),
      },
      { label: 'Crop to Selection', run: () => run('document.cropToSelection'), disabled: !hasSel, separatorBefore: true },
      { label: 'Image Size…', run: imageSizeDialog, disabled: !hasDoc },
      { label: 'Canvas Size…', run: canvasSizeDialog, disabled: !hasDoc },
    ],
    Layer: [
      sc('newLayer', { disabled: !hasDoc }),
      {
        label: 'New Adjustment Layer',
        disabled: !hasDoc,
        children: ADJUSTMENT_KINDS.map((k) => ({ label: ADJUSTMENTS[k as keyof typeof ADJUSTMENTS].label, run: () => newAdjustmentLayer(k), testId: `new-adjust-${k}` })),
      },
      sc('duplicateLayer', { disabled: !layer }),
      sc('groupLayer', { disabled: !layer }),
      sc('ungroup', { disabled: layer?.type !== 'group' }),
      {
        label: 'Layer Mask',
        disabled: !layer,
        separatorBefore: true,
        children: [
          { label: 'Reveal All', run: () => activeLayerRun('layer.addMask', { from: 'revealAll' }), disabled: !!layer?.mask },
          { label: 'Hide All', run: () => activeLayerRun('layer.addMask', { from: 'hideAll' }), disabled: !!layer?.mask },
          { label: 'Reveal Selection', run: () => activeLayerRun('layer.addMask', { from: 'selection' }), disabled: !!layer?.mask || !hasSel },
          { label: 'Invert', run: () => activeLayerRun('layer.invertMask'), disabled: !layer?.mask, separatorBefore: true },
          { label: layer?.mask?.enabled === false ? 'Enable' : 'Disable', run: () => activeLayerRun('layer.setMaskEnabled', { enabled: layer?.mask?.enabled === false }), disabled: !layer?.mask },
          { label: 'Apply', run: () => activeLayerRun('layer.applyMask'), disabled: !layer?.mask || !isPixel },
          { label: 'Delete', run: () => activeLayerRun('layer.deleteMask'), disabled: !layer?.mask },
        ],
      },
      { label: 'Rasterize', run: () => activeLayerRun('layer.rasterize'), disabled: layer?.type !== 'text' && layer?.type !== 'shape' },
      sc('layerUp', { disabled: !layer, separatorBefore: true }),
      sc('layerDown', { disabled: !layer }),
      sc('mergeDown', { disabled: !layer, separatorBefore: true }),
      { label: 'Flatten Image', run: () => run('layer.flatten'), disabled: !hasDoc },
      { label: 'Delete Layer', run: () => activeLayerRun('layer.delete'), disabled: !layer, separatorBefore: true },
    ],
    Select: [
      sc('selectAll', { disabled: !hasDoc }),
      sc('deselect', { disabled: !hasSel }),
      sc('inverse', { disabled: !hasDoc }),
      { label: 'Color Range…', run: colorRangeDialog, disabled: !hasDoc, separatorBefore: true },
      { label: 'Load Layer Transparency', run: () => activeLayerRun('selection.fromLayer'), disabled: !layer },
      {
        label: 'Modify',
        disabled: !hasSel,
        separatorBefore: true,
        children: [
          { label: 'Feather…', run: () => openCommandDialog('selection.feather', 'Feather Selection', { radius: 5 }, [], true) },
          { label: 'Expand…', run: () => openCommandDialog('selection.expand', 'Expand Selection', { pixels: 5 }, [], true) },
          { label: 'Contract…', run: () => openCommandDialog('selection.contract', 'Contract Selection', { pixels: 5 }, [], true) },
        ],
      },
      { label: 'Save Selection', run: () => run('selection.save'), disabled: !hasSel, separatorBefore: true },
      {
        label: 'Load Selection',
        disabled: !channels.length,
        children: channels.map((c) => ({ label: c.name, run: () => run('selection.load', { channelId: c.id }) })),
      },
      sc('quickMask', { label: 'Quick Mask Mode', separatorBefore: true, disabled: !hasDoc }),
    ],
    Filter: FILTER_KINDS.map((k, i) => ({ label: `${FILTERS[k].label}…`, run: () => openFilter(k), disabled: !isPixel, separatorBefore: i === 3, testId: `filter-${k}` })),
    View: [
      { label: 'Zoom In', keys: SHORTCUTS.zoomIn.keys, run: () => zoomStep(1), disabled: !hasDoc },
      { label: 'Zoom Out', keys: SHORTCUTS.zoomOut.keys, run: () => zoomStep(-1), disabled: !hasDoc },
      { label: 'Fit on Screen', keys: SHORTCUTS.fit.keys, run: fitToScreen, disabled: !hasDoc },
      { label: '100%', keys: SHORTCUTS.actualPixels.keys, run: actualPixels, disabled: !hasDoc },
      { label: theme === 'dark' ? 'Light Theme' : 'Dark Theme', run: () => setTheme(theme === 'dark' ? 'light' : 'dark'), separatorBefore: true },
    ],
  };

  return (
    <nav className="menubar" aria-label="Main menu">
      <span className="brand">
        <Logo />
        <span>Canvas AI</span>
      </span>
      <div className="menus" data-any-open={open ? 'true' : 'false'}>
        {Object.entries(menus).map(([label, items]) => (
          <Menu key={label} label={label} items={items} open={open === label} onOpen={() => setOpen(label)} onClose={() => setOpen(null)} />
        ))}
      </div>
    </nav>
  );
}
