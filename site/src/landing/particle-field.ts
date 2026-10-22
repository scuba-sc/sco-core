// SPDX-License-Identifier: GPL-3.0-only
import {
	DESKTOP_PARTICLES, FIELD_CENTER_X, FIELD_CENTER_Y, FIELD_HEIGHT, FIELD_PERIOD, FIELD_WIDTH,
	MOBILE_PARTICLES, MOBILE_WIDTH, POSITION_COMPONENTS, makeOrbits, sampleField,
} from './field-model';
import type { FieldResponse } from './field-model';

const FRAME_INTERVAL = 1000 / 30;
const MAX_DPR = 1.5;
const MAX_BACKING_PIXELS = 1920 * 1080;
const MAX_BACKING_DIMENSION = 3072;
const WORKER_TIMEOUT = 8000;
const FRONT_STRIDE = 67;
const COLORS = ['rgb(141 228 224 / 0.32)', 'rgb(141 228 224 / 0.26)', 'rgb(218 231 234 / 0.48)'];

export function initParticleField(root: HTMLElement): () => void {
	const canvas = root.querySelector<HTMLCanvasElement>('canvas[data-sco-particles]');
	const frontCanvas = root.querySelector<HTMLCanvasElement>('canvas[data-sco-particles-front]');
	const toggle = root.querySelector<HTMLButtonElement>('button[data-sco-field-toggle]');
	let context: CanvasRenderingContext2D | null = null;
	let frontContext: CanvasRenderingContext2D | null = null;
	try { context = canvas?.getContext('2d', { alpha: true }) ?? null; } catch { /* CSS remains visible. */ }
	try { frontContext = frontCanvas?.getContext('2d', { alpha: true }) ?? null; } catch { /* The back layer remains usable. */ }
	if (!canvas || !toggle || !context) {
		root.dataset.state = 'unavailable';
		if (toggle) toggle.hidden = true;
		return () => {};
	}
	const ctx = context;
	const front = frontCanvas && frontContext ? { canvas: frontCanvas, ctx: frontContext } : null;
	if (frontCanvas && !front) { frontCanvas.width = 1; frontCanvas.height = 1; }
	const motion = window.matchMedia('(prefers-reduced-motion: reduce)');
	let disposed = false;
	let suspended = false;
	let failed = false;
	let visible = false;
	let userPaused = false;
	let running = false;
	let hasFrame = false;
	let ready = false;
	let width = 1;
	let height = 1;
	let count = DESKTOP_PARTICLES;
	let snapshot = new Float32Array(count * POSITION_COMPONENTS);
	let buffer: ArrayBuffer | null = null;
	let worker: Worker | null = null;
	let resizeObserver: ResizeObserver | undefined;
	let intersectionObserver: IntersectionObserver | undefined;
	let raf = 0;
	let watchdog: ReturnType<typeof setTimeout> | undefined;
	let sequence = 0;
	let epoch = 0;
	let inFlight: { sequence: number; seconds: number; epoch: number } | undefined;
	let seconds = 0;
	let startedAt = 0;
	let startedFrom = 0;
	let requestedAt = -Infinity;
	let paintedAt = -Infinity;

	const eligible = () => !disposed && !suspended && !failed && !motion.matches &&
		!userPaused && !document.hidden && visible;
	const clearWatchdog = () => {
		if (watchdog !== undefined) clearTimeout(watchdog);
		watchdog = undefined;
	};
	const updateState = () => {
		root.dataset.state = failed ? 'unavailable' : motion.matches || !hasFrame ? 'static' : running ? 'running' : 'paused';
		toggle.hidden = failed || suspended || motion.matches || !hasFrame;
		toggle.textContent = running ? 'Pause animation' : 'Resume animation';
		toggle.setAttribute('aria-pressed', String(!running));
	};
	const stop = () => {
		running = false;
		epoch++;
		if (raf) cancelAnimationFrame(raf);
		raf = 0;
		clearWatchdog();
	};
	const terminateWorker = () => {
		clearWatchdog();
		worker?.terminate();
		worker = null;
		ready = false;
		inFlight = undefined;
		buffer = null;
	};
	const clearCanvas = (surface: HTMLCanvasElement, renderer: CanvasRenderingContext2D) => {
		renderer.setTransform(1, 0, 0, 1, 0, 0);
		renderer.clearRect(0, 0, surface.width, surface.height);
	};
	const fail = () => {
		if (disposed || suspended || failed) return;
		failed = true;
		stop();
		terminateWorker();
		clearCanvas(canvas, ctx);
		if (front) clearCanvas(front.canvas, front.ctx);
		updateState();
	};
	const watchWorker = () => {
		clearWatchdog();
		watchdog = setTimeout(fail, WORKER_TIMEOUT);
	};
	const paint = () => {
		paintedAt = performance.now();
		clearCanvas(canvas, ctx);
		if (front) clearCanvas(front.canvas, front.ctx);
		// The enlarged orbit extends beyond the hero; a sparse near layer crosses the text.
		const mobile = width <= MOBILE_WIDTH;
		const scale = Math.min(width / (mobile ? 700 : FIELD_WIDTH), height / FIELD_HEIGHT) * 2.8;
		const transform = (surface: HTMLCanvasElement, renderer: CanvasRenderingContext2D,
			layerScale: number, centerX: number, centerY: number) => {
			const pixelX = surface.width / width;
			const pixelY = surface.height / height;
			renderer.setTransform(pixelX * layerScale, 0, 0, pixelY * layerScale,
				pixelX * (centerX - FIELD_CENTER_X * layerScale), pixelY * (centerY - FIELD_CENTER_Y * layerScale));
		};
		transform(canvas, ctx, scale, width * (mobile ? 0.9 : 0.83), height * (mobile ? 0.34 : 0.42));
		for (let bucket = 0; bucket < COLORS.length; bucket++) {
			ctx.fillStyle = COLORS[bucket];
			ctx.beginPath();
			const size = (bucket === 2 ? 1.25 : 0.9) / scale;
			for (let index = bucket; index < count; index += COLORS.length) {
				if (front && index % FRONT_STRIDE === 0) continue;
				ctx.rect(snapshot[index * 2], snapshot[index * 2 + 1], size, size);
			}
			ctx.fill();
		}
		const phase = seconds * Math.PI * 2 / FIELD_PERIOD;
		const orbitX = FIELD_CENTER_X + Math.sin(phase) * 26;
		const orbitY = FIELD_CENTER_Y + Math.sin(phase * 2) * 16;
		const tilt = 0.65 + Math.sin(phase) * 0.08;
		const strokeTrails = (renderer: CanvasRenderingContext2D, stride: number, layerScale: number, near: boolean) => {
			const length = Math.min(2.75, Math.max(0.75, layerScale * 1.1)) / layerScale;
			renderer.strokeStyle = near ? 'rgb(218 245 242 / 0.32)' : 'rgb(141 228 224 / 0.2)';
			renderer.lineWidth = (near ? 0.5 : 0.35) / layerScale;
			renderer.lineCap = 'butt';
			renderer.beginPath();
			for (let index = 0; index < count; index += stride) {
				if (!near && front && index % FRONT_STRIDE === 0) continue;
				const x = snapshot[index * 2];
				const y = snapshot[index * 2 + 1];
				const direction = index % 7 === 0 ? -1 : 1;
				const tangentX = -(y - orbitY) / tilt * direction;
				const tangentY = (x - orbitX) * tilt * direction;
				const ratio = length / Math.max(1, Math.hypot(tangentX, tangentY));
				renderer.moveTo(x - tangentX * ratio, y - tangentY * ratio);
				renderer.lineTo(x, y);
			}
			renderer.stroke();
		};
		strokeTrails(ctx, 8, scale, false);
		if (front) {
			const nearScale = scale * 1.16;
			transform(front.canvas, front.ctx, nearScale, width * (mobile ? 0.5 : 0.6), height * 0.37);
			front.ctx.fillStyle = 'rgb(205 245 240 / 0.52)';
			front.ctx.beginPath();
			const size = 1.4 / nearScale;
			for (let index = 0; index < count; index += FRONT_STRIDE) {
				front.ctx.rect(snapshot[index * 2], snapshot[index * 2 + 1], size, size);
			}
			front.ctx.fill();
			strokeTrails(front.ctx, FRONT_STRIDE, nearScale, true);
		}
	};
	const paintStill = () => {
		sampleField(makeOrbits(count), seconds, snapshot);
		hasFrame = true;
		paint();
	};
	const requestSample = (time: number) => {
		if (!worker || !ready || inFlight || !buffer || !eligible()) return;
		const next = { sequence: ++sequence, seconds: time, epoch };
		inFlight = next;
		const transferred = buffer;
		buffer = null;
		watchWorker();
		try {
			worker.postMessage({ type: 'sample', sequence: next.sequence, seconds: time, buffer: transferred }, [transferred]);
		} catch { fail(); }
	};
	const tick = (now: number) => {
		raf = 0;
		if (!running || !eligible()) return;
		if (!inFlight && now - requestedAt >= FRAME_INTERVAL && now - paintedAt >= FRAME_INTERVAL) {
			requestedAt = now;
			requestSample((startedFrom + (now - startedAt) / 1000) % FIELD_PERIOD);
		}
		if (running && eligible()) raf = requestAnimationFrame(tick);
	};
	const begin = () => {
		if (running || !eligible()) return;
		running = true;
		startedAt = performance.now();
		startedFrom = seconds;
		requestedAt = startedAt;
		raf = requestAnimationFrame(tick);
		updateState();
	};
	const startWorker = () => {
		if (worker || !eligible()) return;
		if (typeof Worker === 'undefined') { fail(); return; }
		try {
			const current = new Worker(new URL('./particle-field.worker.ts', import.meta.url), {
				type: 'module', name: 'sco-core-particle-field',
			});
			worker = current;
			buffer = new ArrayBuffer(count * POSITION_COMPONENTS * Float32Array.BYTES_PER_ELEMENT);
			current.onerror = (event) => { event.preventDefault(); fail(); };
			current.onmessageerror = fail;
			current.onmessage = ({ data }: MessageEvent<FieldResponse>) => {
				if (worker !== current || disposed || suspended || failed) return;
				if (data.type === 'ready') {
					if (ready || data.count !== count) { fail(); return; }
					ready = true;
					clearWatchdog();
					requestSample(seconds);
				} else if (data.type === 'frame') {
					const completed = inFlight;
					if (!completed || data.sequence !== completed.sequence || data.seconds !== completed.seconds ||
						!(data.buffer instanceof ArrayBuffer) || data.buffer.byteLength !== snapshot.byteLength) {
						fail(); return;
					}
					clearWatchdog();
					buffer = data.buffer;
					inFlight = undefined;
					if (completed.epoch === epoch && eligible()) {
						snapshot.set(new Float32Array(buffer));
						seconds = data.seconds;
						hasFrame = true;
						paint();
						begin();
					}
				} else { fail(); }
			};
			watchWorker();
			current.postMessage({ type: 'init', count });
		} catch { fail(); }
	};
	const reconcile = () => {
		if (disposed || suspended) return;
		if (motion.matches) {
			stop();
			terminateWorker();
			if (!failed) paintStill();
		} else if (!eligible()) {
			stop();
		} else if (!worker) {
			startWorker();
		} else if (!ready || inFlight) {
			watchWorker();
			if (ready) begin();
		} else if (!running) {
			requestSample(seconds);
		}
		updateState();
	};
	const resize = () => {
		if (disposed || suspended) return;
		const bounds = root.getBoundingClientRect();
		const nextWidth = Math.max(1, bounds.width);
		const nextHeight = Math.max(1, bounds.height);
		const nextCount = nextWidth <= MOBILE_WIDTH ? MOBILE_PARTICLES : DESKTOP_PARTICLES;
		const changedCount = nextCount !== count;
		width = nextWidth;
		height = nextHeight;
		const sizeCanvas = (surface: HTMLCanvasElement, budget: number) => {
			const dpr = Math.min(window.devicePixelRatio || 1, MAX_DPR,
				Math.sqrt(budget / (width * height)), MAX_BACKING_DIMENSION / width, MAX_BACKING_DIMENSION / height);
			const backingWidth = Math.max(1, Math.floor(width * dpr));
			const backingHeight = Math.max(1, Math.floor(height * dpr));
			if (surface.width !== backingWidth) surface.width = backingWidth;
			if (surface.height !== backingHeight) surface.height = backingHeight;
		};
		sizeCanvas(canvas, front ? Math.floor(MAX_BACKING_PIXELS * 0.75) : MAX_BACKING_PIXELS - (frontCanvas ? 1 : 0));
		if (front) sizeCanvas(front.canvas, MAX_BACKING_PIXELS - canvas.width * canvas.height);
		if (changedCount) {
			stop();
			terminateWorker();
			count = nextCount;
			snapshot = new Float32Array(count * POSITION_COMPONENTS);
			if (hasFrame) paintStill();
		} else if (hasFrame) { paint(); }
		if (!intersectionObserver) {
			visible = bounds.width > 0 && bounds.height > 0 && bounds.bottom > 0 && bounds.top < window.innerHeight &&
				bounds.right > 0 && bounds.left < window.innerWidth;
		}
		reconcile();
	};
	const onToggle = () => {
		userPaused = running;
		reconcile();
	};
	const onVisibility = () => reconcile();
	const onMotion = () => reconcile();
	const attach = () => {
		toggle.addEventListener('click', onToggle);
		document.addEventListener('visibilitychange', onVisibility);
		window.addEventListener('resize', resize);
		if (typeof ResizeObserver !== 'undefined') {
			resizeObserver = new ResizeObserver(resize);
			resizeObserver.observe(root);
		}
		if (typeof IntersectionObserver !== 'undefined') {
			intersectionObserver = new IntersectionObserver((entries) => {
				visible = entries.some((entry) => entry.isIntersecting && entry.intersectionRatio > 0);
				reconcile();
			});
			intersectionObserver.observe(root);
		} else { window.addEventListener('scroll', resize, { passive: true }); }
		resize();
	};
	const detach = () => {
		toggle.removeEventListener('click', onToggle);
		document.removeEventListener('visibilitychange', onVisibility);
		window.removeEventListener('resize', resize);
		window.removeEventListener('scroll', resize);
		resizeObserver?.disconnect();
		resizeObserver = undefined;
		intersectionObserver?.disconnect();
		intersectionObserver = undefined;
	};
	const onPageHide = () => {
		suspended = true;
		stop();
		terminateWorker();
		detach();
		updateState();
	};
	const onPageShow = (event: PageTransitionEvent) => {
		if (!event.persisted || !suspended || disposed) return;
		suspended = false;
		visible = false;
		attach();
	};
	window.addEventListener('pagehide', onPageHide);
	window.addEventListener('pageshow', onPageShow);
	// Keep the preference subscription for the component lifetime, including
	// bfcache suspension. reconcile ignores it while the page is suspended.
	motion.addEventListener('change', onMotion);
	attach();
	return () => {
		if (disposed) return;
		disposed = true;
		stop();
		terminateWorker();
		detach();
		window.removeEventListener('pagehide', onPageHide);
		window.removeEventListener('pageshow', onPageShow);
		motion.removeEventListener('change', onMotion);
		toggle.hidden = true;
		root.dataset.state = 'static';
	};
}
