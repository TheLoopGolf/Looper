import { useEditor } from '../store';
import { CloseIcon } from './Icons';

export function Toasts() {
  const toasts = useEditor((s) => s.toasts);
  const dismiss = useEditor((s) => s.dismissToast);
  return (
    <div className="toasts" role="status" aria-live="polite">
      {toasts.map((t) => (
        <div key={t.id} className={`toast ${t.kind}`} role={t.kind === 'error' ? 'alert' : undefined} data-testid={`toast-${t.kind}`}>
          <span>{t.message}</span>
          <button className="icon-btn" aria-label="Dismiss" onClick={() => dismiss(t.id)}>
            <CloseIcon width={12} height={12} />
          </button>
        </div>
      ))}
    </div>
  );
}
