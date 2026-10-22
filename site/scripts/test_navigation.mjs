// SPDX-License-Identifier: GPL-3.0-only
// Optional real-browser check using Node's built-in WebSocket/CDP, no npm additions.
import assert from 'node:assert/strict';
import fs from 'node:fs/promises';
import http from 'node:http';
import os from 'node:os';
import path from 'node:path';
import { spawn } from 'node:child_process';

const candidates = [process.env.CHROME_BIN, '/Applications/Google Chrome.app/Contents/MacOS/Google Chrome',
  'google-chrome', 'chromium', 'chromium-browser'].filter(Boolean);
let chrome;
for (const bin of candidates) {
  if (bin === process.env.CHROME_BIN && path.isAbsolute(bin)) {
    try { await fs.access(bin); chrome = bin; break; } catch { continue; }
  }
  const available = await new Promise(resolve => {
    const probe = spawn(bin, ['--version'], { stdio: 'ignore' });
    const timer = setTimeout(() => { probe.kill(); resolve(false); }, 3000);
    probe.once('error', () => { clearTimeout(timer); resolve(false); });
    probe.once('exit', code => { clearTimeout(timer); resolve(code === 0); });
  });
  if (available) { chrome = bin; break; }
}
if (!chrome) {
  console.error('SKIP browser navigation: set CHROME_BIN to a Chrome/Chromium executable');
  process.exit(77);
}
const root = path.resolve(process.argv[2] ?? 'site/dist');
const profile = await fs.mkdtemp(path.join(os.tmpdir(), 'sco-docs-browser-'));
const server = http.createServer(async (req, res) => {
  try {
    const name = decodeURIComponent(new URL(req.url, 'http://localhost').pathname);
    assert(name.startsWith('/sco-core/'));
    let file = path.resolve(root, name.slice('/sco-core/'.length));
    assert(file === root || file.startsWith(root + path.sep));
    if ((await fs.stat(file)).isDirectory()) file = path.join(file, 'index.html');
    const types = { '.js': 'text/javascript', '.css': 'text/css', '.html': 'text/html', '.json': 'application/json' };
    res.setHeader('Content-Type', types[path.extname(file)] ?? 'application/octet-stream');
    res.end(await fs.readFile(file));
  } catch { res.writeHead(404).end(); }
});
await new Promise(resolve => server.listen(0, '127.0.0.1', resolve));
const origin = `http://127.0.0.1:${server.address().port}`;
const child = spawn(chrome, ['--headless=new', '--remote-debugging-port=0', '--no-first-run',
  '--no-default-browser-check', '--disable-background-networking', `--user-data-dir=${profile}`, 'about:blank'],
  // Own process group: Chrome's helpers (renderer, GPU, crashpad) keep writing the profile after the
  // browser process exits, so teardown must end the whole group before removing the profile.
  { stdio: ['ignore', 'ignore', 'pipe'], detached: process.platform !== 'win32' });
