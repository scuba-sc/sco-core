#!/usr/bin/env python3
"""Resolve a published SDK release, refusing incomplete or inconsistent metadata."""
import argparse
import hashlib
import json
import re
import subprocess
import sys
from html.parser import HTMLParser
from pathlib import Path
from urllib.error import HTTPError, URLError
from urllib.request import Request, urlopen

TAG = re.compile(r'^sdk-v(\d+)\.(\d+)\.(\d+)(?:-rc\.([1-9]\d*))?$')
SHA = re.compile(r'^[0-9a-f]{40}$')
DIGEST = re.compile(r'^sha256:([0-9a-f]{64})$')
PROFILES = ['core', 'game-sc', 'legacy', 'core-source', 'host-source-sc']
# The one earlier published SDK has no release.json. This is historical, not a live selection.
RC1 = ('sdk-v2.4.0-rc.1', '48d7055ef49e1b073ec59e4ce8ded7a6006329eb')
RC1_SUMS_SHA256 = 'b4ca1d6bddc3313ca49cc108fc6eabc0c013323c1f37161dbaf8400b828dcfe2'

class Refusal(ValueError):
    pass

def gh(*args):
    try:
        return subprocess.check_output(['gh', 'api', *args], stderr=subprocess.PIPE, timeout=90)
    except (OSError, subprocess.SubprocessError) as e:
        raise Refusal('GitHub API unavailable') from e

def git(*args):
    try:
        return subprocess.check_output(['git', *args], stderr=subprocess.PIPE, timeout=30).decode().strip()
    except (OSError, subprocess.SubprocessError) as e:
        raise Refusal('release source unavailable in checkout') from e

def obj(data):
    def pairs(items):
        d = {}
        for k, v in items:
            if k in d: raise Refusal('duplicate JSON key')
            d[k] = v
        return d
    try: return json.loads(data, object_pairs_hook=pairs)
    except (ValueError, UnicodeError) as e: raise Refusal('invalid JSON') from e

def rank(tag):
    m = TAG.fullmatch(tag) if isinstance(tag, str) else None
    if not m: return None
    a, b, c = map(int, m.group(1, 2, 3)); rc = m.group(4)
    return (a, b, c, 1 if rc is None else 0, 0 if rc is None else int(rc))

def select(pages):
    releases = []
    for page in pages:
        if not isinstance(page, list): raise Refusal('invalid release list')
        for r in page:
            if not isinstance(r, dict): raise Refusal('invalid release record')
            if rank(r.get('tag_name')) is not None and r.get('draft') is False and r.get('published_at'):
                releases.append(r)
    if not releases: raise Refusal('no published SDK release')
    tags = [r['tag_name'] for r in releases]
    if len(tags) != len(set(tags)): raise Refusal('duplicate release tag')
    return max(releases, key=lambda r: rank(r['tag_name']))

class PublishedTrack(HTMLParser):
    def __init__(self):
        super().__init__(); self.tags = []

    def handle_starttag(self, tag, attrs):
        a = dict(attrs)
        if tag == 'section' and a.get('data-track') == 'published':
            self.tags.append(a.get('data-download-tag'))

def require_not_older(candidate, html):
    parser = PublishedTrack(); parser.feed(html)
    if len(parser.tags) != 1 or rank(parser.tags[0]) is None:
        raise Refusal('live published release identity unavailable')
    if rank(candidate['tag']) < rank(parser.tags[0]):
        raise Refusal('resolved release is older than the live site')
    return parser.tags[0]

