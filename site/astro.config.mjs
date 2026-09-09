import { defineConfig } from 'astro/config';
import sitemap from '@astrojs/sitemap';

export default defineConfig({
  site: 'https://gamemode.pages.dev',
  integrations: [sitemap()],
});
