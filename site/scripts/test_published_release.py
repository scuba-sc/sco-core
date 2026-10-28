#!/usr/bin/env python3
"""Offline release selection, identity and fail-closed tests."""
import copy
import hashlib
import json
import unittest
from unittest.mock import patch
from urllib.error import HTTPError
import published_release as r

REPO='scuba-sc/sco-core'; SHA='a'*40; SDK='2.4.0'; GAME='0.8.0'

def build(tag='sdk-v2.4.0-rc.2', sha=SHA, legacy=False):
    names=[f'sco-sdk-{SDK}.zip',f'sco-sdk-core-{SDK}.zip',f'sco-sdk-game-sc-{GAME}.zip',f'sco-sdk-core-source-{SDK}.zip',f'sco-sdk-host-source-sc-{SDK}.zip']
    files={n:('fixture '+n).encode() for n in names+[n[:-4]+'.spdx.json' for n in names]}
    files['SHA256SUMS']=''.join(f'{hashlib.sha256(v).hexdigest()}  {n}\n' for n,v in sorted(files.items())).encode()
    artifacts=[{'name':n,'role':'checksums' if n=='SHA256SUMS' else ('sbom' if n.endswith('.json') else 'zip'), 'size':len(v),'sha256':hashlib.sha256(v).hexdigest(),'artifact_id':1} for n,v in files.items()]
    if not legacy:
        manifest={'schema_version':3,'tag':tag,'source':{'repo':REPO,'sha':sha,'dependencies':[]},'components_manifest_sha256':'b'*64,'builder':{'workflow_path':'.github/workflows/release-candidate.yml','workflow_sha':sha,'run_id':1,'run_attempt':1},'profiles':r.PROFILES,'artifacts':artifacts}
        files['release.json']=json.dumps(manifest).encode()
    assets=[];data={}
    for i,(name,b) in enumerate(sorted(files.items()),1):
        assets.append({'id':i,'name':name,'state':'uploaded','size':len(b),'digest':'sha256:'+hashlib.sha256(b).hexdigest()});data[i]=b
    release={'id':1,'tag_name':tag,'draft':False,'prerelease':'.rc.' in tag,'published_at':'2026-10-28T00:00:00Z','assets':assets}
    return release,data

def fake_git(*args):
    if args[0]=='merge-base': return ''
    if args[1].endswith(':sdk/VERSION'): return SDK
    if args[1].endswith(':include/sc_game_pack.h'): return f'#define SCO_GAME_PACK_VERSION "{GAME}"'
    if args[1].endswith(':include/sco_api.h'): return '#define SCO_API_MAJOR 1\n#define SCO_API_MINOR 1\n'
    raise AssertionError(args)

def client_for(release,data,tag_sha=SHA):
    def call(path,*args):
        if '/releases?per_page=100&page=1' in path:return json.dumps([release]).encode()
        if '/git/ref/tags/' in path:return json.dumps({'ref':'refs/tags/'+release['tag_name'],'object':{'type':'commit','sha':tag_sha}}).encode()
        if '/releases/assets/' in path:return data[int(path.rsplit('/',1)[1])]
        raise AssertionError(path)
    return call

