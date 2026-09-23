import { defineConfig } from 'vitest/config';

export default defineConfig({
  test: {
    include: ['packages/*/test/**/*.test.ts'],
    environment: 'node',
    // The leak test measures heap usage and needs explicit GC.
    pool: 'forks',
    poolOptions: { forks: { execArgv: ['--expose-gc'] } },
    testTimeout: 30000,
  },
});