def check_live(candidate, url):
    try:
        req = Request(url, headers={'Cache-Control': 'no-cache', 'User-Agent': 'sco-docs-release-check'})
        with urlopen(req, timeout=20) as response:
            html = response.read(4 * 1024 * 1024 + 1)
        if len(html) > 4 * 1024 * 1024: raise Refusal('live site response too large')
        live_tag = require_not_older(candidate, html.decode('utf-8'))
        try:
            with urlopen(Request(url.rstrip('/') + '/published-release.json', headers={'Cache-Control': 'no-cache'}), timeout=20) as response:
                previous = obj(response.read(64 * 1024 + 1))
            if (not isinstance(previous, dict) or rank(previous.get('tag')) is None
                    or not isinstance(previous.get('source_sha'), str) or not SHA.fullmatch(previous['source_sha'])):
                raise Refusal('live release metadata malformed')
            if previous['tag'] != live_tag:
                raise Refusal('live release metadata does not match page')
            if rank(candidate['tag']) < rank(previous['tag']):
                raise Refusal('resolved release is older than live metadata')
            if candidate['tag'] == previous['tag'] and any(candidate.get(k) != previous.get(k) for k in ('source_sha', 'release_id', 'asset_digests')):
                raise Refusal('published release identity changed')
        except HTTPError as e:
            if e.code != 404 or live_tag != RC1[0]: raise
    except (OSError, UnicodeError, URLError) as e:
        raise Refusal('live site unavailable; keep existing deployment') from e

def peel(repo, tag, client):
    ref = obj(client(f'repos/{repo}/git/ref/tags/{tag}'))
    if ref.get('ref') != 'refs/tags/' + tag: raise Refusal('tag ref mismatch')
    x = ref.get('object', {})
    for _ in range(5):
        sha = x.get('sha'); kind = x.get('type')
        if not isinstance(sha, str) or not SHA.fullmatch(sha): raise Refusal('invalid tag object')
        if kind == 'commit': return sha
        if kind != 'tag': break
        x = obj(client(f'repos/{repo}/git/tags/{sha}')).get('object', {})
    raise Refusal('tag does not peel to a commit')

def versions(sha, git_call):
    sdk = git_call('show', sha + ':sdk/VERSION').strip()
    header = git_call('show', sha + ':include/sc_game_pack.h')
    game = re.search(r'^#define\s+SCO_GAME_PACK_VERSION\s+"(\d+\.\d+\.\d+)"', header, re.M)
    api = git_call('show', sha + ':include/sco_api.h')
    major = re.search(r'^#define\s+SCO_API_MAJOR\s+(\d+)', api, re.M)
    minor = re.search(r'^#define\s+SCO_API_MINOR\s+(\d+)', api, re.M)
    if not re.fullmatch(r'\d+\.\d+\.\d+', sdk) or not game or not major or not minor:
        raise Refusal('source version coordinates unavailable')
    git_call('merge-base', '--is-ancestor', sha, 'HEAD')
    return sdk, game.group(1), major.group(1) + '.' + minor.group(1)

