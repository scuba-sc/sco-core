import { readFileSync } from 'node:fs';
import { defineConfig } from 'astro/config';
import starlight from '@astrojs/starlight';

// Home plus the four library hubs, written by scripts/gen.py next to the pages it generates.
const sidebar = JSON.parse(readFileSync(new URL('./src/generated-sidebar.json', import.meta.url), 'utf8'));

export default defineConfig({
  site: 'https://scubamount.github.io',
  base: '/sco-core',
  trailingSlash: 'always',
  integrations: [starlight({
    title: 'sco-core SDK',
    description: 'Public SDK contracts and engineering guides',
    customCss: ['./src/styles/portal.css'],
    sidebar,
    // Starlight builds its default local Pagefind search index; no remote provider.
  })],
});
