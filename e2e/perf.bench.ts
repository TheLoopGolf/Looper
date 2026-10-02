import { test } from '@playwright/test';

/**
 * M1 acceptance benchmark: 6000×4000 document, 20 layers, pan/zoom frame times.
 * Run with `pnpm bench`. Numbers from headless SwiftShader (software GPU) are
 * not representative of real hardware; run headed on a real GPU for sign-off.
 */
for (const backend of ['webgpu', 'webgl2'] as const) {
  test(`6000x4000 × 20 layers pan/zoom (${backend})`, async ({ page }) => {
    await page.goto('/harness.html');
    await page.waitForFunction(() => document.title === 'harness ready');
    const r = await page.evaluate((b) => (window as any).harness.benchmark(b, { width: 6000, height: 4000, layers: 20, frames: 120 }), backend);
    console.log(JSON.stringify(r, null, 2));
  });
}

for (const backend of ['webgpu', 'webgl2'] as const) {
  test(`brush latency on 6000x4000 × 20 layers (${backend})`, async ({ page }) => {
    await page.goto('/harness.html');
    await page.waitForFunction(() => document.title === 'harness ready');
    const r = await page.evaluate((b) => (window as any).harness.brushLatency(b, { width: 6000, height: 4000, layers: 20, events: 120 }), backend);
    console.log(JSON.stringify(r, null, 2));
  });
}
