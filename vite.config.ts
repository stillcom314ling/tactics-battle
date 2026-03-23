import { defineConfig } from 'vite';

export default defineConfig({
  base: './',
  server: {
    host: true, // expose to local network for phone testing
    port: 3000,
  },
  build: {
    outDir: 'dist',
    assetsDir: 'assets',
  },
});
