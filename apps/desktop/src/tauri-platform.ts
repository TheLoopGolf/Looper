import { open, save } from '@tauri-apps/plugin-dialog';
import { readFile, writeFile } from '@tauri-apps/plugin-fs';
import type { Platform } from '@canvas-ai/ui';

const fileName = (path: string) => path.split(/[\\/]/).pop() ?? path;

/** Desktop file access through Tauri's dialog + fs plugins (scoped by capabilities/default.json). */
export const tauriPlatform: Platform = {
  kind: 'desktop',
  async openFile(filters) {
    const path = await open({ multiple: false, directory: false, filters });
    if (typeof path !== 'string') return null;
    return { name: fileName(path), bytes: await readFile(path), handle: path };
  },
  async saveFile(bytes, suggested, filters) {
    const path = typeof suggested.handle === 'string' ? suggested.handle : await save({ defaultPath: suggested.name, filters });
    if (!path) return null;
    await writeFile(path, bytes);
    return { name: fileName(path), handle: path };
  },
};
