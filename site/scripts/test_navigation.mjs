// SPDX-License-Identifier: GPL-3.0-only
// Optional real-browser check using Node's built-in WebSocket/CDP, no npm additions.
import assert from 'node:assert/strict';
import fs from 'node:fs/promises';
import http from 'node:http';
import os from 'node:os';
import path from 'node:path';
import { spawn, spawnSync } from 'node:child_process';

const candidates = [process.env.CHROME_BIN, '/Applications/Google Chrome.app/Contents/MacOS/Google Chrome',
  'google-chrome', 'chromium', 'chromium-browser'].filter(Boolean);
const chrome = candidates.find(bin => spawnSync(bin, ['--version']).status === 0);
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
  { stdio: ['ignore', 'ignore', 'pipe'] });
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
  await call('Page.navigate', { url: origin + '/sco-core/' });
  await wait("document.readyState === 'complete' && !!document.querySelector('site-search')");
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
  assert.deepEqual(failures, []);
  console.log('PASS long reference tables/code at 1280px and 390px; zero script exceptions or nonlocal requests');
} finally {
  if (socket) socket.close();
  child.kill();
  await new Promise(resolve => { if (child.exitCode !== null) resolve(); else child.once('exit', resolve); });
  await new Promise(resolve => server.close(resolve));
  await fs.rm(profile, { recursive: true, force: true });
}
