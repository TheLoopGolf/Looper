import { useEffect, useRef, useState } from 'react';
import { actualPixels, exportImage, fitToScreen, openFile, saveDocument, zoomStep } from '../actions';
import { formatKeys, SHORTCUTS } from '../shortcuts';
import { activeTab, useEditor } from '../store';
import { Logo } from './Icons';

interface MenuItem {
  label: string;
  keys?: string;
  run: () => void;
  disabled?: boolean;
  separatorBefore?: boolean;
}

function Menu({ label, items, open, onOpen, onClose }: { label: string; items: MenuItem[]; open: boolean; onOpen: () => void; onClose: () => void }) {
  const ref = useRef<HTMLDivElement>(null);
  useEffect(() => {
    if (!open) return;
    ref.current?.querySelector<HTMLButtonElement>('[role=menuitem]:not(:disabled)')?.focus();
  }, [open]);
  const onKeyDown = (e: React.KeyboardEvent) => {
    const buttons = [...(ref.current?.querySelectorAll<HTMLButtonElement>('[role=menuitem]:not(:disabled)') ?? [])];
    const i = buttons.indexOf(document.activeElement as HTMLButtonElement);
    if (e.key === 'ArrowDown') buttons[(i + 1) % buttons.length]?.focus();
    else if (e.key === 'ArrowUp') buttons[(i - 1 + buttons.length) % buttons.length]?.focus();
    else if (e.key === 'Escape') onClose();
    else return;
    e.preventDefault();
  };
  return (
    <div className="menu" ref={ref} onKeyDown={onKeyDown}>
      <button className={`menu-trigger${open ? ' open' : ''}`} aria-haspopup="menu" aria-expanded={open} onClick={() => (open ? onClose() : onOpen())} onMouseEnter={(e) => e.currentTarget.parentElement?.parentElement?.dataset.anyOpen === 'true' && onOpen()}>
        {label}
      </button>
      {open && (
        <div className="menu-popup" role="menu" aria-label={label}>
          {items.map((item) => (
            <div key={item.label}>
              {item.separatorBefore && <div className="menu-sep" role="separator" />}
              <button
                role="menuitem"
                className="menu-item"
                disabled={item.disabled}
                onClick={() => {
                  onClose();
                  item.run();
                }}
              >
                <span>{item.label}</span>
                {item.keys && <kbd>{formatKeys(item.keys)}</kbd>}
              </button>
            </div>
          ))}
        </div>
      )}
    </div>
  );
}

export function MenuBar() {
  const [open, setOpen] = useState<string | null>(null);
  const tab = useEditor(activeTab);
  const theme = useEditor((s) => s.theme);
  const setTheme = useEditor((s) => s.setTheme);
  const hasDoc = !!tab;
  const canUndo = !!tab?.bus.history.canUndo;
  const canRedo = !!tab?.bus.history.canRedo;
  const sc = (id: keyof typeof SHORTCUTS, extra: Partial<MenuItem> = {}): MenuItem => ({ label: SHORTCUTS[id].label, keys: SHORTCUTS[id].keys, run: SHORTCUTS[id].run, ...extra });

  useEffect(() => {
    if (!open) return;
    const close = (e: MouseEvent) => !(e.target as HTMLElement).closest('.menubar') && setOpen(null);
    window.addEventListener('mousedown', close);
    return () => window.removeEventListener('mousedown', close);
  }, [open]);

  const menus: Record<string, MenuItem[]> = {
    File: [
      sc('newDocument'),
      { label: 'Open…', keys: SHORTCUTS.open.keys, run: () => void openFile() },
      { label: 'Save', keys: SHORTCUTS.save.keys, run: () => void saveDocument(), disabled: !hasDoc, separatorBefore: true },
      { label: 'Save As…', keys: SHORTCUTS.saveAs.keys, run: () => void saveDocument(true), disabled: !hasDoc },
      { label: 'Export PNG…', keys: SHORTCUTS.exportPng.keys, run: () => void exportImage('png'), disabled: !hasDoc, separatorBefore: true },
      { label: 'Export JPEG…', run: () => void exportImage('jpeg', 0.9), disabled: !hasDoc },
      { label: 'Export WebP…', run: () => void exportImage('webp', 0.9), disabled: !hasDoc },
    ],
    Edit: [sc('undo', { disabled: !canUndo }), sc('redo', { disabled: !canRedo })],
    Layer: [
      sc('newLayer', { disabled: !hasDoc }),
      sc('duplicateLayer', { disabled: !tab?.activeLayerId }),
      sc('groupLayer', { disabled: !tab?.activeLayerId }),
      sc('ungroup', { disabled: !tab?.activeLayerId }),
      sc('layerUp', { disabled: !tab?.activeLayerId, separatorBefore: true }),
      sc('layerDown', { disabled: !tab?.activeLayerId }),
      { label: 'Delete Layer', keys: 'delete', run: SHORTCUTS.deleteLayer.run, disabled: !tab?.activeLayerId, separatorBefore: true },
    ],
    View: [
      { label: 'Zoom In', keys: SHORTCUTS.zoomIn.keys, run: () => zoomStep(1), disabled: !hasDoc },
      { label: 'Zoom Out', keys: SHORTCUTS.zoomOut.keys, run: () => zoomStep(-1), disabled: !hasDoc },
      { label: 'Fit on Screen', keys: SHORTCUTS.fit.keys, run: fitToScreen, disabled: !hasDoc },
      { label: '100%', keys: SHORTCUTS.actualPixels.keys, run: actualPixels, disabled: !hasDoc },
      { label: theme === 'dark' ? 'Light Theme' : 'Dark Theme', run: () => setTheme(theme === 'dark' ? 'light' : 'dark'), separatorBefore: true },
    ],
  };

  return (
    <nav className="menubar" aria-label="Main menu" data-any-open={open ? 'true' : 'false'}>
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
