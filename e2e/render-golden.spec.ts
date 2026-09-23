import { expect, test } from '@playwright/test';

/** GPU backends must match the CPU reference compositor within 1 LSB for every blend mode and group scene. */
for (const backend of ['webgpu', 'webgl2'] as const) {
  test.describe(`${backend} golden images`, () => {
    test.beforeEach(async ({ page }) => {
      await page.goto('/harness.html');
      await page.waitForFunction(() => document.title === 'harness ready');
    });

    test('all blend modes ≤ 1 LSB vs CPU reference', async ({ page }) => {
      const results = await page.evaluate((b) => (window as any).harness.compareBlendModes(b), backend);
      expect(results).toHaveLength(26);
      for (const r of results) expect(r.max, `${r.name}: ${r.detail ?? ''}`).toBeLessThanOrEqual(1);
    });

    test('isolated and pass-through groups ≤ 1 LSB vs CPU reference', async ({ page }) => {
      const results = await page.evaluate((b) => (window as any).harness.compareGroups(b), backend);
      for (const r of results) expect(r.max, `${r.name}: ${r.detail ?? ''}`).toBeLessThanOrEqual(1);
    });
  });
}
