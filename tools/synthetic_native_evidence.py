#!/usr/bin/env python3
"""PRIVATE SYNTHETIC PROTOTYPE. Validation is structural, not independent verification.
Only invented fixture.* row identifiers are accepted. No live runtime integration,
promotion, publication, or release authority is implemented.
"""
import argparse
import datetime
import json
import re
import sys

HEX = re.compile(r'^[0-9a-f]{64}$')
NATIVE = re.compile(r'^fixture\.[a-z0-9_]+(?:\.[a-z0-9_]+)*$')
PROFILE = re.compile(r'^[a-z0-9][a-z0-9_.-]{0,63}$')
CLAIMS = {'unverified', 'static_resolved', 'in_game_verified', 'rejected'}
KINDS = {'synthetic', 'static_report', 'offline_game_observation', 'review'}
ALLOWED_RECORD = {'native_id', 'build', 'profile', 'status', 'evidence_ids'}

class Invalid(ValueError): pass

def fail(message): raise Invalid(message)
def exact(o, fields, path):
    if not isinstance(o, dict) or set(o) != set(fields):
        fail(f'{path}: expected exactly {sorted(fields)}')
def nonempty(s, path):
    if not isinstance(s, str) or not s.strip(): fail(f'{path}: nonempty string required')
def digest(s, path):
    if not isinstance(s, str) or not HEX.fullmatch(s): fail(f'{path}: lower-case SHA-256 required')
def uint(x, bits, path, *, positive=False):
    if type(x) is not int or not (int(positive) <= x < (1 << bits)): fail(path+': invalid unsigned integer')
def identity(b, p, path):
    exact(b, {'executable', 'manifest'}, path + '.build')
    e=b['executable']; exact(e, {'sha256', 'file_size', 'machine', 'pe_timestamp', 'size_of_image'}, path+'.build.executable')
    digest(e['sha256'], path+'.build.executable.sha256')
    uint(e['file_size'],64,path+'.build.executable.file_size',positive=True)
    for k in ('machine','pe_timestamp','size_of_image'): uint(e[k],32,path+'.build.executable.'+k,positive=True)
    if e['machine'] != 34404: fail(path+': unsupported machine')
    m=b['manifest']; exact(m, {'build'}, path+'.build.manifest')
    if not isinstance(m['build'],list) or len(m['build']) != 4: fail(path+': manifest build requires four parts')
    for x in m['build']: uint(x,32,path+'.build.manifest.build')
    exact(p, {'profile_id', 'revision', 'definition_sha256'}, path + '.profile')
    if not isinstance(p['profile_id'], str) or not PROFILE.fullmatch(p['profile_id']): fail(path + '.profile.profile_id: invalid')
    uint(p['revision'],32,path+'.profile.revision',positive=True)
    digest(p['definition_sha256'], path + '.profile.definition_sha256')

def source(o, path):
    exact(o, {'core_source_sha', 'game_pack_source_sha'}, path)
    for key in o:
        if not isinstance(o[key],str) or not re.fullmatch(r'[0-9a-f]{40}',o[key]): fail(path+'.'+key+': 40 lowercase hex git object id required')

def load(path):
    def pairs(items):
        out = {}
        for k, v in items:
            if k in out: fail(f'duplicate JSON key: {k}')
            out[k] = v
        return out
    with open(path, encoding='utf-8') as f:
        return json.load(f, object_pairs_hook=pairs, parse_constant=lambda x: fail('non-finite JSON number: '+x))

