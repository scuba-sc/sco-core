// SPDX-License-Identifier: GPL-3.0-only
// A local orbit model: fixed seeds and periodic harmonics require no simulation history.
export const FIELD_WIDTH = 1920;
export const FIELD_HEIGHT = 1080;
export const FIELD_CENTER_X = 1490;
export const FIELD_CENTER_Y = 406;
export const FIELD_PERIOD = 10;
export const DESKTOP_PARTICLES = 12000;
export const MOBILE_PARTICLES = 3000;
export const MOBILE_WIDTH = 800;
export const POSITION_COMPONENTS = 2;

export type FieldRequest =
	| { type: 'init'; count: number }
	| { type: 'sample'; sequence: number; seconds: number; buffer: ArrayBuffer };

export type FieldResponse =
	| { type: 'ready'; count: number }
	| { type: 'frame'; sequence: number; seconds: number; buffer: ArrayBuffer }
	| { type: 'error' };

const TAU = Math.PI * 2;

function seedFraction(index: number, channel: number): number {
	let value = Math.imul(index + 1, 0x45d9f3b) ^ Math.imul(channel + 1, 0x27d4eb2d);
	value = Math.imul(value ^ (value >>> 16), 0x45d9f3b);
	return ((value ^ (value >>> 16)) >>> 0) / 0x100000000;
}

export function makeOrbits(count: number): Float32Array {
	const orbits = new Float32Array(count * 6);
	for (let index = 0; index < count; index++) {
		const radius = 36 + Math.sqrt(seedFraction(index, 0)) * 326;
		const scattered = seedFraction(index, 1) < 0.64;
		const angle = Math.fround(scattered
			? seedFraction(index, 2) * TAU
			: (index % 3) * TAU / 3 + radius / 72 + (seedFraction(index, 2) - 0.5) * 1.2);
		const wave = Math.fround(seedFraction(index, 3) * TAU);
		const base = index * 6;
		orbits[base] = radius;
		orbits[base + 1] = Math.cos(angle);
		orbits[base + 2] = Math.sin(angle);
		orbits[base + 3] = Math.cos(wave);
		orbits[base + 4] = Math.sin(wave);
		orbits[base + 5] = index % 7 === 0 ? -1 : 1;
	}
	return orbits;
}

export function sampleField(orbits: Float32Array, seconds: number, output: Float32Array): void {
	const phase = ((seconds % FIELD_PERIOD + FIELD_PERIOD) % FIELD_PERIOD) * TAU / FIELD_PERIOD;
	const sinPhase = Math.sin(phase);
	const cosPhase = Math.cos(phase);
	const sinDouble = 2 * sinPhase * cosPhase;
	const cosDouble = cosPhase * cosPhase - sinPhase * sinPhase;
	const centerX = FIELD_CENTER_X + sinPhase * 26;
	const centerY = FIELD_CENTER_Y + sinDouble * 16;
	const tilt = 0.65 + sinPhase * 0.08;
	for (let position = 0, base = 0; position < output.length; position += POSITION_COMPONENTS, base += 6) {
		const radius = orbits[base] + (sinDouble * orbits[base + 3] + cosDouble * orbits[base + 4]) * 11;
		const wobble = (sinPhase * orbits[base + 3] + cosPhase * orbits[base + 4]) * 0.15;
		const square = wobble * wobble;
		// Wobble stays within ±0.15 radians, so low-order terms avoid per-particle trig.
		const sinWobble = wobble * (1 - square / 6);
		const cosWobble = 1 - square / 2 + square * square / 24;
		const spinSin = sinPhase * orbits[base + 5];
		const x = orbits[base + 1] * cosPhase - orbits[base + 2] * spinSin;
		const y = orbits[base + 1] * spinSin + orbits[base + 2] * cosPhase;
		output[position] = centerX + (x * cosWobble - y * sinWobble) * radius;
		output[position + 1] = centerY + (y * cosWobble + x * sinWobble) * radius * tilt;
	}
}
