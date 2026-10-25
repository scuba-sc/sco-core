// SPDX-License-Identifier: GPL-3.0-only
// Runs against the built site in the navigation runner's real Chrome session.
import assert from 'node:assert/strict';
import fs from 'node:fs/promises';
import path from 'node:path';

// Observe the native worker and scheduler without adding production diagnostics.
// Arrays are bounded, and no transferred buffer is retained by this probe.
const probeSource = `(() => {
  const probe = window.__scoFieldProbe = { workers: [], raf: 0, longTasks: [], persisted: [], mediaChanges: [] };
  const nativeMatchMedia = window.matchMedia.bind(window);
  const media = window.__scoFieldMedia = [];
  window.matchMedia = query => {
    const list = nativeMatchMedia(query);
    if (query !== '(prefers-reduced-motion: reduce)') return list;
    const record = { list, adds: [], removes: [], calls: [] };
    media.push(record);
    if (media.length > 32) media.shift();
    const handlers = new WeakMap();
    const add = list.addEventListener.bind(list);
    const remove = list.removeEventListener.bind(list);
    const label = handler => handler.name || handler.handleEvent?.name || '(anonymous)';
    list.addEventListener = (type, handler, options) => {
      if (type !== 'change' || !handler) return add(type, handler, options);
      record.adds.push(label(handler));
      record.adds = record.adds.slice(-64);
      if (!handlers.has(handler)) handlers.set(handler, function(event) {
        record.calls.push({ handler: label(handler), matches: event.matches, at: performance.now() });
        record.calls = record.calls.slice(-64);
        probe.mediaChanges.push(event.matches);
        if (typeof handler === 'function') return handler.call(list, event);
        return handler.handleEvent(event);
      });
      return add(type, handlers.get(handler), options);
    };
    list.removeEventListener = (type, handler, options) => {
      if (type === 'change' && handler) {
        record.removes.push(label(handler));
        record.removes = record.removes.slice(-64);
      }
      return remove(type, handlers.get(handler) || handler, options);
    };
    return list;
  };
  const NativeWorker = window.Worker;
  window.Worker = class extends NativeWorker {
    constructor(url, options) {
      const field = options?.name === 'sco-core-particle-field';
      if (field && sessionStorage.getItem('sco-field-test') === 'construction') {
        throw new DOMException('Injected worker construction failure', 'SecurityError');
      }
      super(url, options);
      if (!field) return;
      const record = { url: String(url), started: performance.now(), count: 0, sent: 0,
        received: 0, inFlight: 0, maxInFlight: 0, bytes: [], transfers: [], times: [],
        phases: [], terminated: false, readyMs: null, firstFrameMs: null };
      probe.workers.push(record);
      this.addEventListener('message', ({ data }) => {
        if (data.type === 'ready') {
          record.count = data.count;
          record.readyMs = performance.now() - record.started;
        }
        if (data.type === 'frame') {
          record.received++;
          record.inFlight--;
          record.firstFrameMs ??= performance.now() - record.started;
          record.bytes.push(data.buffer.byteLength);
          record.bytes = record.bytes.slice(-256);
        }
      });
      const post = this.postMessage.bind(this);
      this.postMessage = (data, transfer) => {
        if (data.type === 'sample') {
          record.sent++;
          record.inFlight++;
          record.maxInFlight = Math.max(record.maxInFlight, record.inFlight);
          record.bytes.push(data.buffer.byteLength);
          record.transfers.push(transfer?.length === 1 && transfer[0] === data.buffer);
          record.times.push(performance.now());
          record.phases.push(data.seconds);
          for (const key of ['bytes', 'transfers', 'times', 'phases']) record[key] = record[key].slice(-256);
        }
        return post(data, transfer);
      };
      const terminate = this.terminate.bind(this);
      this.terminate = () => { record.terminated = true; return terminate(); };
    }
  };
  const request = window.requestAnimationFrame.bind(window);
  window.requestAnimationFrame = callback => request(time => { probe.raf++; callback(time); });
  new PerformanceObserver(list => {
    probe.longTasks.push(...list.getEntries().map(entry => ({ start: entry.startTime, duration: entry.duration })));
    probe.longTasks = probe.longTasks.slice(-100);
  }).observe({ type: 'longtask', buffered: true });
  addEventListener('pageshow', event => probe.persisted.push(event.persisted));
})();`;

