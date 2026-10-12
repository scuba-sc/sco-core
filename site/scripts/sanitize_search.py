#!/usr/bin/env python3
"""Remove upstream build-machine prefixes from Pagefind WASM panic strings.

Pagefind's prebuilt WebAssembly can contain its builder's absolute Rust source
paths. These are not site source, but are still publication metadata. Replace
only an equal-length path prefix in WASM data bytes; offsets and instructions
remain unchanged. Decompressed WASM must have the WebAssembly magic/version.
"""
import gzip
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
OLD = b'/' + b'Users' + b'/'
NEW = b'/build/'


def sanitize(root):
    changed = 0
    files = sorted((root / 'pagefind').glob('wasm.*.pagefind'))
    if not files:
        raise ValueError('Pagefind WASM missing')
    for path in files:
        raw = path.read_bytes()
        compressed = raw.startswith(b'\x1f\x8b')
        data = gzip.decompress(raw) if compressed else raw
        # Pagefind wraps each resource in a fixed signature before the WASM.
        magic = data.find(b'\x00asm\x01\x00\x00\x00')
        if magic < 0:
            raise ValueError('Invalid Pagefind WebAssembly')
        count = data.count(OLD)
        if count:
            # Validate sections and remap only the data section, never opcodes.
            def uleb(offset):
                value, shift = 0, 0
                while offset < len(data):
                    byte = data[offset]
                    offset += 1
                    value |= (byte & 127) << shift
                    if not byte & 128:
                        return value, offset
                    shift += 7
                    if shift > 35:
                        break
                raise ValueError('Invalid WebAssembly section length')

            cursor = magic + 8
            spans = []
            while cursor < len(data):
                section = data[cursor]
                length, start = uleb(cursor + 1)
                end = start + length
                if end > len(data):
                    raise ValueError('Truncated WebAssembly section')
                if section == 11:
                    spans.append((start, end))
                cursor = end
            rewritten = bytearray(data)
            replaced = 0
            for start, end in spans:
                payload = data[start:end]
                replaced += payload.count(OLD)
                rewritten[start:end] = payload.replace(OLD, NEW)
            if replaced != count:
                raise ValueError('Build path appears outside WebAssembly data')
            data = bytes(rewritten)
            path.write_bytes(gzip.compress(data, mtime=0) if compressed else data)
            changed += count
    print('Pagefind build-path prefixes remapped:', changed)
    return changed


if __name__ == '__main__':
    sanitize(ROOT / 'dist')
