import { defineConfig } from 'astro/config';
import starlight from '@astrojs/starlight';

export default defineConfig({
  site: 'https://scubamount.github.io',
  base: '/sco-core',
  trailingSlash: 'always',
  integrations: [starlight({
    title: 'sco-core SDK',
    description: 'Public SDK contracts and engineering guides',
    sidebar: [
      { label: 'Overview', slug: 'index' },
      { label: 'Services', items: [{ autogenerate: { directory: 'services' } }] },
      { label: 'Engineering guides', items: [{ autogenerate: { directory: 'guides' } }] },
    ],
    // Starlight builds its default local Pagefind search index; no remote provider.
  })],
});
