import { expect, test, type Page } from '@playwright/test';
import { activeDoc, historyTitles, layerNames, mod, newDocument, pixelAt } from './helpers';

/** Drags on the canvas between document coordinates (assumes 100% zoom after Ctrl+1). */
async function drag(page: Page, from: [number, number], to: [number, number], steps = 8) {
  const [a, b] = await toScreen(page, from);
  const [c, d] = await toScreen(page, to);
  await page.mouse.move(a, b);
  await page.mouse.down();
  await page.mouse.move(c, d, { steps });
  await page.mouse.up();
}

async function toScreen(page: Page, [x, y]: [number, number]): Promise<[number, number]> {
  const box = (await page.getByTestId('canvas-host').boundingBox())!;
  const v = await page.evaluate(() => {
    const s = (window as any).canvasAI.store.getState();
    return s.views[s.activeTabId];
  });
  return [box.x + v.panX + x * v.zoom, box.y + v.panY + y * v.zoom];
}

async function click(page: Page, at: [number, number], opts: { modifiers?: ('Alt' | 'Shift')[]; clickCount?: number } = {}) {
  const [x, y] = await toScreen(page, at);
  for (const m of opts.modifiers ?? []) await page.keyboard.down(m);
  await page.mouse.click(x, y, { clickCount: opts.clickCount });
  for (const m of opts.modifiers ?? []) await page.keyboard.up(m);
}

const store = (page: Page, fn: string) => page.evaluate(`(() => { const s = window.canvasAI.store.getState(); const tab = s.tabs.find((t) => t.id === s.activeTabId); return ${fn}; })()`);

test.beforeEach(async ({ page }) => {
  await newDocument(page, 400, 300);
  await page.keyboard.press(`${mod}+1`);
});

test('brush paints with live preview, commits one undoable stroke; eraser removes', async ({ page }) => {
  await page.getByTestId('tool-brush').click();
  await drag(page, [50, 150], [350, 150], 20);
  expect(await pixelAt(page, 200, 150)).toEqual([0, 0, 0, 255]);
  expect(await historyTitles(page)).toEqual(['Document created', 'Brush Stroke']);
  await page.keyboard.press('e');
  await page.getByTestId('opt-size').fill('60');
  await drag(page, [200, 100], [200, 200]);
  expect(await pixelAt(page, 200, 150)).toEqual([0, 0, 0, 0]);
  await page.keyboard.press(`${mod}+z`);
  await page.keyboard.press(`${mod}+z`);
  expect(await pixelAt(page, 200, 150)).toEqual([255, 255, 255, 255]);
});

test('marquee + delete clears pixels; shift adds; wand and fill with foreground', async ({ page }) => {
  await page.keyboard.press('m');
  await drag(page, [10, 10], [110, 60]);
  await drag(page, [200, 200], [250, 250].map((v) => v) as [number, number]);
  // Second drag replaced the selection; shift-drag adds.
  await page.keyboard.down('Shift');
  await drag(page, [10, 10], [110, 60]);
  await page.keyboard.up('Shift');
  await page.keyboard.press('Delete');
  expect((await pixelAt(page, 50, 30))[3]).toBe(0);
  expect((await pixelAt(page, 225, 225))[3]).toBe(0);
  expect((await pixelAt(page, 150, 150))[3]).toBe(255);
  await page.keyboard.press(`${mod}+d`);
  await page.keyboard.press('w');
  await click(page, [50, 30]);
  expect(await store(page, '!!tab.doc.selection')).toBe(true);
  await page.evaluate(() => (window as any).canvasAI.store.getState().setColor('fg', [255, 0, 0, 255]));
  await page.keyboard.press('Alt+Backspace');
  expect(await pixelAt(page, 50, 30)).toEqual([255, 0, 0, 255]);
  expect(await pixelAt(page, 150, 150)).toEqual([255, 255, 255, 255]);
});