def resolve(repo, client=gh, git_call=git):
    pages = []
    for page in range(1, 101):
        values = obj(client(f'repos/{repo}/releases?per_page=100&page={page}'))
        if not isinstance(values, list): raise Refusal('invalid release list')
        pages.append(values)
        if len(values) < 100: break
    else: raise Refusal('release list too large')
    release = select(pages); tag = release['tag_name']; sha = peel(repo, tag, client)
    if type(release.get('prerelease')) is not bool or (rank(tag)[3] == 1 and release['prerelease']):
        raise Refusal('release status conflicts with its tag')
    if type(release.get('id')) != int or release['id'] <= 0: raise Refusal('invalid release id')
    sdk, game, abi = versions(sha, git_call)
    if '.'.join(str(x) for x in rank(tag)[:3]) != sdk: raise Refusal('tag/source SDK mismatch')
    assets = release.get('assets')
    if not isinstance(assets, list): raise Refusal('asset list unavailable')
    assets_by_name = {}
    for a in assets:
        if not isinstance(a, dict): raise Refusal('invalid asset')
        name=a.get('name'); digest=a.get('digest'); size=a.get('size'); aid=a.get('id')
        if not isinstance(name,str) or name in assets_by_name or a.get('state')!='uploaded' or type(aid)!=int or aid<=0 or type(size)!=int or size<=0 or not isinstance(digest,str) or not DIGEST.fullmatch(digest): raise Refusal('invalid, duplicate or incomplete asset')
        assets_by_name[name]=a
    names = {'core': f'sco-sdk-core-{sdk}.zip', 'game-sc': f'sco-sdk-game-sc-{game}.zip', 'legacy': f'sco-sdk-{sdk}.zip', 'core-source': f'sco-sdk-core-source-{sdk}.zip', 'host-source-sc': f'sco-sdk-host-source-sc-{sdk}.zip'}
    required = set(names.values()) | {n[:-4]+'.spdx.json' for n in names.values()} | {'SHA256SUMS'}
    if set(assets_by_name) not in (required, required | {'release.json'}): raise Refusal('release asset set incomplete or unexpected')
    def read(name):
        a=assets_by_name[name]; b=client(f'repos/{repo}/releases/assets/{a["id"]}', '-H', 'Accept: application/octet-stream')
        if len(b)!=a['size'] or 'sha256:'+hashlib.sha256(b).hexdigest()!=a['digest']: raise Refusal('asset bytes differ from published metadata')
        return b
    sums = {}
    try:
        for line in read('SHA256SUMS').decode('ascii').splitlines():
            m=re.fullmatch(r'([0-9a-f]{64})\s+\*?([A-Za-z0-9][A-Za-z0-9._-]*)',line)
            if not m or m.group(2) in sums: raise Refusal('invalid checksums')
            sums[m.group(2)]=m.group(1)
    except UnicodeError as e: raise Refusal('invalid checksums') from e
    if set(sums)!=required-{'SHA256SUMS'}: raise Refusal('checksum inventory mismatch')
    for n, digest in sums.items():
        if assets_by_name[n]['digest']!='sha256:'+digest: raise Refusal('asset digest mismatch')
    if 'release.json' in assets_by_name:
        # Validate the manifest as well as GitHub's uploaded digests and SHA256SUMS.
        sys.path.insert(0,str(Path(__file__).resolve().parents[2]/'tools'))
        import release_verify
        manifest=obj(read('release.json'))
        try: release_verify.record_shape(manifest)
        except Exception as e: raise Refusal('invalid release manifest') from e
        if manifest['tag']!=tag or manifest['source']['repo']!=repo or manifest['source']['sha']!=sha: raise Refusal('manifest identity mismatch')
        listed={a['name']:a for a in manifest['artifacts']}
        if len(listed)!=len(manifest['artifacts']) or set(listed)!=required: raise Refusal('manifest inventory mismatch')
        for n,a in listed.items():
            if a['size']!=assets_by_name[n]['size'] or 'sha256:'+a['sha256']!=assets_by_name[n]['digest']: raise Refusal('manifest asset mismatch')
    elif (tag,sha)!=RC1:
        raise Refusal('release manifest missing')
    elif assets_by_name['SHA256SUMS']['digest'] != 'sha256:' + RC1_SUMS_SHA256:
        raise Refusal('historical RC1 checksum identity changed')
    return {'schema_version':1,'tag':tag,'source_sha':sha,'release_id':release['id'],
            'sdk_version':sdk,'game_pack_version':game,'plugin_abi':abi,'profiles':PROFILES,
            'archives':names,'asset_digests':{n:a['digest'] for n,a in sorted(assets_by_name.items())}}

def main():
    p=argparse.ArgumentParser(description=__doc__);p.add_argument('--repo',required=True);p.add_argument('--out',type=Path,required=True);p.add_argument('--live-url');args=p.parse_args()
    if not re.fullmatch(r'[A-Za-z0-9][A-Za-z0-9-]*/[A-Za-z0-9._-]+',args.repo): raise Refusal('invalid repository')
    data=resolve(args.repo)
    if args.live_url: check_live(data, args.live_url)
    args.out.parent.mkdir(parents=True,exist_ok=True);args.out.write_text(json.dumps(data,sort_keys=True,indent=2)+'\n');print('Verified published release',data['tag'],'source',data['source_sha'])
if __name__=='__main__':
    try: main()
    except Refusal as e: print('published release refused:',e,file=sys.stderr);raise SystemExit(1)