def validate(d):
    exact(d, {'schema_version', 'dataset_kind', 'build', 'profile', 'source', 'evidence', 'records'}, '$')
    if type(d['schema_version']) is not int or d['schema_version'] != 1: fail('unsupported schema version')
    # This prototype never accepts real-world claims, to prevent confusing fixtures with proof.
    if d['dataset_kind'] != 'synthetic': fail('prototype accepts synthetic dataset only')
    identity(d['build'], d['profile'], '$')
    source(d['source'], '$.source')
    evidence = {}
    if not isinstance(d['evidence'], list): fail('evidence must be an array')
    for i, e in enumerate(d['evidence']):
        path = f'evidence[{i}]'
        exact(e, {'id', 'native_id', 'kind', 'artifact_sha256', 'producer', 'created_utc', 'method', 'build', 'profile', 'source'}, path)
        nonempty(e['id'], path+'.id')
        if not isinstance(e['native_id'],str) or not NATIVE.fullmatch(e['native_id']): fail(path+': invalid native id')
        nonempty(e['method'],path+'.method')
        source(e['source'],path+'.source')
        if e['source'] != d['source']: fail(path+': source mismatch')
        if e['id'] in evidence: fail(f'duplicate evidence id: {e["id"]}')
        if not isinstance(e['kind'],str) or e['kind'] not in KINDS: fail(path+': invalid evidence kind')
        # Do not fabricate real claims in the runnable prototype.
        if e['kind'] != 'synthetic': fail(path+': synthetic evidence only')
        digest(e['artifact_sha256'], path+'.artifact_sha256')
        nonempty(e['producer'], path+'.producer')
        if not isinstance(e['created_utc'], str) or not re.fullmatch(r'\d{4}-\d{2}-\d{2}T\d{2}:\d{2}:\d{2}Z', e['created_utc']): fail(path+': UTC timestamp required')
        try: datetime.datetime.strptime(e['created_utc'], '%Y-%m-%dT%H:%M:%SZ')
        except ValueError: fail(path+': invalid UTC calendar timestamp')
        identity(e['build'], e['profile'], path)
        if e['build'] != d['build'] or e['profile'] != d['profile']: fail(path+': build/profile mismatch')
        evidence[e['id']] = e
    if not isinstance(d['records'], list): fail('records must be an array')
    seen = set()
    referenced = set()
    for i, r in enumerate(d['records']):
        path=f'records[{i}]'
        exact(r, ALLOWED_RECORD, path)
        if not isinstance(r['native_id'],str) or not NATIVE.fullmatch(r['native_id']): fail(path+': invalid native id')
        if r['native_id'] in seen: fail(path+': duplicate native id')
        seen.add(r['native_id'])
        identity(r['build'],r['profile'],path)
        if r['build'] != d['build'] or r['profile'] != d['profile']: fail(path+': build/profile mismatch')
        if not isinstance(r['status'],str) or r['status'] not in CLAIMS: fail(path+': unknown status')
        if r['status'] != 'unverified': fail(path+': synthetic data cannot claim real verification')
        ids=r['evidence_ids']
        if not isinstance(ids,list) or not ids or any(not isinstance(x,str) for x in ids) or len(ids)!=len(set(ids)): fail(path+': evidence_ids must be a nonempty unique string array')
        if any(x not in evidence for x in ids): fail(path+': unknown evidence reference')
        if any(evidence[x]['native_id'] != r['native_id'] for x in ids): fail(path+': evidence native mismatch')
        referenced.update(ids)
    orphaned = set(evidence) - referenced
    if orphaned: fail('unreferenced evidence ids: ' + ', '.join(sorted(orphaned)))
    return d

def catalogue(d, index):
    validate(d)
    # Explicit identity joins only; do not infer verification from existing tier.
    if not isinstance(index, list) or any(not isinstance(x, str) or not NATIVE.fullmatch(x) for x in index) or len(set(index)) != len(index): fail('native index must contain unique valid ids')
    rows = {r['native_id']: r for r in d['records']}
    unknown = set(rows) - set(index)
    if unknown: fail('unknown native ids: ' + ', '.join(sorted(unknown)))
    return {'schema_version':1, 'dataset_kind':'synthetic', 'build':d['build'], 'profile':d['profile'], 'source':d['source'],
            'entries':[{'native_id': n, 'evidence_status':rows[n]['status'] if n in rows else 'not_recorded'} for n in sorted(index)]}

def main():
    p=argparse.ArgumentParser(description=__doc__)
    p.add_argument('evidence'); p.add_argument('index', help='JSON list of invented fixture.* native ids'); p.add_argument('--output')
    a=p.parse_args()
    try:
        output=json.dumps(catalogue(load(a.evidence), load(a.index)),indent=2,sort_keys=True)+'\n'
        if a.output:
            with open(a.output,'w',encoding='utf-8',newline='\n') as f: f.write(output)
        else: sys.stdout.buffer.write(output.encode('utf-8'))
    except (Invalid, OSError, ValueError, TypeError) as e:
        print('native evidence: '+str(e),file=sys.stderr); return 1
    return 0
if __name__ == '__main__': sys.exit(main())
