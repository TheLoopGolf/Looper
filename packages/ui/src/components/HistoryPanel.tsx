import type { Actor, HistoryEntry } from '@canvas-ai/core';
import { useEffect, useRef, useState } from 'react';
import { activeTab, useEditor } from '../store';
import { ChevronIcon, SparkIcon } from './Icons';

function ActorBadge({ actor }: { actor: Actor }) {
  if (actor.kind === 'user') return null;
  const label = actor.kind === 'agent' ? actor.name : actor.name;
  return (
    <span className={`badge actor-${actor.kind}`} title={`Performed by ${actor.kind}: ${label}`}>
      {actor.kind === 'agent' && <SparkIcon width={11} height={11} />} {label}
    </span>
  );
}

export function HistoryPanel() {
  const tab = useEditor(activeTab);
  const jumpTo = useEditor((s) => s.jumpTo);
  const [expanded, setExpanded] = useState<Set<number>>(new Set());
  const listRef = useRef<HTMLOListElement>(null);
  const version = tab?.historyVersion;

  useEffect(() => {
    listRef.current?.querySelector('.current')?.scrollIntoView({ block: 'nearest' });
  }, [version]);

  if (!tab) return <section className="panel" aria-label="History"><h2 className="panel-title">History</h2></section>;
  const { entries, cursor } = tab.bus.history;

  const toggle = (id: number) =>
    setExpanded((s) => {
      const n = new Set(s);
      if (n.has(id)) n.delete(id);
      else n.add(id);
      return n;
    });

  const row = (entry: HistoryEntry, index: number) => {
    const applied = index < cursor;
    const isGroup = !!entry.children;
    return (
      <li key={entry.id} className={`history-item${applied ? '' : ' undone'}${index === cursor - 1 ? ' current' : ''}`} data-testid="history-item">
        <div className="history-row">
          {isGroup ? (
            <button
              className={`icon-btn chevron${expanded.has(entry.id) ? ' open' : ''}`}
              aria-label={expanded.has(entry.id) ? 'Collapse steps' : `Expand ${entry.children!.length} steps`}
              aria-expanded={expanded.has(entry.id)}
              onClick={() => toggle(entry.id)}
            >
              <ChevronIcon width={12} height={12} />
            </button>
          ) : (
            <span className="history-dot" aria-hidden="true" />
          )}
          <button className="history-btn" onClick={() => jumpTo(index + 1)} aria-current={index === cursor - 1 ? 'step' : undefined}>
            <span className="history-title">{entry.title}</span>
            <ActorBadge actor={entry.actor} />
          </button>
        </div>
        {isGroup && expanded.has(entry.id) && (
          <ol className="history-children">
            {entry.children!.map((c) => (
              <li key={c.id} className="history-child">
                {c.title}
              </li>
            ))}
          </ol>
        )}
      </li>
    );
  };

  return (
    <section className="panel history-panel" aria-label="History">
      <h2 className="panel-title">History</h2>
      <ol className="history-list" ref={listRef} data-testid="history-list">
        <li className={`history-item${cursor === 0 ? ' current' : ''}`} data-testid="history-origin">
          <div className="history-row">
            <span className="history-dot origin" aria-hidden="true" />
            <button className="history-btn" onClick={() => jumpTo(0)} aria-current={cursor === 0 ? 'step' : undefined}>
              <span className="history-title">{tab.fileName ? `Open ${tab.fileName}` : 'Document created'}</span>
            </button>
          </div>
        </li>
        {entries.map(row)}
      </ol>
    </section>
  );
}
