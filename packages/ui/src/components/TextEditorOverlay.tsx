import { findLayer, PatchBuilder, type TextLayer } from '@canvas-ai/core';
import { useEffect, useRef } from 'react';
import { useEditor, type DocTab } from '../store';

/** In-place text editing for the Text tool. Commits on blur or Ctrl/Cmd+Enter; Escape cancels. */
export function TextEditorOverlay({ tab }: { tab: DocTab }) {
  const session = useEditor((s) => s.textEdit);
  const view = useEditor((s) => s.views[tab.id]);
  const opts = useEditor((s) => s.toolOptions.text);
  const fg = useEditor((s) => s.colors.fg);
  const ref = useRef<HTMLTextAreaElement>(null);
  const done = useRef(false);

  const existing = session?.layerId ? (findLayer(tab.doc, session.layerId)?.layer as TextLayer | undefined) : undefined;

  useEffect(() => {
    if (!session) return;
    done.current = false;
    ref.current?.focus();
    // Hide the layer being edited so the text isn't drawn twice.
    if (existing) {
      const st = useEditor.getState();
      st.setPreview(new PatchBuilder(tab.doc).replaceLayer({ ...existing, visible: false }).doc);
    }
    // eslint-disable-next-line react-hooks/exhaustive-deps
  }, [session?.layerId, session?.x, session?.y]);

  if (!session || !view) return null;

  const style = existing?.style ?? { fontFamily: opts.fontFamily, fontSize: opts.fontSize, fontWeight: opts.fontWeight, italic: opts.italic, align: opts.align, lineHeight: 1.2, letterSpacing: 0, color: fg };
  const finish = (commit: boolean) => {
    if (done.current) return;
    done.current = true;
    const st = useEditor.getState();
    const text = ref.current?.value ?? '';
    st.setTextEdit(null);
    st.setPreview(null);
    if (!commit || !text.trim()) return;
    if (existing) {
      if (text !== existing.text) st.run('layer.updateText', { layerId: existing.id, text });
    } else {
      st.run('layer.createText', {
        text,
        x: session.x,
        y: session.y,
        style: { fontFamily: opts.fontFamily, fontSize: opts.fontSize, fontWeight: opts.fontWeight, italic: opts.italic, align: opts.align, color: fg },
        ...(tab.activeLayerId ? { above: tab.activeLayerId } : {}),
      });
      st.setTool('move');
    }
  };

  const z = view.zoom;
  const [r, g, b, a] = style.color;
  return (
    <textarea
      ref={ref}
      className="text-editor"
      data-testid="text-editor"
      aria-label="Text"
      defaultValue={session.text}
      spellCheck={false}
      style={{
        left: session.x * z + view.panX,
        top: session.y * z + view.panY,
        width: existing?.boxWidth ? existing.boxWidth * z : undefined,
        font: `${style.italic ? 'italic ' : ''}${style.fontWeight} ${style.fontSize * z}px ${style.fontFamily}`,
        lineHeight: style.lineHeight,
        letterSpacing: `${style.letterSpacing * z}px`,
        textAlign: style.align,
        color: `rgba(${r},${g},${b},${a / 255})`,
      }}
      onPointerDown={(e) => e.stopPropagation()}
      onBlur={() => finish(true)}
      onKeyDown={(e) => {
        e.stopPropagation();
        if (e.key === 'Escape') finish(false);
        else if (e.key === 'Enter' && (e.metaKey || e.ctrlKey)) finish(true);
      }}
      onInput={(e) => {
        const el = e.currentTarget;
        el.style.height = 'auto';
        el.style.height = `${el.scrollHeight}px`;
      }}
    />
  );
}
