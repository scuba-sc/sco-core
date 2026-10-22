// SPDX-License-Identifier: GPL-3.0-only
import { DESKTOP_PARTICLES, POSITION_COMPONENTS, makeOrbits, sampleField } from './field-model';
import type { FieldRequest, FieldResponse } from './field-model';

// The worker has no clock or loop. It only returns the single buffer it receives.
let orbits: Float32Array | undefined;
let count = 0;

function reply(message: FieldResponse, transfer: Transferable[] = []): void {
	self.postMessage(message, { transfer });
}

self.onmessage = ({ data }: MessageEvent<FieldRequest>) => {
	try {
		if (data.type === 'init') {
			if (!Number.isInteger(data.count) || data.count < 1 || data.count > DESKTOP_PARTICLES) {
				throw new Error('Invalid field size');
			}
			count = data.count;
			orbits = makeOrbits(count);
			reply({ type: 'ready', count });
		} else if (data.type === 'sample') {
			if (!orbits || !Number.isFinite(data.seconds) || !Number.isSafeInteger(data.sequence) ||
				!(data.buffer instanceof ArrayBuffer) ||
				data.buffer.byteLength !== count * POSITION_COMPONENTS * Float32Array.BYTES_PER_ELEMENT) {
				throw new Error('Invalid field sample');
			}
			sampleField(orbits, data.seconds, new Float32Array(data.buffer));
			reply({ type: 'frame', sequence: data.sequence, seconds: data.seconds, buffer: data.buffer }, [data.buffer]);
		}
	} catch {
		reply({ type: 'error' });
	}
};
