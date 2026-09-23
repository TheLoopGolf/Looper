/** Host services that differ between the browser and the Tauri desktop shell. */
export interface OpenedFile {
  name: string;
  bytes: Uint8Array;
  /** Opaque handle for "Save" to write back to the same place (path on desktop, FileSystemFileHandle on web). */
  handle?: unknown;
}

export interface SaveTarget {
  name: string;
  handle?: unknown;
}

export interface FileFilter {
  name: string;
  extensions: string[];
}

export interface Platform {
  readonly kind: 'web' | 'desktop';
  openFile(filters: FileFilter[]): Promise<OpenedFile | null>;
  /** Writes bytes; asks for a location unless `target.handle` is given. Returns the saved target or null if cancelled. */
  saveFile(bytes: Uint8Array, suggested: SaveTarget, filters: FileFilter[]): Promise<SaveTarget | null>;
}

type FilePickerWindow = Window & {
  showOpenFilePicker?: (opts: unknown) => Promise<FileSystemFileHandle[]>;
  showSaveFilePicker?: (opts: unknown) => Promise<FileSystemFileHandle>;
};

const mimeFor = (ext: string) =>
  ({ png: 'image/png', jpg: 'image/jpeg', jpeg: 'image/jpeg', webp: 'image/webp', cnva: 'application/x-canvas-ai' })[ext] ?? 'application/octet-stream';

function pickerTypes(filters: FileFilter[]) {
  return filters.map((f) => ({
    description: f.name,
    accept: { [mimeFor(f.extensions[0])]: f.extensions.map((e) => `.${e}`) },
  }));
}

/** Browser implementation: File System Access API where available, <input>/download fallback otherwise. */
export const webPlatform: Platform = {
  kind: 'web',
  async openFile(filters) {
    const w = window as FilePickerWindow;
    if (w.showOpenFilePicker) {
      try {
        const [handle] = await w.showOpenFilePicker({ types: pickerTypes(filters), multiple: false });
        const file = await handle.getFile();
        return { name: file.name, bytes: new Uint8Array(await file.arrayBuffer()), handle };
      } catch (e) {
        if ((e as DOMException).name === 'AbortError') return null;
        throw e;
      }
    }
    return new Promise((resolve) => {
      const input = document.createElement('input');
      input.type = 'file';
      input.accept = filters.flatMap((f) => f.extensions.map((e) => `.${e}`)).join(',');
      input.onchange = async () => {
        const file = input.files?.[0];
        resolve(file ? { name: file.name, bytes: new Uint8Array(await file.arrayBuffer()) } : null);
      };
      input.oncancel = () => resolve(null);
      input.click();
    });
  },
  async saveFile(bytes, suggested, filters) {
    const w = window as FilePickerWindow;
    let handle = suggested.handle as FileSystemFileHandle | undefined;
    if (!handle && w.showSaveFilePicker) {
      try {
        handle = await w.showSaveFilePicker({ suggestedName: suggested.name, types: pickerTypes(filters) });
      } catch (e) {
        if ((e as DOMException).name === 'AbortError') return null;
        throw e;
      }
    }
    if (handle) {
      const writable = await handle.createWritable();
      await writable.write(bytes as Uint8Array<ArrayBuffer>);
      await writable.close();
      return { name: handle.name, handle };
    }
    const ext = suggested.name.split('.').pop() ?? '';
    const url = URL.createObjectURL(new Blob([bytes as Uint8Array<ArrayBuffer>], { type: mimeFor(ext) }));
    const a = document.createElement('a');
    a.href = url;
    a.download = suggested.name;
    a.click();
    setTimeout(() => URL.revokeObjectURL(url), 10_000);
    return { name: suggested.name };
  },
};
