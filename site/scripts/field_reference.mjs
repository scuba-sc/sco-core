// SPDX-License-Identifier: GPL-3.0-only
// Test/benchmark helpers; this exact-trigonometric oracle is not shipped to browsers.
import fs from 'node:fs/promises';
import { stripTypeScriptTypes } from 'node:module';

let modelPromise;
export function loadFieldModel() {
  modelPromise ??= fs.readFile(new URL('../src/landing/field-model.ts', import.meta.url), 'utf8').then(source => {
    const javascript = stripTypeScriptTypes(source, { mode: 'strip' });
    return import('data:text/javascript;base64,' + Buffer.from(javascript).toString('base64'));
  });
  return modelPromise;
}

// Keep geometry and band periods independent of the optimized implementation.
const PERIOD = 480;
const TURNS = [12, 8, 6, 5];
const TAU = Math.PI * 2;
export function sampleReference(orbits, seconds, output) {
  const phase = ((seconds % PERIOD + PERIOD) % PERIOD) * TAU / PERIOD;
  const centerX = 1490 + Math.sin(phase * 2) * 22;
  const centerY = 406 + Math.sin(phase * 4) * 12;
  const tilt = 0.62 + Math.sin(phase * 4) * 0.035;
  for (let index = 0, base = 0; index < output.length / 2; index++, base += 6) {
    const spin = phase * TURNS[index % TURNS.length] * orbits[base + 5];
    const radius = orbits[base] + (Math.sin(phase * 3) * orbits[base + 3]
      + Math.cos(phase * 3) * orbits[base + 4]) * 8;
    const wobble = (Math.sin(phase * 2) * orbits[base + 3]
      + Math.cos(phase * 2) * orbits[base + 4]) * 0.045;
    const sinSpin = Math.sin(spin + wobble);
    const cosSpin = Math.cos(spin + wobble);
    output[index * 2] = centerX + (orbits[base + 1] * cosSpin - orbits[base + 2] * sinSpin) * radius;
    output[index * 2 + 1] = centerY + (orbits[base + 1] * sinSpin + orbits[base + 2] * cosSpin) * radius * tilt;
  }
}