let socket;
let failures = [];
try {
  const browserURL = await new Promise((resolve, reject) => {
    const timer = setTimeout(() => reject(Error('Chrome startup timeout')), 15000);
    let text = '';
    child.stderr.on('data', data => {
      text += data;
      const match = text.match(/DevTools listening on (ws:\/\/\S+)/);
      if (match) { clearTimeout(timer); resolve(match[1]); }
    });
    child.once('error', reject);
    child.once('exit', code => { clearTimeout(timer); reject(Error(`Chrome exited ${code}`)); });
  });
  const port = new URL(browserURL).port;
  const targets = await (await fetch(`http://127.0.0.1:${port}/json/list`)).json();
  socket = new WebSocket(targets.find(t => t.type === 'page').webSocketDebuggerUrl);
  await new Promise((resolve, reject) => { socket.onopen = resolve; socket.onerror = reject; });
  let id = 0;
  const pending = new Map();
  socket.onmessage = ({ data }) => {
    const message = JSON.parse(data);
    if (message.id && pending.has(message.id)) {
      const { resolve, reject, timer } = pending.get(message.id);
      clearTimeout(timer); pending.delete(message.id);
      if (message.error) reject(Error(JSON.stringify(message.error))); else resolve(message.result);
    }
    if (message.method === 'Runtime.exceptionThrown') failures.push(message.params.exceptionDetails.text);
    if (message.method === 'Network.requestWillBeSent') {
      const url = message.params.request.url;
      if (/^https?:/.test(url) && !url.startsWith(origin + '/')) failures.push('Nonlocal request: ' + url);
    }
  };
  const call = (method, params = {}) => new Promise((resolve, reject) => {
    const number = ++id;
    const timer = setTimeout(() => { pending.delete(number); reject(Error(method + ' timeout')); }, 10000);
    pending.set(number, { resolve, reject, timer });
    socket.send(JSON.stringify({ id: number, method, params }));
  });
  const evaluate = async expression => {
    const result = await call('Runtime.evaluate', { expression, returnByValue: true, awaitPromise: true });
    assert(!result.exceptionDetails, JSON.stringify(result.exceptionDetails));
    return result.result.value;
  };
  const wait = async expression => {
    const end = Date.now() + 10000;
    while (Date.now() < end) {
      const value = await evaluate(expression);
      if (value) return value;
      await new Promise(resolve => setTimeout(resolve, 50));
    }
    throw Error('Condition timeout: ' + expression);
  };
  const key = async (key, code, modifiers = 0) => {
    const windowsVirtualKeyCode = { Enter: 13, Tab: 9, Escape: 27, k: 75 }[key];
    await call('Input.dispatchKeyEvent', { type: 'keyDown', key, code, modifiers, windowsVirtualKeyCode });
    await call('Input.dispatchKeyEvent', { type: 'keyUp', key, code, modifiers, windowsVirtualKeyCode });
  };
  await call('Runtime.enable'); await call('Network.enable'); await call('Page.enable');
  if (process.env.SCO_DOCS_CPU_THROTTLE) {
    const rate = Number(process.env.SCO_DOCS_CPU_THROTTLE);
    assert(Number.isFinite(rate) && rate >= 1, 'SCO_DOCS_CPU_THROTTLE must be at least 1');
    await call('Emulation.setCPUThrottlingRate', { rate });
  }
  // Hold Starlight's idle initialization beyond document load to reproduce a cold
  // CI page deterministically. Only scheduling changes: the real Pagefind UI runs.
  const deferSearchIdle = process.argv.includes('--defer-search-idle');
  if (deferSearchIdle) {
    await call('Page.addScriptToEvaluateOnNewDocument', { source: `
      const requestIdle = window.requestIdleCallback.bind(window);
      const cancelIdle = window.cancelIdleCallback.bind(window);
      const callbacks = new Map();
      let nextId = 0;
      window.requestIdleCallback = callback => {
        callbacks.set(++nextId, callback);
        return nextId;
      };
      window.cancelIdleCallback = id => callbacks.delete(id);
      window.__releaseSearchIdle = () => {
        window.requestIdleCallback = requestIdle;
        window.cancelIdleCallback = cancelIdle;
        for (const callback of callbacks.values()) requestIdle(callback);
        callbacks.clear();
      };
    ` });
  }
  const searchReady = async () => {
    await wait("document.readyState === 'complete' && !!customElements.get('site-search') && !!document.querySelector('site-search [data-open-modal]:not([disabled])')");
    if (deferSearchIdle) {
      await wait("typeof window.__releaseSearchIdle === 'function'");
      assert.equal(await evaluate("!!document.querySelector('site-search input')"), false,
        'The cold-load regression must hold Pagefind initialization beyond document load');
      await evaluate('window.__releaseSearchIdle()');
    }
    // Starlight focuses the input synchronously when opening. Document load and
    // custom-element registration do not await its idle-scheduled Pagefind import.
    await wait("!!document.querySelector('site-search .pagefind-ui__search-input')");
  };
  await call('Page.navigate', { url: origin + '/sco-core/' });
  await searchReady();
  await key('k', 'KeyK', process.platform === 'darwin' ? 4 : 2);
  await wait("!!document.querySelector('dialog[open]') && document.activeElement.tagName === 'INPUT'");
  await call('Input.insertText', { text: 'schedule game thread task' });
  await wait("!!document.querySelector('.pagefind-ui__result-link')");
  await key('Tab', 'Tab'); // Clear control, then first result.
  await key('Tab', 'Tab');
  assert((await evaluate('document.activeElement.href')).endsWith('/reference/sco-api-run-on-game-thread/'));
  await key('Enter', 'Enter');
  await wait("location.pathname.endsWith('/reference/sco-api-run-on-game-thread/') && document.readyState === 'complete'");
  console.log('PASS keyboard: shortcut opens/focuses search; Tab reaches result; Enter follows stable route');
  await searchReady();
  if (deferSearchIdle) console.log('PASS cold search initialization held beyond load on both navigations');
  await key('k', 'KeyK', process.platform === 'darwin' ? 4 : 2);
  await wait("!!document.querySelector('dialog[open]')");
  await key('Escape', 'Escape');
  await wait("!document.querySelector('dialog[open]')");
  await wait("!document.querySelector('dialog').contains(document.activeElement)");
  console.log('PASS Escape closes search and returns focus outside the closed dialog');
  for (const width of [1280, 390]) {
    await call('Emulation.setDeviceMetricsOverride', { width, height: 900, deviceScaleFactor: 1, mobile: false });
    assert(await evaluate('document.documentElement.scrollWidth <= innerWidth'), `page overflow at ${width}px`);
    assert(await evaluate("!!document.querySelector('main pre') && !!document.querySelector('main table')"));
  }
  const catalogue = JSON.parse(await fs.readFile(path.join(root, 'sdk-catalogue.json')));
  const searchTest = await fs.readFile(new URL('./test_search.mjs', import.meta.url), 'utf8');
  const reports = await evaluate(`(async()=>{ ${searchTest.replace('export async function', 'async function')};
    return await testSearch(${JSON.stringify(catalogue)}); })()`);
  reports.forEach(report => console.log(report));

  const natives = JSON.parse(await fs.readFile(path.join(root, 'native-index.json')));
  const target = natives.entries[Math.floor(natives.entries.length * 0.8)];
  const other = natives.entries[1];
  const visible = "[...document.querySelectorAll('[data-sco-natives] tbody tr:not([hidden])')]";
  const targetInView = "(() => { const b = document.querySelector('tr.is-target').getBoundingClientRect(); "
    + 'return b.top >= 0 && b.bottom <= innerHeight; })()';
  await call('Emulation.setDeviceMetricsOverride', { width: 1280, height: 900, deviceScaleFactor: 1, mobile: false });
  await call('Page.navigate', { url: `${origin}/sco-core/natives/?${target.hash}` });
  await wait(`document.readyState === 'complete' && document.querySelector('tr.is-target')?.id === ${JSON.stringify(target.hash)}`);
  assert(await evaluate(targetInView), 'Deep-linked row is not scrolled into view');
  assert.equal(await evaluate("document.querySelector('tr.is-target').getAttribute('aria-current')"), 'location');
  await evaluate("(() => { const s = document.querySelector('#sco-natives-tier'); s.value = '1'; s.dispatchEvent(new Event('change')); })()");
  assert.equal(await evaluate(`${visible}.length`), natives.entries.filter(e => e.tier === 1).length);
  assert(await evaluate(`${visible}.every(r => r.dataset.tier === '1')`));
  await evaluate("(() => { const s = document.querySelector('#sco-natives-tier'); s.value = ''; s.dispatchEvent(new Event('change')); })()");
  await evaluate("document.querySelector('#sco-natives-q').focus()");
  await call('Input.insertText', { text: target.id });
  await wait(`${visible}.some(r => r.id === ${JSON.stringify(target.hash)})`);
  const matching = natives.entries.filter(e => [e.id, e.hash, e.subsystem, e.source, ...(e.capability ?? [])]
    .join(' ').toLowerCase().includes(target.id.toLowerCase()));
  assert.equal(await evaluate(`${visible}.length`), matching.length);
  assert.equal(await evaluate("document.querySelector('#sco-natives-count').textContent"),
    `Showing ${matching.length} of ${natives.entries.length} rows`);
  await evaluate("(() => { const q = document.querySelector('#sco-natives-q'); q.value = ''; q.dispatchEvent(new Event('input')); })()");
  await evaluate(`document.getElementById(${JSON.stringify(other.hash)}).querySelector('a.sco-hash').click()`);
  await wait(`location.search === ${JSON.stringify('?' + other.hash)} && document.querySelector('tr.is-target')?.id === ${JSON.stringify(other.hash)}`);
  assert(await evaluate(targetInView), 'Clicked hash row is not in view');
  for (const width of [1280, 390]) {
    await call('Emulation.setDeviceMetricsOverride', { width, height: 900, deviceScaleFactor: 1, mobile: width < 600 });
    await call('Page.navigate', { url: `${origin}/sco-core/natives/#${target.hash.toLowerCase()}` });
    await wait(`document.readyState === 'complete' && document.querySelector('tr.is-target')?.id === ${JSON.stringify(target.hash)}`);
    assert(await evaluate(targetInView), `Deep-linked row is not in view at ${width}px`);
    assert(await evaluate('document.documentElement.scrollWidth <= innerWidth'), `natives overflow at ${width}px`);
  }
  await call('Page.navigate', { url: `${origin}/sco-core/natives/?_0x0000000000000000` });
  await wait("document.readyState === 'complete' && !document.querySelector('#sco-natives-notice').hidden");
  assert.equal(await evaluate('document.querySelectorAll("tr.is-target").length'), 0);
  console.log('PASS native index: query and hash deep links scroll to and mark one row; tier and text filters '
    + 'match the JSON; hash links stay on the page; unknown hash reported; no overflow at 1280px and 390px');
  assert.deepEqual(failures, []);
  console.log('PASS long reference tables/code at 1280px and 390px; zero script exceptions or nonlocal requests');
} finally {
  if (socket) socket.close();
  const groupAlive = () => {
    if (!child.pid) return false;
    try { process.kill(-child.pid, 0); return true; } catch (error) { if (error.code === 'ESRCH') return false; throw error; }
  };
  if (process.platform === 'win32') child.kill();
  else if (groupAlive()) process.kill(-child.pid, 'SIGTERM');
  await new Promise(resolve => { if (child.exitCode !== null || child.signalCode !== null) resolve(); else child.once('exit', resolve); });
  if (process.platform !== 'win32') {
    for (let wait = 0; groupAlive(); wait++) {
      if (wait === 50) process.kill(-child.pid, 'SIGKILL');
      if (wait > 100) throw Error('Chrome helper processes outlived SIGKILL');
      await new Promise(resolve => setTimeout(resolve, 100));
    }
  }
  await new Promise(resolve => server.close(resolve));
  await fs.rm(profile, { recursive: true, force: true });
}
