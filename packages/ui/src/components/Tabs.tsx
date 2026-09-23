import { isTabDirty, useEditor } from '../store';
import { CloseIcon } from './Icons';

export function Tabs() {
  const tabs = useEditor((s) => s.tabs);
  const activeTabId = useEditor((s) => s.activeTabId);
  const setActiveTab = useEditor((s) => s.setActiveTab);
  const closeTab = useEditor((s) => s.closeTab);
  if (tabs.length === 0) return null;
  return (
    <div className="tabs" role="tablist" aria-label="Open documents">
      {tabs.map((t) => {
        const dirty = isTabDirty(t);
        return (
          <div key={t.id} className={`tab${t.id === activeTabId ? ' active' : ''}`}>
            <button role="tab" aria-selected={t.id === activeTabId} className="tab-label" onClick={() => setActiveTab(t.id)} title={t.fileName ?? t.doc.name}>
              {t.fileName ?? t.doc.name}
              {dirty && <span className="dirty-dot" aria-label="unsaved changes">•</span>}
            </button>
            <button
              className="icon-btn tab-close"
              aria-label={`Close ${t.fileName ?? t.doc.name}`}
              onClick={() => {
                if (!dirty || confirm(`Close "${t.fileName ?? t.doc.name}" without saving?`)) closeTab(t.id);
              }}
            >
              <CloseIcon width={12} height={12} />
            </button>
          </div>
        );
      })}
    </div>
  );
}
