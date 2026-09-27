import { App, createSampleDocument, useEditor, webPlatform } from '@canvas-ai/ui';
import '@canvas-ai/ui/styles.css';
import { StrictMode } from 'react';
import { createRoot } from 'react-dom/client';

// Test/debug hook (used by Playwright). Harmless in production: exposes only the in-memory editor store.
if (import.meta.env.DEV || new URLSearchParams(location.search).has('debug')) {
  (window as unknown as { canvasAI: unknown }).canvasAI = { store: useEditor };
}

// Preview builds (VITE_OPEN_SAMPLE=1) and #sample links start with the sample document open.
if (import.meta.env.VITE_OPEN_SAMPLE === '1' || location.hash === '#sample') {
  useEditor.getState().openDocument(createSampleDocument());
}

const app = <App platform={webPlatform} />;
// ?nostrict disables StrictMode's double effects (debugging GPU resource lifetimes).
createRoot(document.getElementById('root')!).render(new URLSearchParams(location.search).has('nostrict') ? app : <StrictMode>{app}</StrictMode>);
