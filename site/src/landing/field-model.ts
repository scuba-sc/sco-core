// SPDX-License-Identifier: GPL-3.0-only
// Seeded orbital bands are sampled directly, without simulation history.
export const FIELD_WIDTH = 1920;
export const FIELD_HEIGHT = 1080;
export const FIELD_CENTER_X = 1490;
export const FIELD_CENTER_Y = 406;
export const FIELD_PERIOD = 480;
export const ORBIT_TURNS = [12, 8, 6, 5] as const;
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

export function seedFraction(index: number, channel: number): number {
	let value = Math.imul(index + 1, 0x45d9f3b) ^ Math.imul(channel + 1, 0x27d4eb2d);
	value = Math.imul(value ^ (value >>> 16), 0x45d9f3b);
	return ((value ^ (value >>> 16)) >>> 0) / 0x100000000;
}

export function fieldGeometry(seconds: number) {
	const phase = ((seconds % FIELD_PERIOD + FIELD_PERIOD) % FIELD_PERIOD) * TAU / FIELD_PERIOD;
	return {
		phase,
		centerX: FIELD_CENTER_X + Math.sin(phase * 2) * 22,
		centerY: FIELD_CENTER_Y + Math.sin(phase * 4) * 12,
		tilt: 0.62 + Math.sin(phase * 4) * 0.035,
	};
}

export function makeOrbits(count: number): Float32Array {
	const orbits = new Float32Array(count * 6);
	for (let index = 0; index < count; index++) {
		const radius = 34 + Math.sqrt(seedFraction(index, 0)) * 316 + (index % ORBIT_TURNS.length) * 9;
		const scattered = seedFraction(index, 1) < 0.53;
		const angle = Math.fround(scattered
			? seedFraction(index, 2) * TAU
			: (index % 3) * TAU / 3 + radius / 79 + (seedFraction(index, 2) - 0.5) * 0.9);
		const wave = Math.fround(seedFraction(index, 3) * TAU);
		const base = index * 6;
		orbits[base] = radius;
		orbits[base + 1] = Math.cos(angle);
		orbits[base + 2] = Math.sin(angle);
		orbits[base + 3] = Math.cos(wave);
		orbits[base + 4] = Math.sin(wave);
		orbits[base + 5] = index % 11 === 0 ? -1 : 1;
	}
	return orbits;
}

// Four band rotations and the small shared harmonics are reused by every star.
const bandRotations = new Float64Array(ORBIT_TURNS.length * 2);

export function sampleField(orbits: Float32Array, seconds: number, output: Float32Array): void {
	const { phase, centerX, centerY, tilt } = fieldGeometry(seconds);
	const sinBreath = Math.sin(phase * 3);
	const cosBreath = Math.cos(phase * 3);
	const sinWander = Math.sin(phase * 2);
	const cosWander = Math.cos(phase * 2);
	for (let band = 0; band < ORBIT_TURNS.length; band++) {
		const spin = phase * ORBIT_TURNS[band];
		bandRotations[band * 2] = Math.cos(spin);
		bandRotations[band * 2 + 1] = Math.sin(spin);
	}
	for (let index = 0, base = 0; index < output.length / POSITION_COMPONENTS; index++, base += 6) {
		const band = index % ORBIT_TURNS.length * 2;
		const cosSpin = bandRotations[band];
		const sinSpin = bandRotations[band + 1] * orbits[base + 5];
		const radius = orbits[base] + (sinBreath * orbits[base + 3] + cosBreath * orbits[base + 4]) * 8;
		const wobble = (sinWander * orbits[base + 3] + cosWander * orbits[base + 4]) * 0.045;
		const square = wobble * wobble;
		// Wobble is bounded by ±0.045 radians; these terms stay below 0.01px error.
		const sinWobble = wobble * (1 - square / 6);
		const cosWobble = 1 - square / 2 + square * square / 24;
		const x = orbits[base + 1] * cosSpin - orbits[base + 2] * sinSpin;
		const y = orbits[base + 1] * sinSpin + orbits[base + 2] * cosSpin;
		output[index * 2] = centerX + (x * cosWobble - y * sinWobble) * radius;
		output[index * 2 + 1] = centerY + (y * cosWobble + x * sinWobble) * radius * tilt;
	}
}
