import { expect, test } from '@playwright/test';
import { activeDoc, historyTitles, layerNames, mod, newDocument, pixelAt } from './helpers';

test('create a document, add and reorder layers, undo/redo from keyboard', async ({ page }) => {
  await newDocument(page);
  await expect(page.getByTestId('render-stats')).toContainText(/WEBGPU|WEBGL2|CPU/);
  expect(await layerNames(page)).toEqual(['Background']);
  expect(await pixelAt(page, 10, 10)).toEqual([255, 255, 255, 255]);

  await page.getByTestId('new-layer').click();
  await page.getByTestId('new-layer').click();
  expect(await layerNames(page)).toEqual(['Layer 2', 'Layer 1', 'Background']);

  // Move "Layer 2" (active) down one step.
  await page.keyboard.press(`${mod}+BracketLeft`);
  expect(await layerNames(page)).toEqual(['Layer 1', 'Layer 2', 'Background']);
  expect(await historyTitles(page)).toEqual(['Document created', 'New Layer', 'New Layer', 'Move Layer']);

  await page.keyboard.press(`${mod}+z`);
  expect(await layerNames(page)).toEqual(['Layer 2', 'Layer 1', 'Background']);
  await page.keyboard.press(`${mod}+z`);
  await page.keyboard.press(`${mod}+z`);
  expect(await layerNames(page)).toEqual(['Background']);
  await page.keyboard.press(`${mod}+Shift+z`);
  await page.keyboard.press(`${mod}+Shift+z`);
  await page.keyboard.press(`${mod}+Shift+z`);
  expect(await layerNames(page)).toEqual(['Layer 1', 'Layer 2', 'Background']);
});

test('visibility, opacity and blend mode are applied to the render and undoable', async ({ page }) => {
  await newDocument(page, 300, 300);
  // Fill a red layer through the same command API agents and plugins use.
  await page.evaluate(() => (window as any).canvasAI.store.getState().run('layer.create', { name: 'Red', color: [255, 0, 0, 255] }));
  expect(await pixelAt(page, 5, 5)).toEqual([255, 0, 0, 255]);

  await page.getByTestId('opacity').fill('50');
  await expect.poll(() => pixelAt(page, 5, 5)).toEqual([255, 128, 128, 255]);

  await page.getByTestId('blend-mode').selectOption('multiply');
  // Multiply over white leaves the source color; at 50% it's halfway between.
  await expect.poll(() => pixelAt(page, 5, 5)).toEqual([255, 128, 128, 255]);
  await page.getByTestId('blend-mode').selectOption('difference');
  await expect.poll(() => pixelAt(page, 5, 5)).toEqual([128, 255, 255, 255]);

  const row = page.getByTestId('layer-row').filter({ hasText: 'Red' });
  await row.getByTestId('visibility-toggle').click();
  await expect.poll(() => pixelAt(page, 5, 5)).toEqual([255, 255, 255, 255]);
  expect((await activeDoc(page)).layers[1]).toMatchObject({ visible: false, blendMode: 'difference', opacity: 0.5 });

  await page.keyboard.press(`${mod}+z`); // show again
  await page.keyboard.press(`${mod}+z`); // back to multiply
  await expect.poll(() => pixelAt(page, 5, 5)).toEqual([255, 128, 128, 255]);
  expect((await activeDoc(page)).layers[1]).toMatchObject({ visible: true, blendMode: 'multiply' });
});

test('history panel jumps between states and shows grouped agent runs', async ({ page }) => {
  await newDocument(page, 200, 200);
  await page.evaluate(() => {
    const s = (window as any).canvasAI.store.getState();
    const tab = s.tabs.find((t: any) => t.id === s.activeTabId);
    tab.bus.beginGroup('Warm up the tones', { kind: 'agent', name: 'Color Agent', runId: 'run1' });
    const { layerId } = tab.bus.dispatch('layer.create', { name: 'Warmth', color: [255, 140, 0, 255] });
    tab.bus.dispatch('layer.setBlendMode', { layerId, blendMode: 'softLight' });
    tab.bus.dispatch('layer.setOpacity', { layerId, opacity: 40 });
    tab.bus.endGroup();
  });
  const items = page.getByTestId('history-item');
  await expect(items).toHaveCount(1);
  await expect(items.first()).toContainText('Warm up the tones');
  await expect(items.first()).toContainText('Color Agent');
  await items.first().getByRole('button', { name: /Expand 3 steps/ }).click();
  await expect(page.locator('.history-child')).toHaveText(['New Layer', 'Blend Mode: softLight', 'Opacity 40%']);

  // One undo removes the whole agent run.
  await page.keyboard.press(`${mod}+z`);
  expect(await layerNames(page)).toEqual(['Background']);
  await page.getByRole('button', { name: 'Warm up the tones' }).click();
  expect(await layerNames(page)).toEqual(['Warmth', 'Background']);
  await page.getByRole('button', { name: 'Document created' }).click();
  expect(await layerNames(page)).toEqual(['Background']);
});

test('invalid commands surface readable errors and leave the document untouched', async ({ page }) => {
  await newDocument(page, 100, 100);
  await page.evaluate(() => (window as any).canvasAI.store.getState().run('layer.setOpacity', { layerId: 'nope', opacity: 10 }));
  await expect(page.getByTestId('toast-error')).toContainText('Layer not found');
  expect(await historyTitles(page)).toEqual(['Document created']);
});

test('groups: create via shortcut, rename inline, drag a layer into a group', async ({ page }) => {
  await newDocument(page, 256, 256);
  await page.getByTestId('new-layer').click();
  await page.keyboard.press(`${mod}+g`);
  expect(await layerNames(page)).toEqual(['Group 1', 'Layer 1', 'Background']);

  await page.getByTestId('layer-row').filter({ hasText: 'Group 1' }).dblclick();
  await page.getByLabel('Layer name').fill('Foreground');
  await page.keyboard.press('Enter');
  expect(await layerNames(page)).toEqual(['Foreground', 'Layer 1', 'Background']);

  const bg = page.getByTestId('layer-row').filter({ hasText: 'Background' });
  const group = page.getByTestId('layer-row').filter({ hasText: 'Foreground' });
  await bg.dragTo(group, { targetPosition: { x: 60, y: 15 } });
  const doc = await activeDoc(page);
  expect(doc.layers.map((l) => l.name)).toEqual(['Foreground']);
  // Dropped "into" a group lands on top of its children.
  expect(await layerNames(page)).toEqual(['Foreground', 'Background', 'Layer 1']);
});

test('pan and zoom via keyboard and wheel', async ({ page }) => {
  await newDocument(page, 2000, 1500);
  const zoom = page.getByTestId('zoom-level');
  const initial = await zoom.textContent();
  await page.keyboard.press(`${mod}+1`);
  await expect(zoom).toHaveText('100%');
  await page.keyboard.press(`${mod}+Equal`);
  await expect(zoom).toHaveText('150%');
  await page.keyboard.press(`${mod}+0`);
  await expect(zoom).toHaveText(initial!);
  const host = page.getByTestId('canvas-host');
  await host.hover();
  await page.keyboard.down(mod);
  await page.mouse.wheel(0, -200);
  await page.keyboard.up(mod);
  await expect(zoom).not.toHaveText(initial!);
});
