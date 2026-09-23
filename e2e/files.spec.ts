import { readFileSync } from 'node:fs';
import { expect, test } from '@playwright/test';
import { layerNames, mod, newDocument, pixelAt, waitForRenderer } from './helpers';

// Force the <input>/download fallbacks so Playwright can drive file dialogs.
test.beforeEach(async ({ page }) => {
  await page.addInitScript(() => {
    delete (window as any).showOpenFilePicker;
    delete (window as any).showSaveFilePicker;
    (window as any).showOpenFilePicker = undefined;
    (window as any).showSaveFilePicker = undefined;
  });
});

test('save as .cnva and reopen with layers, blend modes and pixels intact', async ({ page }) => {
  await newDocument(page, 300, 200);
  await page.evaluate(() => {
    const s = (window as any).canvasAI.store.getState();
    const { layerId } = s.run('layer.create', { name: 'Blue', color: [0, 0, 255, 255] });
    s.run('pixels.fillRect', { layerId, x: 0, y: 0, width: 100, height: 200, color: [0, 0, 0, 0] });
    s.run('layer.setBlendMode', { layerId, blendMode: 'screen' });
  });
  const before = await pixelAt(page, 150, 100);

  const downloadPromise = page.waitForEvent('download');
  await page.keyboard.press(`${mod}+s`);
  const download = await downloadPromise;
  expect(download.suggestedFilename()).toBe('Untitled.cnva');
  const path = await download.path();

  const chooserPromise = page.waitForEvent('filechooser');
  await page.keyboard.press(`${mod}+o`);
  const chooser = await chooserPromise;
  await chooser.setFiles({ name: 'Untitled.cnva', mimeType: 'application/octet-stream', buffer: readFileSync(path) });
  await expect(page.getByRole('tab')).toHaveCount(2);
  await waitForRenderer(page);
  expect(await layerNames(page)).toEqual(['Blue', 'Background']);
  expect(await pixelAt(page, 150, 100)).toEqual(before);
  expect(await pixelAt(page, 50, 100)).toEqual([255, 255, 255, 255]);
  await expect(page.getByTestId('history-origin')).toContainText('Open Untitled.cnva');
});

test('open a PNG by dropping it and export PNG/JPEG', async ({ page }) => {
  await page.goto('/');
  // Build a PNG in the page with the browser encoder: left half red, right half 50% green.
  const png = await page.evaluate(async () => {
    const c = new OffscreenCanvas(64, 32);
    const g = c.getContext('2d')!;
    g.fillStyle = 'rgb(255,0,0)';
    g.fillRect(0, 0, 32, 32);
    g.fillStyle = 'rgba(0,255,0,0.5)';
    g.fillRect(32, 0, 32, 32);
    return [...new Uint8Array(await (await c.convertToBlob({ type: 'image/png' })).arrayBuffer())];
  });
  const dataTransfer = await page.evaluateHandle((bytes) => {
    const dt = new DataTransfer();
    dt.items.add(new File([new Uint8Array(bytes)], 'dropped.png', { type: 'image/png' }));
    return dt;
  }, png);
  await page.locator('main.center').dispatchEvent('drop', { dataTransfer });
  await expect(page.getByRole('tab', { name: /dropped/ })).toBeVisible();
  await waitForRenderer(page);
  expect(await layerNames(page)).toEqual(['Background']);
  expect(await pixelAt(page, 5, 5)).toEqual([255, 0, 0, 255]);
  const green = await pixelAt(page, 40, 5);
  expect(green[1]).toBe(255);
  expect(Math.abs(green[3] - 128)).toBeLessThanOrEqual(1);

  const pngDownload = page.waitForEvent('download');
  await page.keyboard.press(`${mod}+Shift+e`);
  expect((await pngDownload).suggestedFilename()).toBe('dropped.png');

  const jpegDownload = page.waitForEvent('download');
  await page.getByRole('button', { name: 'File' }).click();
  await page.getByRole('menuitem', { name: /Export JPEG/ }).click();
  const jpeg = await jpegDownload;
  expect(jpeg.suggestedFilename()).toBe('dropped.jpg');
  const bytes = readFileSync(await jpeg.path());
  expect([bytes[0], bytes[1]]).toEqual([0xff, 0xd8]);
});
