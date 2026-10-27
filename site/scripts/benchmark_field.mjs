// SPDX-License-Identifier: GPL-3.0-only
// Warm CPU math only: not a worker, paint, browser-frame-rate or GPU benchmark.
import { performance } from 'node:perf_hooks';
import { loadFieldModel, sampleReference } from './field_reference.mjs';

const model = await loadFieldModel();
const report = { node: process.version, warmupSamples: 200, batches: 21, samplesPerBatch: 100,
  results: [], limits: 'Warm CPU math excludes initialization, worker transfer, scheduling and painting. Native GPU use, power and whole-page performance are not measured.' };
for (const count of [model.MOBILE_PARTICLES, model.DESKTOP_PARTICLES]) {
  const orbits = model.makeOrbits(count);
  const output = new Float32Array(count * model.POSITION_COMPONENTS);
  const implementations = { exactTrigonometric: sampleReference, optimized: model.sampleField };
  for (const sample of Object.values(implementations)) {
    for (let index = 0; index < report.warmupSamples; index++) {
      sample(orbits, model.FIELD_PERIOD * index / report.warmupSamples, output);
    }
  }
  const timings = { exactTrigonometric: [], optimized: [] };
  for (let batch = 0; batch < report.batches; batch++) {
    const order = batch % 2 ? ['optimized', 'exactTrigonometric'] : ['exactTrigonometric', 'optimized'];
    for (const name of order) {
      const start = performance.now();
      for (let index = 0; index < report.samplesPerBatch; index++) {
        const seconds = model.FIELD_PERIOD * (batch * report.samplesPerBatch + index)
          / (report.batches * report.samplesPerBatch);
        implementations[name](orbits, seconds, output);
      }
      timings[name].push((performance.now() - start) / report.samplesPerBatch);
    }
  }
  const median = values => values.toSorted((a, b) => a - b)[Math.floor(values.length / 2)];
  const exactMs = median(timings.exactTrigonometric);
  const optimizedMs = median(timings.optimized);
  report.results.push({ count, parameterBytes: orbits.byteLength, positionBytes: output.byteLength,
    exactMedianMsPerSample: exactMs, optimizedMedianMsPerSample: optimizedMs,
    warmCpuMathReductionPercent: (1 - optimizedMs / exactMs) * 100 });
}
console.log(JSON.stringify(report, null, 2));