export async function testLanding({ call, evaluate, wait, origin, root, setParticleWorkerFailure }) {
  const home = origin + '/sco-core/';
  const field = "document.querySelector('[data-sco-landing][data-sco-field]')";
  const toggle = "document.querySelector('[data-sco-field-toggle]')";
  const active = "window.__scoFieldProbe.workers.filter(worker => !worker.terminated).at(-1)";
  const sleep = ms => new Promise(resolve => setTimeout(resolve, ms));
  const setMotion = async value => {
    await call('Emulation.setEmulatedMedia', { features: [{ name: 'prefers-reduced-motion', value }] });
    // CDP changes .matches before Chrome delivers change events. Give rendering
    // two opportunities to dispatch native media events, including after bfcache.
    await evaluate(`getComputedStyle(document.documentElement).color;
      new Promise(resolve => requestAnimationFrame(() => requestAnimationFrame(resolve)))`);
  };
  const script = await call('Page.addScriptToEvaluateOnNewDocument', { source: probeSource });
  let navigation = 0;
  const navigate = async route => {
    const url = home + route + (route.includes('?') ? '&' : '?') + 'landing-test=' + ++navigation;
    await call('Page.navigate', { url });
    await wait(`location.href === ${JSON.stringify(url)} && document.readyState === 'complete' && !!window.__scoFieldProbe`);
  };
  const running = async () => {
    await wait(`${field}?.dataset.state === 'running' && ${active}?.received > 0 && !${toggle}.hidden`);
  };
  const metrics = () => evaluate(`({ state: ${field}?.dataset.state, worker: ${active},
    raf: window.__scoFieldProbe.raf, longTasks: window.__scoFieldProbe.longTasks })`);
  const scroll = async top => {
    await evaluate(`scrollTo({ top: ${top}, behavior: 'instant' });
      new Promise(resolve => requestAnimationFrame(() => requestAnimationFrame(resolve)))`);
  };
  const scrollStyles = () => evaluate(`(() => {
    const read = selector => {
      const element = document.querySelector(selector);
      const style = getComputedStyle(element);
      let effectiveOpacity = 1;
      for (let ancestor = element; ancestor; ancestor = ancestor.parentElement) {
        effectiveOpacity *= Number(getComputedStyle(ancestor).opacity);
      }
      const box = element.getBoundingClientRect();
      return { opacity: Number(style.opacity), effectiveOpacity, transform: style.transform,
        animation: style.animationName, onscreen: box.bottom > 0 && box.top < innerHeight };
    };
    return { copy: read('.sco-landing__copy'), topline: read('.sco-landing__topline'),
      hubs: read('.sco-landing__hub-nav'), footer: read('.sco-landing__footer'),
      particles: read('[data-sco-particles]'),
      front: read('[data-sco-particles-front]'),
      block: read('.sl-container:has(> [data-sco-landing]) > .sl-markdown-content > :first-child') };
  })()`);
  const assertStopped = async label => {
    // One already-transferred frame may finish, so settle before measuring no new work.
    await sleep(100);
    const before = await metrics();
    await sleep(250);
    const after = await metrics();
    assert.equal(after.worker?.sent, before.worker?.sent, label + ': worker kept sampling');
    assert.equal(after.raf, before.raf, label + ': animation frame loop kept running');
  };
  try {
    await call('Network.setCacheDisabled', { cacheDisabled: true });
    await setMotion('no-preference');
    await call('Emulation.setDeviceMetricsOverride', { width: 1440, height: 1000, deviceScaleFactor: 2, mobile: false });
    await evaluate("sessionStorage.removeItem('sco-field-test')");
    await navigate('');
    await running();
    await setMotion('reduce');
    await sleep(150);
    console.log('OBSERVED initial reduced-motion listeners ' + JSON.stringify(await evaluate(`({
      state: ${field}?.dataset.state, media: window.__scoFieldMedia.map(record => ({
        matches: record.list.matches, adds: record.adds, removes: record.removes, calls: record.calls })) })`)));
    await wait(`${field}?.dataset.state === 'static' && ${active} === undefined`);
    await assertStopped('initial live reduced-motion change');
    await setMotion('no-preference');
    await running();
    console.log('PASS initial live reduced-motion toggle stops the worker/loop and restores only when eligible');

    for (const theme of ['dark', 'light']) {
      for (const width of [360, 390, 430, 600, 820, 1024, 1366, 1440, 1920]) {
        await call('Emulation.setDeviceMetricsOverride', { width, height: 1000, deviceScaleFactor: 2, mobile: false });
        await evaluate(`(() => {
          const select = document.querySelector('header starlight-theme-select select');
          select.value = ${JSON.stringify(theme)};
          select.dispatchEvent(new Event('change', { bubbles: true }));
        })()`);
        await wait(`document.documentElement.dataset.theme === ${JSON.stringify(theme)}
          && ${active}?.count === ${width <= 800 ? 3000 : 12000} && ${active}?.received > 0`);

        // A resize within the same particle-count tier still needs its native
        // ResizeObserver delivery before backing dimensions can be measured.
        await evaluate(`${field}.getBoundingClientRect();
          new Promise(resolve => requestAnimationFrame(() => requestAnimationFrame(resolve)))`);
        const layout = await evaluate(`(() => {
          const visible = element => {
            const box = element?.getBoundingClientRect();
            return !!box && box.width > 0 && box.height > 0 && getComputedStyle(element).visibility !== 'hidden';
          };
          const hero = ${field};
          const theme = document.querySelector('header starlight-theme-select select');
          const canvases = [...hero.querySelectorAll('canvas[data-sco-particles], canvas[data-sco-particles-front]')];
          return {
            overflow: document.documentElement.scrollWidth > innerWidth,
            titles: [...document.querySelectorAll('h1')].map(title => ({ text: title.textContent.trim(), id: title.id,
              marked: title.hasAttribute('data-page-title') })),
            links: [...hero.querySelectorAll('a')].map(link => ({ path: new URL(link.href).pathname,
              text: link.textContent.trim(), visible: visible(link),
              left: link.getBoundingClientRect().left, right: link.getBoundingClientRect().right })),
            arrows: [...hero.querySelectorAll('.sco-landing__arrow')].map(icon => ({
              svg: icon instanceof SVGSVGElement && !!icon.querySelector('path'),
              hidden: icon.getAttribute('aria-hidden'), focusable: icon.tabIndex >= 0, visible: visible(icon)
            })),
            search: visible(document.querySelector('header [data-open-modal]')),
            theme: visible(theme) && !!(theme.getAttribute('aria-label') || theme.labels?.length),
            controls: [theme, document.querySelector('header [data-open-modal]'), ${toggle}].every(control => {
              const box = control.getBoundingClientRect();
              return box.left >= 0 && box.right <= innerWidth;
            }),
            canvases: canvases.map(canvas => ({ width: canvas.width, height: canvas.height,
              cssWidth: canvas.getBoundingClientRect().width, cssHeight: canvas.getBoundingClientRect().height,
              pointer: getComputedStyle(canvas).pointerEvents, hidden: canvas.getAttribute('aria-hidden') })),
            clipped: [getComputedStyle(hero).overflowX, getComputedStyle(hero).overflowY]
              .every(value => value === 'clip' || value === 'hidden'),
            foreground: (Number.parseInt(getComputedStyle(hero.querySelector('[data-sco-particles-front]')).zIndex, 10) || 0)
              > (Number.parseInt(getComputedStyle(hero.querySelector('.sco-landing__inner')).zIndex, 10) || 0),
            workers: window.__scoFieldProbe.workers.filter(worker => !worker.terminated).map(worker => ({
              count: worker.count, bytes: worker.bytes, maxInFlight: worker.maxInFlight })),
            intro: !!document.querySelector('.sco-intro')
          };
        })()`);
        const label = width + 'px ' + theme;
        assert.equal(layout.overflow, false, label + ': page overflow');
        assert.deepEqual(layout.titles, [{ text: 'sco-core SDK', id: '_top', marked: true }], label + ': semantic title');
        assert(layout.search && layout.theme && layout.controls, label + ': header/control accessibility');
        assert.equal(layout.intro, false, label + ': duplicate old intro');
        assert.equal(layout.arrows.length, 6, label + ': task/hub arrow count');
        assert(layout.arrows.every(icon => icon.svg && icon.hidden === 'true' && !icon.focusable && icon.visible),
          label + ': arrows must be visible decorative SVGs outside the tab order');
        for (const [route, text] of [['guides/getting-started', 'Start a plugin'], ['hubs/reference', 'Browse API']]) {
          assert(layout.links.some(link => link.path === '/sco-core/' + route + '/' && link.text === text),
            label + ': task link label changed');
        }
        assert.equal(layout.workers.length, 1, label + ': active worker bound');
        assert(layout.workers.every(worker => worker.maxInFlight <= 1
          && worker.bytes.every(bytes => bytes <= worker.count * 8)), label + ': per-particle buffer/queue bound');
        for (const route of ['guides/getting-started', 'hubs/reference',
          ...['c', 'cpp', 'csharp', 'lua'].map(language => 'languages/' + language),
          ...['start', 'sdks', 'reference', 'integration'].map(hub => 'hubs/' + hub)]) {
          assert(layout.links.some(link => link.path === '/sco-core/' + route + '/' && link.visible
            && link.left >= -1 && link.right <= width + 1), label + ': missing/clipped ' + route);
        }
        assert.equal(layout.canvases.length, 2, label + ': back/front canvas count');
        assert(layout.canvases.every(canvas => canvas.width <= 3072 && canvas.height <= 3072
          && canvas.pointer === 'none' && canvas.hidden === 'true'), label + ': canvas dimension/pointer/accessibility bound');
        assert(layout.canvases.reduce((sum, canvas) => sum + canvas.width * canvas.height, 0) <= 2073600,
          label + ': combined backing store bound');
        assert(layout.canvases.every(canvas => canvas.width <= Math.ceil(canvas.cssWidth * 1.5)
          && canvas.height <= Math.ceil(canvas.cssHeight * 1.5)), label + ': DPR bound');
        assert(layout.clipped && layout.foreground, label + ': clipped foreground particle layer');
      }
    }
    console.log('PASS landing: nine widths in both themes; one title, real task/language/hub links, decorative SVG arrows, visible search/theme controls, bounded canvas and no overflow');

    for (const width of [1440, 390]) {
      await call('Emulation.setDeviceMetricsOverride', { width, height: 1000, deviceScaleFactor: 2, mobile: false });
      await scroll(0);
      await running();
      const start = await scrollStyles();
      const positions = await evaluate(`(() => {
        const hero = ${field}.getBoundingClientRect();
        const block = document.querySelector('.sl-container:has(> [data-sco-landing]) > .sl-markdown-content > :first-child').getBoundingClientRect();
        return { heroExit: hero.bottom + scrollY - innerHeight * 0.4,
          blockEntry: block.top + scrollY - innerHeight + Math.min(20, block.height * 0.1),
          blockCenter: block.top + scrollY - innerHeight * 0.5 };
      })()`);
      for (const top of [0, positions.heroExit / 2, positions.heroExit]) {
        await scroll(top);
        const styles = await scrollStyles();
        for (const part of ['copy', 'topline', 'hubs', 'footer']) {
          assert.equal(styles[part].effectiveOpacity, 1, width + 'px: scrolling faded hero ' + part);
        }
      }
      const exited = await scrollStyles();
      console.log('OBSERVED native scroll ' + JSON.stringify({ width, start, exited, positions,
        document: await evaluate(`({ y: scrollY, focused: document.activeElement?.outerHTML.slice(0, 160),
          focusWithin: document.querySelector('.sco-landing__copy').matches(':focus-within'),
          reduced: matchMedia('(prefers-reduced-motion: reduce)').matches,
          animations: document.querySelector('.sco-landing__copy').getAnimations().map(animation => ({
            state: animation.playState, progress: animation.effect.getComputedTiming().progress,
            timeline: animation.timeline?.currentTime?.toString() })) })`) }));
      assert(exited.hubs.onscreen && exited.footer.onscreen, width + 'px: lower hero readability was not checked onscreen');
      assert.notEqual(exited.copy.transform, start.copy.transform, width + 'px: hero copy did not move on scroll');
      assert(exited.particles.opacity < start.particles.opacity - 0.05, width + 'px: background decoration did not fade');
      assert(exited.front.opacity < start.front.opacity - 0.05, width + 'px: foreground decoration did not fade');
      assert.notEqual(exited.particles.transform, start.particles.transform, width + 'px: particle parallax did not advance');
      assert.notEqual(exited.front.transform, start.front.transform, width + 'px: foreground parallax did not advance');
      assert.notEqual(exited.particles.transform, exited.front.transform, width + 'px: particle depth planes moved together');
      await scroll(0);
      const reversed = await scrollStyles();
      for (const part of ['copy', 'topline', 'hubs', 'footer']) {
        assert.equal(reversed[part].effectiveOpacity, 1, width + 'px: reverse scrolling faded hero ' + part);
      }
      assert(Math.abs(reversed.particles.opacity - start.particles.opacity) < 0.02, width + 'px: background fade did not reverse');
      assert(Math.abs(reversed.front.opacity - start.front.opacity) < 0.02, width + 'px: foreground fade did not reverse');
      assert.equal(reversed.copy.transform, start.copy.transform, width + 'px: hero movement did not reverse');
      assert.equal(reversed.particles.transform, start.particles.transform, width + 'px: parallax did not reverse');
      assert.equal(reversed.front.transform, start.front.transform, width + 'px: foreground parallax did not reverse');
      await scroll(positions.blockEntry);
      const entering = await scrollStyles();
      await scroll(positions.blockCenter);
      const entered = await scrollStyles();
      assert(entered.block.opacity > entering.block.opacity + 0.05, width + 'px: content block did not fade in');
      await scroll(positions.blockEntry);
      const reverseEntry = await scrollStyles();
      assert(Math.abs(reverseEntry.block.opacity - entering.block.opacity) < 0.02, width + 'px: content entry did not reverse');
      await scroll(0);
    }
    console.log('PASS native CSS scroll timelines: opaque hero text, decoration fade, hero movement, particle parallax and content entry follow actual scroll and reverse at desktop/mobile widths');

    await call('Emulation.setDeviceMetricsOverride', { width: 1440, height: 1000, deviceScaleFactor: 2, mobile: false });
    await wait(`${active}?.count === 12000 && ${active}?.received > 0`);
    await running();
    const first = await metrics();
    await sleep(2100);
    const timed = await metrics();
    const worker = timed.worker;
    const times = worker.times.filter(time => time >= (first.worker.times.at(-1) ?? 0));
    const intervals = times.slice(1).map((time, index) => time - times[index]);
    const elapsed = times.at(-1) - times[0];
    assert(worker.sent - first.worker.sent <= 65, 'worker exceeded 30fps cadence over 2.1s');
    assert(worker.sent > first.worker.sent, 'worker did not advance during cadence observation');
    assert(intervals.every(interval => interval >= 32.5), 'sample cadence exceeded 30fps with timer tolerance');
    assert.equal(worker.maxInFlight, 1, 'worker request queue exceeded one');
    assert(worker.transfers.every(Boolean), 'sample must transfer exactly its one buffer');
    assert(worker.bytes.every(bytes => bytes > 0 && bytes <= 96000), 'position buffers exceeded 96kB');
    assert(Number.isFinite(worker.readyMs) && Number.isFinite(worker.firstFrameMs), 'real worker initialization unobserved');
    console.log('OBSERVED landing worker ' + JSON.stringify({ count: worker.count, readyMs: worker.readyMs,
      firstFrameMs: worker.firstFrameMs, samples: times.length, elapsedMs: elapsed,
      meanIntervalMs: intervals.reduce((sum, value) => sum + value, 0) / intervals.length,
      minIntervalMs: Math.min(...intervals), maxInFlight: worker.maxInFlight,
      maxBufferBytes: Math.max(...worker.bytes), longTasks: timed.longTasks }));

    await evaluate(`${toggle}.click()`);
    await wait(`${field}.dataset.state === 'paused' && ${toggle}.getAttribute('aria-pressed') === 'true'`);
    await assertStopped('pause');
    const paused = await metrics();
    const bitmaps = `${field} && [...${field}.querySelectorAll('canvas')].map(canvas => canvas.toDataURL())`;
    const image = await evaluate(`JSON.stringify(${bitmaps})`);
    await sleep(200);
    assert.equal(await evaluate(`JSON.stringify(${bitmaps})`), image, 'pause changed a painted canvas phase');
    await evaluate(`${toggle}.click()`);
    await running();
    const resumed = await metrics();
    const resumePhase = resumed.worker.phases.slice(-(resumed.worker.sent - paused.worker.sent))[0];
    const phaseDelta = ((resumePhase - paused.worker.phases.at(-1)) % 10 + 10) % 10;
    assert(phaseDelta <= 0.2 || phaseDelta >= 9.9, 'resume jumped across paused time');
    assert.equal(await evaluate(`${toggle}.getAttribute('aria-pressed')`), 'false');

    await evaluate('scrollTo(0, document.documentElement.scrollHeight)');
    await wait(`${field}.dataset.state !== 'running'`);
    await assertStopped('offscreen');
    await evaluate('scrollTo(0, 0)');
    await running();
    const { targetInfo } = await call('Target.getTargetInfo');
    const background = await call('Target.createTarget', { url: 'about:blank', background: false });
    let nativeHidden;
    try {
      await call('Target.activateTarget', { targetId: background.targetId });
      await sleep(100);
      nativeHidden = await evaluate('document.hidden');
      if (nativeHidden) {
        await wait(`${field}.dataset.state !== 'running'`);
        await assertStopped('background tab');
      }
      await call('Target.activateTarget', { targetId: targetInfo.targetId });
      await running();
    } finally { await call('Target.closeTarget', { targetId: background.targetId }); }
    if (!nativeHidden) {
      // Some headless versions keep every tab visible. Exercise the visibility
      // handler explicitly as a lifecycle fixture while retaining the real worker.
      await evaluate(`Object.defineProperty(document, 'hidden', { configurable: true, get: () => true });
        Object.defineProperty(document, 'visibilityState', { configurable: true, get: () => 'hidden' });
        document.dispatchEvent(new Event('visibilitychange'))`);
      await wait(`${field}.dataset.state !== 'running'`);
      await assertStopped('hidden visibility fixture');
      await evaluate("delete document.hidden; delete document.visibilityState; document.dispatchEvent(new Event('visibilitychange'))");
      await running();
    }
    console.log('OBSERVED visibility lifecycle: ' + (nativeHidden ? 'native background tab' : 'headless visibility-event fixture'));

    await evaluate("dispatchEvent(new PageTransitionEvent('pagehide', { persisted: true }))");
    assert.equal(await evaluate(`${active} !== undefined`), false, 'pagehide left worker alive');
    await assertStopped('pagehide');
    await evaluate("dispatchEvent(new PageTransitionEvent('pageshow', { persisted: true }))");
    await running();
    await evaluate(`${toggle}.click()`);
    await wait(`${field}.dataset.state === 'paused'`);
    await evaluate("dispatchEvent(new PageTransitionEvent('pagehide', { persisted: true })); dispatchEvent(new PageTransitionEvent('pageshow', { persisted: true }))");
    await wait(`${field}.dataset.state === 'paused' && !${toggle}.hidden`);
    assert.equal(await evaluate(`${active} !== undefined`), false, 'persisted restore ignored user pause');
    await assertStopped('paused persisted restore');
    await evaluate(`${toggle}.click()`);
    await running();
    const history = await call('Page.getNavigationHistory');
    const homeEntry = history.entries[history.currentIndex].id;
    await evaluate('window.__scoFieldProbe.persisted = []');
    await evaluate(`${field}.querySelector('a[href="/sco-core/guides/getting-started/"]').click()`);
    await wait("location.pathname === '/sco-core/guides/getting-started/' && document.readyState === 'complete'");
    assert.equal(await evaluate('window.__scoFieldProbe.workers.length'), 0, 'navigated docs created field worker');
    await call('Page.navigateToHistoryEntry', { entryId: homeEntry });
    await wait("location.pathname === '/sco-core/' && document.readyState === 'complete'");
    await running();
    console.log('OBSERVED browser back pageshow.persisted: ' + await evaluate('window.__scoFieldProbe.persisted.includes(true)'));
    console.log('PASS real worker pause/resume retains painted phase; offscreen, visibility, persisted lifecycle fixtures and browser-back navigation restore only when eligible');

    await setMotion('reduce');
    await sleep(150);
    console.log('OBSERVED live reduced-motion transition ' + JSON.stringify(await evaluate(`({
      state: ${field}?.dataset.state, reduced: matchMedia('(prefers-reduced-motion: reduce)').matches,
      activeWorkers: window.__scoFieldProbe.workers.filter(worker => !worker.terminated).length,
      mediaChanges: window.__scoFieldProbe.mediaChanges, persisted: window.__scoFieldProbe.persisted,
      media: window.__scoFieldMedia.map(record => ({ matches: record.list.matches, adds: record.adds,
        removes: record.removes, calls: record.calls })) })`)));
    await wait(`${field}?.dataset.state === 'static' && ${active} === undefined`);
    await assertStopped('live reduced-motion change');
    await navigate('');
    await wait(`${field}?.dataset.state === 'static'`);
    assert.equal(await evaluate('window.__scoFieldProbe.workers.length'), 0, 'reduced motion created a worker');
    assert.equal(await evaluate(`${toggle}.hidden`), true);
    const reducedScroll = await evaluate(`document.querySelector('.sl-container:has(> [data-sco-landing]) > .sl-markdown-content').offsetTop`);
    for (const top of [0, reducedScroll, 0]) {
      await scroll(top);
      const styles = await scrollStyles();
      for (const [part, style] of Object.entries(styles)) {
        assert.equal(style.opacity, 1, 'reduced motion hides ' + part);
        assert.equal(style.animation, 'none', 'reduced motion animates ' + part);
        assert.equal(await evaluate(`new DOMMatrixReadOnly(${JSON.stringify(style.transform === 'none' ? '' : style.transform)}).isIdentity`), true,
          'reduced motion transforms ' + part);
      }
    }
    await assertStopped('reduced motion');
    await setMotion('no-preference');

    for (const failure of ['construction', 'startup', 'runtime']) {
      await evaluate(`sessionStorage.setItem('sco-field-test', ${JSON.stringify(failure)})`);
      setParticleWorkerFailure(failure === 'construction' ? null : failure);
      await navigate('');
      await wait(`${field}?.dataset.state === 'unavailable' && ${toggle}.hidden`);
      assert(await evaluate(`${field}.querySelectorAll('a[href]').length >= 10`), failure + ': fallback lost links');
      await assertStopped(failure + ' failure');
    }
    setParticleWorkerFailure(null);
    await evaluate("sessionStorage.removeItem('sco-field-test')");
    console.log('PASS reduced motion has a static field and no worker/loop; native construction and real worker startup/runtime failures preserve usable content');

    await call('Emulation.setScriptExecutionDisabled', { value: true });
    await call('Page.navigate', { url: home });
    await wait(`document.readyState === 'complete' && !!${field}`);
    assert.equal(await evaluate(`${field}.dataset.state`), 'static');
    assert.equal(await evaluate(`${toggle}.hidden`), true);
    assert.equal(await evaluate("document.querySelectorAll('h1').length"), 1);
    assert.equal(await evaluate(`${field}.querySelectorAll('a[href]').length >= 10`), true);
    await evaluate(`${field}.querySelector('a[href="/sco-core/guides/getting-started/"]').click()`);
    await wait("location.pathname === '/sco-core/guides/getting-started/' && document.readyState === 'complete'");
    await call('Emulation.setScriptExecutionDisabled', { value: false });
    await navigate('guides/getting-started/');
    assert.equal(await evaluate(`!!${field}`), false, 'docs received landing hero');
    assert.equal(await evaluate('window.__scoFieldProbe.workers.length'), 0, 'docs created field worker');
    assert.equal(await evaluate("getComputedStyle(document.querySelector('main .sl-markdown-content > :first-child')).animationName"), 'none',
      'docs inherited landing scroll animations');
    await navigate('404.html');
    assert.equal(await evaluate(`!!${field}`), false, '404 received landing hero');
    assert.equal(await evaluate("document.querySelectorAll('h1').length"), 1);
    assert.equal(await evaluate('window.__scoFieldProbe.workers.length'), 0);
    console.log('PASS no-JavaScript task link follows its real route; docs and 404 retain the default hero without a field worker');

    const assets = await fs.readdir(path.join(root, '_astro'));
    const sizes = { js: 0, css: 0, worker: 0 };
    for (const name of assets) {
      const bytes = (await fs.stat(path.join(root, '_astro', name))).size;
      if (name.endsWith('.js')) sizes.js += bytes;
      if (name.endsWith('.css')) sizes.css += bytes;
      if (/particle-field\.worker.*\.js$/.test(name)) sizes.worker += bytes;
    }
    console.log('OBSERVED built asset bytes ' + JSON.stringify(sizes));
  } finally {
    setParticleWorkerFailure(null);
    await call('Emulation.setScriptExecutionDisabled', { value: false });
    await call('Emulation.setEmulatedMedia', { features: [] });
    await call('Page.removeScriptToEvaluateOnNewDocument', { identifier: script.identifier });
  }
}