test('polygonal lasso, quick mask and select menu', async ({ page }) => {
  await page.getByTestId('tool-polyLasso').click();
  await click(page, [20, 20]);
  await click(page, [200, 20]);
  await click(page, [20, 200]);
  await page.keyboard.press('Enter');
  expect(await store(page, '!!tab.doc.selection')).toBe(true);
  await page.getByRole('button', { name: 'Select' }).click();
  await page.getByRole('menuitem', { name: 'Inverse' }).click();
  await page.keyboard.press(`${mod}+d`);
  await page.keyboard.press('q');
  await page.keyboard.press('b');
  await drag(page, [300, 250], [350, 250]);
  await page.keyboard.press('q');
  expect(await store(page, '!!tab.doc.selection')).toBe(true);
  expect(await historyTitles(page)).toContain('Quick Mask');
});

test('gradient, paint bucket, eyedropper', async ({ page }) => {
  await page.evaluate(() => (window as any).canvasAI.store.getState().setColor('fg', [0, 0, 255, 255]));
  await page.keyboard.press('g');
  await drag(page, [0, 10], [400, 10]);
  const mid = await pixelAt(page, 200, 100);
  expect(mid[2]).toBeGreaterThan(100);
  expect(mid[0]).toBeGreaterThan(100);
  await page.evaluate(() => (window as any).canvasAI.store.getState().run('pixels.fillRect', { layerId: (window as any).canvasAI.store.getState().tabs[0].activeLayerId, x: 0, y: 0, width: 400, height: 300, color: [255, 255, 255, 255] }));
  await page.evaluate(() => (window as any).canvasAI.store.getState().setColor('fg', [0, 160, 0, 255]));
  await page.keyboard.press('k');
  await click(page, [100, 100]);
  expect(await pixelAt(page, 300, 250)).toEqual([0, 160, 0, 255]);
  await page.evaluate(() => (window as any).canvasAI.store.getState().setColor('fg', [1, 2, 3, 255]));
  await page.keyboard.press('i');
  await click(page, [10, 10]);
  await expect.poll(() => page.evaluate(() => (window as any).canvasAI.store.getState().colors.fg)).toEqual([0, 160, 0, 255]);
});

test('shape and text tools create editable layers', async ({ page }) => {
  await page.keyboard.press('u');
  await drag(page, [20, 20], [120, 80]);
  expect((await activeDoc(page)).layers.map((l) => l.name)).toContain('Rectangle');
  expect(await pixelAt(page, 70, 50)).toEqual([0, 0, 0, 255]);
  await page.keyboard.press('t');
  await click(page, [150, 200]);
  await page.getByTestId('text-editor').fill('Hello');
  await page.keyboard.press(`${mod}+Enter`);
  expect(await layerNames(page)).toContain('Hello');
  const t = await store(page, 'tab.doc.layers.find((l) => l.type === "text")');
  expect(t).toMatchObject({ text: 'Hello' });
  // Text renders through the browser font engine: some dark pixels near the click point.
  const area = await page.evaluate(async () => [...(await (window as any).canvasAI.store.getState().renderer.readComposite({ x: 140, y: 140, width: 120, height: 80 }))]);
  expect(Math.min(...area.filter((_: number, i: number) => i % 4 === 0))).toBeLessThan(100);
});

test('move tool drags a layer; free transform scales it; crop to 1:1', async ({ page }) => {
  await page.evaluate(() => {
    const s = (window as any).canvasAI.store.getState();
    const { layerId } = s.run('layer.create', { name: 'Box' });
    s.run('pixels.fillRect', { layerId, x: 50, y: 50, width: 40, height: 40, color: [200, 0, 0, 255] });
  });
  await page.keyboard.press('v');
  await drag(page, [60, 60], [110, 80]);
  expect(await pixelAt(page, 110, 80)).toEqual([200, 0, 0, 255]);
  expect(await historyTitles(page)).toContain('Move');
  await page.keyboard.press(`${mod}+t`);
  // Drag the bottom-right handle of the 40×40 box (now at 100,70) to double its size.
  await drag(page, [140, 110], [180, 150]);
  await page.keyboard.press('Enter');
  expect(await historyTitles(page)).toContain('Free Transform');
  expect(await pixelAt(page, 170, 140)).toEqual([200, 0, 0, 255]);
  await page.keyboard.press('c');
  await page.getByTestId('crop-aspect').selectOption('1:1');
  await drag(page, [10, 10], [210, 150]);
  await page.keyboard.press('Enter');
  const doc = await activeDoc(page);
  expect(doc.width).toBe(200);
  expect(await store(page, 'tab.doc.height')).toBe(200);
});

