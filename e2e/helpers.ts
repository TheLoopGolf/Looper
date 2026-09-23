import type { Page } from '@playwright/test';

export const mod = process.platform === 'darwin' ? 'Meta' : 'Control';

export async function newDocument(page: Page, width = 800, height = 600) {
  await page.goto('/');
  await page.getByTestId('welcome-new').click();
  await page.getByTestId('new-width').fill(String(width));
  await page.getByTestId('new-height').fill(String(height));
  await page.getByTestId('create-document').click();
  await page.getByTestId('layer-row').first().waitFor();
  await waitForRenderer(page);
}

export const waitForRenderer = (page: Page) => page.waitForFunction(() => !!(window as any).canvasAI.store.getState().renderer);

export const layerNames = (page: Page) => page.getByTestId('layer-row').evaluateAll((rows) => rows.map((r) => r.getAttribute('aria-label')));
export const historyTitles = (page: Page) =>
  page.locator('.history-list > .history-item').evaluateAll((items) => items.filter((i) => !i.classList.contains('undone')).map((i) => i.querySelector('.history-title')!.textContent));

/** Reads a composited pixel through the active renderer. */
export const pixelAt = (page: Page, x: number, y: number) =>
  page.evaluate(async ([x, y]) => {
    const r = (window as any).canvasAI.store.getState().renderer;
    return [...(await r.readComposite({ x, y, width: 1, height: 1 }))];
  }, [x, y]);

export const activeDoc = (page: Page): Promise<{ layers: { name: string; opacity: number; blendMode: string; visible: boolean }[]; width: number }> =>
  page.evaluate(() => {
    const s = (window as any).canvasAI.store.getState();
    const tab = s.tabs.find((t: any) => t.id === s.activeTabId);
    return { layers: tab.doc.layers.map((l: any) => ({ name: l.name, opacity: l.opacity, blendMode: l.blendMode, visible: l.visible })), width: tab.doc.width };
  });
