#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-only
"""No-game, actual stdio launcher + compiled native peer + real host/mapping fixture.

Synthetic collector default is logic evidence, never OS-security evidence.
"""
import argparse
import hashlib
import json
import os
from pathlib import Path
import queue
import subprocess
import sys
import tempfile
import threading
import time

HERE = Path(__file__).resolve().parent
NAMES = ('runtime_get_host_info', 'runtime_list_plugins', 'runtime_tail_plugin_log',
         'runtime_list_service_status', 'runtime_get_resolution_status', 'runtime_get_game_status')

class Child:
    def __init__(self, args):
        self.proc = subprocess.Popen(args, stdin=subprocess.PIPE, stdout=subprocess.PIPE, stderr=subprocess.PIPE)
        self.lines = queue.Queue()
        def read():
            for line in iter(self.proc.stdout.readline, b''):
                self.lines.put(line)
            self.lines.put(None)
        self.thread = threading.Thread(target=read, daemon=True); self.thread.start()
    def send(self, message):
        if isinstance(message, dict):
            message = json.dumps(message)
        self.proc.stdin.write(message.encode() + b'\n'); self.proc.stdin.flush()
    def line(self):
        line = self.lines.get(timeout=5)
        if line is None:
            raise AssertionError('Fixture ended before response')
        return line.decode().strip()
    def close(self, message=None):
        if self.proc.poll() is None:
            if message:
                self.send(message)
            self.proc.stdin.close()
            try:
                self.proc.wait(timeout=5)
            except subprocess.TimeoutExpired:
                self.proc.kill(); self.proc.wait(timeout=2)
        assert self.proc.returncode == 0, self.proc.stderr.read().decode(errors='replace')

class Mcp(Child):
    def __init__(self, args):
        super().__init__(args); self.ident = 0
        self.rpc('initialize', {'protocolVersion': '2025-11-25', 'capabilities': {},
                               'clientInfo': {'name': 'synthetic-local-fixture', 'version': '1'}})
        self.send({'jsonrpc': '2.0', 'method': 'notifications/initialized'})
    def rpc(self, method, params=None):
        self.ident += 1
        frame = {'jsonrpc': '2.0', 'id': self.ident, 'method': method}
        if params is not None:
            frame['params'] = params
        self.send(frame); answer = json.loads(self.line())
        assert answer['id'] == self.ident
        return answer
    def tool(self, name, arguments=None):
        answer = self.rpc('tools/call', {'name': name, 'arguments': arguments or {}})
        assert 'result' in answer, answer
        result = answer['result']
        assert json.loads(result['content'][0]['text']) == result['structuredContent']
        return result['structuredContent']

def main():
    p = argparse.ArgumentParser(description=__doc__)
    p.add_argument('--host', required=True, type=Path)
    p.add_argument('--adapter', required=True, type=Path)
    p.add_argument('--root', required=True, type=Path)
    p.add_argument('--collector-log'); p.add_argument('--collector-source')
    p.add_argument('--collector-capacity', default='8388608')
    a = p.parse_args()
    directory = Path(tempfile.mkdtemp(prefix='sco-runtime-integration-', dir=os.environ.get('TMPDIR')))
    host_args = [str(a.host.resolve()), str(directory), str(a.root.resolve())]
    if a.collector_log:
        assert a.collector_source
        host_args += [a.collector_log, a.collector_source, a.collector_capacity]
    host = Child(host_args); primary = secondary = disabled = None
    try:
        assert host.line() == 'ready'
        digest = hashlib.sha256(a.adapter.read_bytes()).hexdigest()
        args = [sys.executable, str(HERE / 'server.py'), '--enable-runtime', '--disclosure-consent',
                '--adapter', str(a.adapter.resolve()), '--adapter-sha256', digest,
                '--credential-file', str(directory / 'session.json')]
        for name in NAMES:
            args += ['--allow-tool', name]
        disabled = Mcp([sys.executable, str(HERE / 'server.py')])
        assert disabled.rpc('tools/list')['result']['tools'] == []
        disabled.close(); disabled = None
        primary = Mcp(args)
        assert [t['name'] for t in primary.rpc('tools/list')['result']['tools']] == list(NAMES)
        session = None
        for i, name in enumerate(NAMES):
            if i == 5:
                time.sleep(1.05) # approved independent burst budget
            arguments = {'plugin_id': 'fixture_loaded'} if name == NAMES[2] else {}
            value = primary.tool(name, arguments)
            assert value['status'] in ('ok', 'stale', 'restart_required'), value
            provenance = value['provenance']
            assert provenance['source'] == 'runtime' and provenance['host_build'] == 'fixture-host-1'
            assert provenance['source_sha'] == '0' * 40 and provenance['age_ms'] >= 0
            session = session or provenance['session_id']; assert provenance['session_id'] == session
            if name == NAMES[1]:
                states = {item['plugin_id']: item['state'] for item in value['data']['items']}
                assert states['fixture_loaded'] == 'loaded' and states['fixture_refused'] == 'refused'
                assert states['fixture_faulted'] == 'faulted'
            if name == NAMES[2]:
                assert value['data']['redacted_count'] >= 1
                assert all(e['untrusted_content'] and 'ignore previous' not in e['message'] for e in value['data']['entries'])
        secondary = Mcp(args)
        assert secondary.tool(NAMES[0])['error']['code'] == 'peer_busy'
        secondary.close(); secondary = None
        hostile = primary.rpc('tools/call', {'name': 'runtime_invoke', 'arguments': {'name': 'fixture.mutation'}})
        assert hostile['error']['code'] == -32602
        host.send('metrics'); metrics = json.loads(host.line())
        assert metrics['mutation_callbacks'] == metrics['service_handouts'] == 0
        assert metrics['mapping_bytes'] == 262144 and metrics['max_request_bytes'] == 4096
        primary.close(); primary = None
        host.send('stop'); assert host.line() == 'ok'
        assert not (directory / 'session.json').exists()
        host.send('restart'); assert host.line() == 'ok'
        primary = Mcp(args); new = primary.tool(NAMES[0]); assert new['provenance']['session_id'] != session
        if not a.collector_log:
            host.send('deny'); assert host.line() == 'ok'
            denied = primary.tool(NAMES[0]); assert denied['error']['code'] == 'audit_unavailable' and denied['data'] is None
        else:
            # Actual non-admin append/clear/retention proof is a separate dedicated executable.
            print('Windows collector used; effective-identity destructive-rights fixture is still separately required')
        primary.close(); primary = None
        host.send('revoke'); assert host.line() == 'ok'
        host.send('stop'); assert host.line() == 'ok'
        print(json.dumps({'fixture': 'no-game-real-dispatcher-native-peer-stdio', 'metrics': metrics,
                          'windows_security': 'NOT VERIFIED by synthetic collector', 'game': 'not run in game'}))
    finally:
        for child in (disabled, secondary, primary):
            if child is not None:
                child.close()
        host.close('quit')
        if directory.exists():
            directory.rmdir()

if __name__ == '__main__':
    main()
