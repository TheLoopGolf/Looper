import { expect, test } from '@playwright/test';
import { layerNames } from './helpers';

test('unsaved work is autosaved and offered for recovery after a crash/reload', async ({ page }) => {
  await page.goto('/?autosave=300');
  await page.evaluate(() => indexedDB.deleteDatabase('canvas-ai-recovery'));
  await page.getByTestId('welcome-new').click();
  await page.getByTestId('create-document').click();
  await page.evaluate(() => {
    const s = (window as any).canvasAI.store.getState();
    s.run('layer.create', { name: 'Important work', color: [10, 200, 30, 255] });
  });
  // Wait for an autosave, then "crash" (reload without saving).
  await page.waitForTimeout(1500);
  await page.reload();
  await expect(page.getByTestId('recovery-dialog')).toBeVisible();
  await page.getByTestId('recovery-restore').click();
  await expect(page.getByRole('tab', { name: /recovered/ })).toBeVisible();
  expect(await layerNames(page)).toEqual(['Important work', 'Background']);
  // Restored entries are removed: a second reload offers nothing.
  await page.reload();
  await page.waitForTimeout(500);
  await expect(page.getByTestId('recovery-dialog')).not.toBeVisible();
});
