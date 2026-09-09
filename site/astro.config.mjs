import { defineConfig } from 'astro/config';
import sitemap from '@astrojs/sitemap';

export default defineConfig({
  site: 'https://fourshades.pages.dev',
  integrations: [sitemap()],
});