test('adjustment layers from the menu, edited in Properties; masks painted with the brush', async ({ page }) => {
  await page.getByRole('button', { name: 'Layer', exact: true }).click();
  await page.getByRole('menuitem', { name: 'New Adjustment Layer' }).click();
  await page.getByTestId('new-adjust-invert').click();
  expect(await pixelAt(page, 5, 5)).toEqual([0, 0, 0, 255]);
  await expect(page.getByTestId('adjustment-props')).toContainText('Invert');
  await page.keyboard.press(`${mod}+z`);
  await page.getByRole('button', { name: 'Layer', exact: true }).click();
  await page.getByRole('menuitem', { name: 'New Adjustment Layer' }).click();
  await page.getByTestId('new-adjust-posterize').click();
  await page.getByTestId('adjustment-props').getByLabel('Levels', { exact: true }).fill('2');
  expect(await store(page, 'tab.doc.layers[1].adjustment.params.levels')).toBe(2);
  // Paint black on the background's mask: the area becomes transparent.
  await page.getByTestId('layer-row').filter({ hasText: 'Background' }).click();
  await page.getByTestId('add-mask').click();
  await page.getByTestId('edit-mask').check();
  await page.keyboard.press('b');
  await page.getByTestId('opt-size').fill('40');
  await drag(page, [100, 100], [300, 100]);
  expect((await pixelAt(page, 200, 100))[3]).toBe(0);
  expect((await pixelAt(page, 200, 250))[3]).toBe(255);
  expect(await historyTitles(page)).toContain('Paint Mask');
});

test('filter and direct adjustment dialogs preview live and apply on OK', async ({ page }) => {
  await page.evaluate(() => {
    const s = (window as any).canvasAI.store.getState();
    s.run('pixels.fillRect', { layerId: s.tabs[0].activeLayerId, x: 0, y: 0, width: 200, height: 300, color: [0, 0, 0, 255] });
  });
  await page.getByRole('button', { name: 'Filter' }).click();
  await page.getByTestId('filter-gaussianBlur').click();
  await expect(page.getByTestId('params-dialog')).toBeVisible();
  await page.getByTestId('params-dialog').getByRole('spinbutton').fill('20');
  // Live preview blurs the edge before committing.
  await expect.poll(async () => (await pixelAt(page, 200, 150))[0], { timeout: 5000 }).toBeLessThan(250);
  expect(await historyTitles(page)).toEqual(['Document created', 'Fill Rectangle']);
  await page.getByTestId('params-ok').click();
  await expect.poll(() => historyTitles(page)).toEqual(['Document created', 'Fill Rectangle', 'Gaussian Blur']);
  await page.getByRole('button', { name: 'Image' }).click();
  await page.getByRole('menuitem', { name: 'Adjustments' }).click();
  await page.getByRole('menuitem', { name: 'Invert…' }).click();
  expect(await pixelAt(page, 50, 150)).toEqual([255, 255, 255, 255]);
});

test('export layers as a zip', async ({ page }) => {
  await page.addInitScript(() => ((window as any).showSaveFilePicker = undefined));
  await page.evaluate(() => ((window as any).showSaveFilePicker = undefined));
  await page.evaluate(() => (window as any).canvasAI.store.getState().run('layer.create', { name: 'Top', color: [9, 9, 9, 255] }));
  await page.keyboard.press(`${mod}+Shift+e`);
  const download = page.waitForEvent('download');
  await page.getByTestId('export-layers').click();
  expect((await download).suggestedFilename()).toBe('Untitled-layers.zip');
});
