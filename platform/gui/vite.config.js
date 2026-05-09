import { defineConfig } from 'vite';
export default defineConfig({
  server: {
    port: 3001, host: true,
    proxy: {
      // /api/* → backend port 8000 (strip /api prefix)
      '/api': {
        target: 'http://localhost:8000',
        changeOrigin: true,
        rewrite: path => path.replace(/^\/api/, ''),
      },
      '/ctrl':      { target: 'http://localhost:8001', changeOrigin: true },
      '/xapp-start':{ target: 'http://localhost:38868', changeOrigin: true, rewrite: () => '/' },
      '/xapp-stop': { target: 'http://localhost:38869', changeOrigin: true, rewrite: () => '/' },
    },
  },
  preview: { port: 3001, host: true },
});
