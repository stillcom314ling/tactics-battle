import { defineConfig } from 'vitest/config';

export default defineConfig({
  test: {
    globals:     true,
    environment: 'node',
    include:     ['src/tests/**/*.test.ts'],
    coverage: {
      provider: 'v8',
      reporter: ['text', 'html'],
      include:  [
        'src/core/**',
        'src/game/SpellSystem.ts',
        'src/game/RenderSystem.ts',
        'src/game/TargetingController.ts',
        'src/combat/**',
        'src/ai/**',
        'src/utils/**',
      ],
    },
  },
});
