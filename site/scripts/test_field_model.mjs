// SPDX-License-Identifier: GPL-3.0-only
import assert from 'node:assert/strict';
import { loadFieldModel, sampleReference } from './field_reference.mjs';

const model = await loadFieldModel();
assert.equal(model.FIELD_PERIOD, 480, 'the complete field has an eight-minute period');
assert.deepEqual([...model.ORBIT_TURNS], [12, 8, 6, 5], 'four distinct orbital layers');
assert.equal(model.DESKTOP_PARTICLES, 12000);
assert.equal(model.MOBILE_PARTICLES, 3000);
assert.equal(model.POSITION_COMPONENTS, 2);
let maxReferencePixelError = 0;
const full = model.makeOrbits(model.DESKTOP_PARTICLES);

for (const count of [1, model.MOBILE_PARTICLES, model.DESKTOP_PARTICLES]) {
  const orbits = model.makeOrbits(count);
  assert(orbits instanceof Float32Array);
  assert.equal(orbits.byteLength, count * 6 * Float32Array.BYTES_PER_ELEMENT);
  assert(orbits.byteLength <= 288000, 'bounded orbital parameter storage');
  assert.deepEqual(orbits, model.makeOrbits(count), 'seeded construction is deterministic');
  assert.deepEqual(orbits, full.slice(0, orbits.length), 'count changes preserve the common seeded prefix');
  for (let base = 0; base < orbits.length; base += 6) {
    assert(orbits[base] >= 34 && orbits[base] <= 377, 'bounded positive orbital radius');
    for (const offset of [1, 3]) {
      assert(Math.abs(orbits[base + offset] ** 2 + orbits[base + offset + 1] ** 2 - 1) < 0.000001,
        'cached rotations stay normalized');
    }
    assert(orbits[base + 5] === 1 || orbits[base + 5] === -1, 'orbit direction is signed');
  }
  const actual = new Float32Array(count * model.POSITION_COMPONENTS);
  const expected = new Float32Array(actual.length);
  const start = new Float32Array(actual.length);
  assert(actual.byteLength <= 96000, 'bounded worker position snapshot');
  model.sampleField(orbits, 0, start);
  for (const seconds of [0, 1.234, 10, 57, -0.25, -1000, 1000000000]) {
    model.sampleField(orbits, seconds, actual);
    assert(actual.every(Number.isFinite), 'samples stay finite for negative and large times');
    sampleReference(orbits, seconds, expected);
    for (let index = 0; index < actual.length; index++) {
      maxReferencePixelError = Math.max(maxReferencePixelError, Math.abs(actual[index] - expected[index]));
    }
  }
  model.sampleField(orbits, model.FIELD_PERIOD, actual);
  assert.deepEqual(actual, start, 'declared period closes exactly');
  model.sampleField(orbits, 0, actual);
  assert.deepEqual(actual, start, 'sampling order does not alter the field');
  model.sampleField(orbits, 10, actual);
  const rmsChange = Math.sqrt(actual.reduce((sum, value, index) => sum + (value - start[index]) ** 2, 0) / actual.length);
  assert(rmsChange > 5, 'the complete scene must not repeat after ten seconds');
  model.sampleField(orbits, model.FIELD_PERIOD - 0.001, actual);
  model.sampleField(orbits, 0.001, expected);
  for (let index = 0; index < actual.length; index++) {
    assert(Math.abs(actual[index] - expected[index]) < 0.5, 'wrapping must not jump the field');
  }
  if (count > 1) {
    for (let phase = 0; phase <= 1000; phase++) {
      const seconds = model.FIELD_PERIOD * phase / 1000;
      model.sampleField(orbits, seconds, actual);
      sampleReference(orbits, seconds, expected);
      for (let index = 0; index < actual.length; index++) {
        maxReferencePixelError = Math.max(maxReferencePixelError, Math.abs(actual[index] - expected[index]));
      }
    }
  }
}
assert(maxReferencePixelError < 0.01, 'optimized model must match the exact-trigonometric reference');
console.log('PASS particle model: deterministic finite independent-time samples, orbital layers, bounded storage, non-repetition and continuous 480s wrap');
console.log('PASS exact-trigonometric reference ' + JSON.stringify({ phases: 1001, counts: [3000, 12000],
  maxReferencePixelError, toleranceReferencePixels: 0.01 }));
