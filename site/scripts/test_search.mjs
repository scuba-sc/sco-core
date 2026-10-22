// SPDX-License-Identifier: GPL-3.0-only
// Invoked inside the real browser by test_navigation.mjs. Pagefind's browser
// worker is exercised; Node's unsupported non-worker fallback is not a proxy.
export async function testSearch(catalogue, normalizeTaskQuery) {
  const pagefind = await import('/sco-core/pagefind/pagefind.js');
  const cases = [
    ['sco.storage', 'services/sco-storage'],
    ['sco_api.run_on_game_thread', 'reference/sco-api-run-on-game-thread'],
    ['TaskTrampoline', 'reference/sco-api-run-on-game-thread'],
    ['schedule game thread task', 'reference/sco-api-run-on-game-thread'],
    ['open SQL cursor', 'reference/sco-storage-v1-query'],
    ['find service table', 'reference/sco-api-query-service'],
    ['spawn NPC', 'reference/sc-actors-v1-spawn-npc'],
    ['spawn a ship', 'reference/sc-spawn-service-v1-spawn-near-player', 3],
    ['spwan ship', 'reference/sc-spawn-service-v1-spawn-near-player', 3],
    ['how do I spawn a ship', 'reference/sc-spawn-service-v1-spawn-near-player', 3],
    ['read inventory', 'reference/sc-inventory-v1-snapshot', 3],
    ['read inventroy', 'reference/sc-inventory-v1-snapshot', 3],
    ['how can I read inventory', 'reference/sc-inventory-v1-snapshot', 3],
    ['read container contents', 'reference/sc-container-contents-v1-snapshot', 3],
  ];
  const reports = [];
  for (const [query, route, limit = 20] of cases) {
    const results = await pagefind.search(normalizeTaskQuery(query));
    const entries = await Promise.all(results.results.slice(0, limit).map(r => r.data()));
    if (!entries.some(r => new URL(r.url, location.origin).pathname === `/sco-core/${route}/`)) {
      throw Error(`${query}: intended entry absent from first ${limit} results: ${entries.map(r => r.url)}`);
    }
    const html = await (await fetch(`/sco-core/${route}/`)).text();
    if (!html.includes(catalogue.source_sha) || !html.includes('development preview')) {
      throw Error('Result lost snapshot identity or mixed preview/release');
    }
    if (route.startsWith('reference/')) {
      for (const heading of ['Absent capability', 'Language mappings', 'Examples']) {
        if (!html.includes(heading)) throw Error('Missing result section: ' + heading);
      }
      const record = catalogue.records.find(r => r.id === route.slice('reference/'.length));
      for (const mapping of Object.values(record.language_mappings)) {
        if (!html.includes(mapping.status)) throw Error('Missing catalogue language status');
      }
      for (const example of record.examples) {
        if (!html.includes(`/blob/${catalogue.source_sha}/${example}`)) throw Error('Unpinned result example');
      }
    }
    reports.push(`PASS query ${JSON.stringify(query)} -> ${route}`);
  }
  const missing = await pagefind.search('qzxzzvneverdocumentedalias');
  // Pagefind may return approximate matches for an unknown query. A search hit
  // must never invent an API alias or lead outside the current static snapshot.
  for (const result of missing.results) {
    const data = await result.data();
    const url = new URL(data.url, location.origin);
    if (url.origin !== location.origin || !url.pathname.startsWith('/sco-core/')) {
      throw Error('Unknown query escaped the current site');
    }
    const response = await fetch(url);
    if (!response.ok || !(await response.text()).includes(catalogue.source_sha)) {
      throw Error('Unknown query hit is stale or dead');
    }
  }
  reports.push(`PASS unknown query: ${missing.results.length} approximate hits resolve to current snapshot (not API aliases)`);
  return reports;
}