class PublishedTests(unittest.TestCase):
    def test_rank_and_publish_selection(self):
        def item(tag,draft=False,prerelease=True):return {'tag_name':tag,'draft':draft,'prerelease':prerelease,'published_at':'2026-01-01'}
        self.assertEqual(r.select([[item('sdk-v2.4.0-rc.1',prerelease=False),item('sdk-v2.4.0-rc.2')]])['tag_name'],'sdk-v2.4.0-rc.2')
        self.assertEqual(r.select([[item('sdk-v2.4.0-rc.1'),item('sdk-v2.4.0-rc.2',prerelease=False)]])['tag_name'],'sdk-v2.4.0-rc.2')
        self.assertEqual(r.select([[item('sdk-v2.4.0-rc.2'),item('sdk-v2.4.0')]])['tag_name'],'sdk-v2.4.0')
        self.assertEqual(r.select([[item('sdk-v2.4.0-rc.1'),item('sdk-v2.4.0-rc.2',draft=True)]])['tag_name'],'sdk-v2.4.0-rc.1')
        with self.assertRaises(r.Refusal):r.select([[item('sdk-v2.4.0-rc.2',draft=True)]])

    def test_rc2_and_legacy_rc1_verified(self):
        rel,data=build(); v=r.resolve(REPO,client_for(rel,data),fake_git)
        self.assertEqual((v['tag'],v['source_sha'],v['game_pack_version']),('sdk-v2.4.0-rc.2',SHA,GAME))
        rel['prerelease']=False
        self.assertEqual(r.resolve(REPO,client_for(rel,data),fake_git)['tag'], 'sdk-v2.4.0-rc.2')
        rel,data=build(tag=r.RC1[0],sha=r.RC1[1],legacy=True)
        sums = next(a for a in rel['assets'] if a['name'] == 'SHA256SUMS')
        with patch.object(r, 'RC1_SUMS_SHA256', sums['digest'][7:]):
            v=r.resolve(REPO,client_for(rel,data,r.RC1[1]),fake_git)
        self.assertEqual(v['tag'],r.RC1[0])

    def test_missing_or_changed_assets_refuse(self):
        rel,data=build(); rel['assets'].pop()
        with self.assertRaises(r.Refusal):r.resolve(REPO,client_for(rel,data),fake_git)
        rel,data=build(); rel['assets'][0]['digest']='sha256:'+'0'*64
        with self.assertRaises(r.Refusal):r.resolve(REPO,client_for(rel,data),fake_git)
        rel,data=build();rel['assets'][0]['state']='new'
        with self.assertRaises(r.Refusal):r.resolve(REPO,client_for(rel,data),fake_git)

    def test_tag_source_or_manifest_mismatch_refuses(self):
        rel,data=build()
        with self.assertRaises(r.Refusal):r.resolve(REPO,client_for(rel,data,'c'*40),fake_git)
        rel,data=build(tag='sdk-v2.4.0-rc.3',legacy=True)
        with self.assertRaises(r.Refusal):r.resolve(REPO,client_for(rel,data),fake_git)
        rel,data=build(tag='sdk-v2.4.0');rel['prerelease']=True
        with self.assertRaises(r.Refusal):r.resolve(REPO,client_for(rel,data),fake_git)

    def test_failed_api_never_returns_fallback(self):
        def fail(*args):raise r.Refusal('unavailable')
        with self.assertRaises(r.Refusal):r.resolve(REPO,fail,fake_git)

    def test_live_site_refuses_stale_and_missing_identity(self):
        new={'tag':'sdk-v2.4.0-rc.2'}; old={'tag':'sdk-v2.4.0-rc.1'}
        html='<section data-track="published" data-download-tag="sdk-v2.4.0-rc.2">'
        r.require_not_older(new,html)
        with self.assertRaises(r.Refusal):r.require_not_older(old,html)
        with self.assertRaises(r.Refusal):r.require_not_older(new,'<html></html>')

    def test_legacy_metadata_bootstrap_is_rc1_only(self):
        class Response:
            def __init__(self, body): self.body = body
            def __enter__(self): return self
            def __exit__(self, *args): pass
            def read(self, *args): return self.body
        def run(live_tag):
            def open_(request, timeout):
                if request.full_url.endswith('published-release.json'):
                    raise HTTPError(request.full_url, 404, 'missing', {}, None)
                return Response(f'<section data-track="published" data-download-tag="{live_tag}">'.encode())
            with patch.object(r, 'urlopen', open_):
                r.check_live({'tag':'sdk-v2.4.0-rc.2'}, 'https://example.test/')
        run('sdk-v2.4.0-rc.1')
        with self.assertRaises(r.Refusal): run('sdk-v2.4.0-rc.2')

    def test_same_tag_replaced_source_or_assets_refused(self):
        rel,data=build(); candidate=r.resolve(REPO,client_for(rel,data),fake_git)
        class Response:
            def __init__(self, body): self.body = body
            def __enter__(self): return self
            def __exit__(self, *args): pass
            def read(self, *args): return self.body
        html=b'<section data-track="published" data-download-tag="sdk-v2.4.0-rc.2">'
        for key, value in (('source_sha','b'*40), ('release_id', 2), ('asset_digests', {})):
            previous=copy.deepcopy(candidate);previous[key]=value
            def open_(request, timeout):
                return Response(json.dumps(previous).encode() if request.full_url.endswith('published-release.json') else html)
            with self.subTest(key=key), patch.object(r, 'urlopen', open_), self.assertRaises(r.Refusal):
                r.check_live(candidate, 'https://example.test/')

    def test_redeployed_source_and_release_are_separate(self):
        import gen
        rel,data=build(); meta=r.resolve(REPO,client_for(rel,data),fake_git)
        saved=(gen.PUBLISHED_RELEASE,gen.PUBLISHED_DOWNLOAD_TAG,gen.PUBLISHED_METADATA)
        try:
            gen.use_published_metadata(meta)
            result=gen.banner({'sdk_version':SDK,'game_pack_version':GAME,'source_sha':'d'*40},None,'Guides')
            self.assertIn('d'*40,result);self.assertIn(SHA,result);self.assertIn('sdk-v2.4.0-rc.2',result)
        finally:gen.PUBLISHED_RELEASE,gen.PUBLISHED_DOWNLOAD_TAG,gen.PUBLISHED_METADATA=saved

    def test_failed_promotion_cannot_deploy(self):
        workflow=(r.Path(__file__).resolve().parents[2]/'.github/workflows/docs-site.yml').read_text()
        self.assertIn("github.event.workflow_run.conclusion == 'success'",workflow)
        self.assertIn("github.event.workflow_run.event == 'workflow_dispatch'",workflow)
        self.assertIn("github.event.workflow_run.path == '.github/workflows/release.yml'",workflow)
        self.assertIn('Refuse a stale source or changed published release',workflow)

if __name__=='__main__':unittest.main()
