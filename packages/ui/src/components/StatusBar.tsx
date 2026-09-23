import { useEffect, useState } from 'react';
import { activeTab, useEditor } from '../store';
import { cursorPosition, frameStats } from './CanvasView';

export function StatusBar() {
  const tab = useEditor(activeTab);
  const zoom = useEditor((s) => (s.activeTabId ? s.views[s.activeTabId]?.zoom : undefined));
  const renderer = useEditor((s) => s.renderer);
  const [, force] = useState(0);
  useEffect(() => {
    const id = setInterval(() => force((n) => n + 1), 500);
    return () => clearInterval(id);
  }, []);
  if (!tab) return <footer className="statusbar" />;
  const stats = renderer?.stats;
  return (
    <footer className="statusbar" aria-label="Status">
      <span data-testid="zoom-level">{zoom !== undefined ? `${+(zoom * 100).toFixed(zoom < 0.1 ? 1 : 0)}%` : ''}</span>
      <span>
        {tab.doc.width} × {tab.doc.height} px · {tab.doc.colorProfile} · {tab.doc.bitDepth}-bit
      </span>
      <span>{cursorPosition.inside ? `${cursorPosition.x}, ${cursorPosition.y}` : ''}</span>
      <span className="spacer" />
      {stats && (
        <span className="render-stats" data-testid="render-stats" title="Renderer backend, frame rate and pending tiles">
          {stats.backend.toUpperCase()} · {Math.round(frameStats.fps)} fps{stats.pendingTiles ? ` · ${stats.pendingTiles} tiles pending` : ''}
        </span>
      )}
      <span className="local-indicator" title="All processing happens on this device">On-device</span>
    </footer>
  );
}
