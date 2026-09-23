import { defineConfig, devices } from '@playwright/test';

/**
 * Headless Chromium needs Vulkan SwiftShader for WebGPU canvas presentation;
 * without it presenting a WebGPU frame loses the GPU instance.
 */
export const GPU_ARGS = ['--enable-unsafe-webgpu', '--use-angle=swiftshader', '--enable-features=Vulkan', '--use-vulkan=swiftshader'];

export default defineConfig({
  testDir: './e2e',
  testMatch: /.*\.spec\.ts/,
  timeout: 60_000,
  fullyParallel: false,
  workers: 1,
  reporter: [['list']],
  use: {
    baseURL: 'http://localhost:5173',
    viewport: { width: 1280, height: 800 },
    launchOptions: { args: GPU_ARGS },
    trace: 'retain-on-failure',
  },
  projects: [{ name: 'chromium', use: { ...devices['Desktop Chrome'], launchOptions: { args: GPU_ARGS } } }],
  webServer: {
    command: 'pnpm --filter @canvas-ai/web dev',
    url: 'http://localhost:5173',
    reuseExistingServer: true,
    timeout: 60_000,
  },
});
