# SPDX-License-Identifier: GPL-3.0-only
"""Explicit native peer only. No Python wire atomics or unverified host responses."""
import ctypes
import hashlib
import os
from pathlib import Path
import stat
import threading
import time

CODES = {1: 'runtime_disabled', 2: 'host_unavailable', 3: 'peer_busy',
         4: 'corrupt_frame', 5: 'auth_failed', 6: 'session_changed',
         7: 'deadline_expired', 9: 'audit_unavailable', 10: 'rate_limited'}

class PeerError(ValueError):
    def __init__(self, code):
        self.code = code
        super().__init__(code)

class NativePeer:
    def __init__(self, adapter, digest, credential):
        path = Path(adapter)
        if not path.is_absolute() or path.is_symlink() or len(digest) != 64:
            raise ValueError('Invalid pinned adapter')
        flags = os.O_RDONLY | getattr(os, 'O_NOFOLLOW', 0)
        fd = os.open(path, flags)
        try:
            info = os.fstat(fd)
            if not stat.S_ISREG(info.st_mode) or info.st_size > 64 * 1024 * 1024:
                raise ValueError('Invalid adapter extent')
            if os.name != 'nt' and (info.st_uid not in (0, os.getuid()) or info.st_mode & 0o022):
                raise ValueError('Untrusted adapter permissions')
            h = hashlib.sha256()
            while chunk := os.read(fd, 65536):
                h.update(chunk)
            if h.hexdigest() != digest:
                raise ValueError('Adapter digest mismatch')
            options = {'winmode': 0x00000100 | 0x00001000} if os.name == 'nt' else {}
            self.lib = ctypes.CDLL(str(path), **options)
            after = os.stat(path, follow_symlinks=False)
            if (info.st_dev, info.st_ino, info.st_size, info.st_mtime_ns) != (after.st_dev, after.st_ino, after.st_size, after.st_mtime_ns):
                raise ValueError('Adapter changed during loading')
        finally:
            os.close(fd)
        signatures = {
            'sco_rt_adapter_version': (ctypes.c_uint, []),
            'sco_rt_now': (ctypes.c_uint64, []),
            'sco_rt_create': (ctypes.c_void_p, []),
            'sco_rt_destroy': (None, [ctypes.c_void_p]),
            'sco_rt_close': (None, [ctypes.c_void_p]),
            'sco_rt_begin': (ctypes.c_int, [ctypes.c_void_p, ctypes.c_char_p]),
            'sco_rt_challenge': (ctypes.c_int, [ctypes.c_void_p]),
            'sco_rt_ready': (ctypes.c_int, [ctypes.c_void_p]),
            'sco_rt_heartbeat': (ctypes.c_int, [ctypes.c_void_p]),
            'sco_rt_send': (ctypes.c_int, [ctypes.c_void_p, ctypes.c_uint, ctypes.c_char_p, ctypes.c_uint]),
            'sco_rt_receive': (ctypes.c_int, [ctypes.c_void_p, ctypes.c_void_p, ctypes.c_uint, ctypes.POINTER(ctypes.c_uint)]),
        }
        for name, (result, args) in signatures.items():
            fn = getattr(self.lib, name); fn.restype = result; fn.argtypes = args
        if self.lib.sco_rt_adapter_version() != 1:
            raise ValueError('Adapter version mismatch')
        if not Path(credential).is_absolute():
            raise ValueError('Credential path must be absolute')
        self.credential = str(credential)
        self.pointer = self.lib.sco_rt_create()
        if not self.pointer:
            raise ValueError('Adapter allocation refused')
        self.connected = False
        self.lock = threading.Lock()
        self.stopping = threading.Event()
        self.link_error = None
        self.worker = threading.Thread(target=self._heartbeat, daemon=True)
        self.worker.start()

    def _check(self, code):
        if code:
            self.connected = False
            self.lib.sco_rt_close(self.pointer)
            raise PeerError(CODES.get(code, 'corrupt_frame'))

    def _wait(self, fn, deadline):
        while True:
            code = fn(self.pointer)
            if code != 8:
                self._check(code); return
            if time.monotonic() >= deadline:
                self._check(7)
            time.sleep(0.005)

    def _heartbeat(self):
        while not self.stopping.wait(0.5):
            with self.lock:
                if self.connected:
                    try:
                        self._check(self.lib.sco_rt_heartbeat(self.pointer))
                    except PeerError as exc:
                        self.link_error = exc.code

    def read(self, opcode, payload):
        with self.lock:
            if self.link_error:
                code, self.link_error = self.link_error, None
                raise PeerError(code) # no automatic replay into a new host session
            deadline = time.monotonic() + 2
            if not self.connected:
                try:
                    os.stat(self.credential, follow_symlinks=False)
                except FileNotFoundError:
                    raise PeerError('host_unavailable') from None
                except OSError:
                    raise PeerError('auth_failed') from None
                self._check(self.lib.sco_rt_begin(self.pointer, os.fsencode(self.credential)))
                self._wait(self.lib.sco_rt_challenge, deadline)
                self._wait(self.lib.sco_rt_ready, deadline)
                self.connected = True
            self._check(self.lib.sco_rt_send(self.pointer, opcode, payload, len(payload)))
            buffer = ctypes.create_string_buffer(65536)
            size = ctypes.c_uint()
            def receive(pointer):
                return self.lib.sco_rt_receive(pointer, buffer, len(buffer), ctypes.byref(size))
            self._wait(receive, time.monotonic() + 2)
            return bytes(buffer.raw[:size.value])

    def close(self):
        self.stopping.set()
        self.worker.join(timeout=2.1)
        with self.lock:
            if self.pointer:
                self.lib.sco_rt_destroy(self.pointer)
                self.pointer = None
