#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-only
"""Reproduce static SDK discovery over real stdio; no runtime connection.

Freshness negatives test this tutorial's evidence-consumption rule against the
reviewed synthetic schema vectors, NOT a live host/adapter/server implementation.
"""
import argparse
import copy
import json
from pathlib import Path
import re
import subprocess
import sys

HERE = Path(__file__).resolve().parent
ROOT = HERE.parents[1]
ALLOWLIST = ('search_api', 'get_symbol', 'list_services', 'get_example',
             'get_capability_status', 'check_compatibility')


def require(condition, reason):
    if not condition:
        raise RuntimeError(reason)


def checkout_coordinates():
    """The SDK and game-pack versions this checkout declares, so the tutorial never pins them."""
    sdk = (ROOT / 'sdk' / 'VERSION').read_text(encoding='utf-8').strip()
    header = (ROOT / 'include' / 'sc_game_pack.h').read_text(encoding='utf-8')
    game_pack = re.search(r'^#define\s+SCO_GAME_PACK_VERSION\s+"([^"]+)"', header, re.MULTILINE)
    require(game_pack is not None, 'SCO_GAME_PACK_VERSION not found')
    return sdk, game_pack.group(1)


def require_current_observation(result, session_id, epoch):
    """Evidence rule only; schema validation/authentication belong to the runtime.

    The expected session/epoch must come from the authenticated current host, not
    from this response or catalogue SHA. We never feed this helper live data here.
    """
    provenance = result.get('provenance', {})
    age = provenance.get('age_ms')
    require(result.get('status') == 'ok' and result.get('data') is not None,
            'not a fresh usable observation')
    require(provenance.get('source') == 'runtime' and provenance.get('schema_version') == 1,
            'not runtime provenance')
    require(bool(session_id) and bool(epoch) and provenance.get('session_id') == session_id
            and provenance.get('epoch') == epoch, 'session changed')
    require(type(age) is int and 0 <= age <= 1000, 'stale runtime observation')
    require(not result['data'].get('restart_required'), 'restart required')


def freshness_negatives():
    fixture = json.loads((HERE / 'runtime_schema/responses.json').read_text())['runtime_get_host_info'][0]
    p = fixture['provenance']
    require_current_observation(fixture, p['session_id'], p['epoch'])
    cases = []
    stale = copy.deepcopy(fixture); stale['provenance']['age_ms'] = 1001
    cases.append(('stale age despite ok status', stale, p['session_id'], p['epoch']))
    stale = copy.deepcopy(fixture); stale['status'] = 'stale'
    cases.append(('stale status', stale, p['session_id'], p['epoch']))
    cases.append(('old session', fixture, 'fedcba9876543210fedcba9876543210', p['epoch']))
    cases.append(('old epoch', fixture, p['session_id'], '2'))
    for label, result, session, epoch in cases:
        try:
            require_current_observation(result, session, epoch)
        except RuntimeError as error:
            print(f'tutorial evidence refused: {label} ({error})')
        else:
            raise RuntimeError('Negative control accepted: ' + label)


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--catalogue', required=True, type=Path)
    parser.add_argument('--source-sha', required=True)
    parser.add_argument('--transcript', type=Path, help='New JSON file for actual stdio requests/responses')
    sdk_version, game_pack_version = checkout_coordinates()
    parser.add_argument('--sdk-version', default=sdk_version,
                        help="Expected catalogue sdk_version (default: this checkout's sdk/VERSION)")
    parser.add_argument('--game-pack-version', default=game_pack_version,
                        help="Expected catalogue game_pack_version (default: this checkout's SCO_GAME_PACK_VERSION)")
    args = parser.parse_args()
    require(args.transcript is None or not args.transcript.exists(), 'Transcript path already exists')
    messages = []
    def request(method, params):
        message = {'jsonrpc': '2.0', 'id': len(messages) + 1, 'method': method, 'params': params}
        messages.append(message)
        return message['id']
    request('initialize', {'protocolVersion': '2025-11-25', 'capabilities': {},
                          'clientInfo': {'name': 'sdk-tutorial', 'version': '1.0.0'}})
    messages.append({'jsonrpc': '2.0', 'method': 'notifications/initialized'})
    listed = request('tools/list', {})
    def call(name, arguments):
        return request('tools/call', {'name': name, 'arguments': arguments})
    call('list_services', {})
    search = call('search_api', {'query': 'spawn_npc', 'service': 'game.actors', 'language': 'cpp'})
    symbols = ['sc_actors_v1.spawn_npc', 'sc_actors_v1.despawn',
               'sc_actors_v1.health', 'sc_entities_v1.get_transform', 'sc_spatial_v1.player_pose']
    discovered = []
    for symbol in symbols:
        discovered.append((symbol, call('get_symbol', {'symbol_or_id': symbol})))
        call('get_example', {'symbol_or_id': symbol, 'language': 'cpp'})
        call('get_capability_status', {'symbol_or_id': symbol})
    compatibility = call('check_compatibility', {'sdk_version': args.sdk_version,
                                                 'game_pack_version': args.game_pack_version})
    # No runtime flag or adapter. Runtime and mutating names must stay unavailable.
    refused = [call(name, {}) for name in ('runtime_get_host_info', 'invoke_command', 'sco.reload')]
    completed = subprocess.run([sys.executable, str(HERE / 'server.py'), '--catalogue', str(args.catalogue)],
                               input=''.join(json.dumps(m) + '\n' for m in messages),
                               text=True, capture_output=True, timeout=30, check=True)
    responses = [json.loads(line) for line in completed.stdout.splitlines()]
    indexed = {r['id']: r for r in responses}
    require(len(responses) == len(messages) - 1, 'Missing/extra stdio responses')
    require({t['name'] for t in indexed[listed]['result']['tools']} == set(ALLOWLIST),
            'Static allowlist drift; do not guess runtime tools')
    for response in responses:
        if response['id'] in refused:
            require(response.get('error', {}).get('code') == -32602, 'Unsupported tool did not refuse')
        else:
            require('error' not in response and not response['result'].get('isError'),
                    'Discovery failed: ' + json.dumps(response))
            if 'structuredContent' in response['result']:
                require(response['result']['structuredContent']['provenance']['source_sha'] == args.source_sha,
                        'Stale catalogue source SHA')
    require(indexed[search]['result']['structuredContent']['total_matches'] > 0, 'Empty service search')
    for symbol, identifier in discovered:
        data = indexed[identifier]['result']['structuredContent']
        record = data['record']
        require(record['symbol'] == symbol and record['member_extent'] <= record['table_size'],
                'Wrong contract/member extent')
        print(f"contract {symbol}: {record['version_macro']}; extent={record['member_extent']}; "
              f"cpp={record['language_mappings']['cpp']['status']}; examples={len(record['examples'])}")
    require(all(indexed[compatibility]['result']['structuredContent']['coordinate_matches'].values()),
            'SDK/game-pack coordinates differ')
    freshness_negatives()
    if args.transcript:
        args.transcript.parent.mkdir(parents=True, exist_ok=True)
        args.transcript.write_text(json.dumps({'requests': messages, 'responses': responses}, indent=2) + '\n')
    print('static MCP tutorial: PASS; unsupported tools refused; synthetic stale/session negatives PASS')
    print('Runtime host/adapter, Windows, gameplay: NOT VERIFIED')


if __name__ == '__main__':
    main()
