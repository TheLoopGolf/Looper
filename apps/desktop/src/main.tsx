import { App } from '@canvas-ai/ui';
import '@canvas-ai/ui/styles.css';
import { StrictMode } from 'react';
import { createRoot } from 'react-dom/client';
import { tauriPlatform } from './tauri-platform';

createRoot(document.getElementById('root')!).render(
  <StrictMode>
    <App platform={tauriPlatform} />
  </StrictMode>,
);
