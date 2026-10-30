#!/usr/bin/env python3
"""Synthetic valid-input differential evidence, with explicit per-row coverage gaps.
Run: python3 tests/issue302_valid_parity.py --out <fresh-scratch-directory>
No proprietary image is read. Sources are never edited; baseline is git archive.
"""
import argparse
import collections
import hashlib
import json
import os
from pathlib import Path
import re
import subprocess
import tarfile
import tempfile

from issue302_fixtures_custom import setup as custom_setup
from issue302_fixtures_offline import setup as offline_setup
from issue302_fixtures_world import setup as world_setup

BASELINE = '5b010dc0697535f10e9264235494b667ea850ea6'
ROOT = Path(__file__).resolve().parents[1]

def run(cmd, **kw):
    return subprocess.run([str(x) for x in cmd], check=True, text=True, **kw)

COMMON = r'''
#include "sco/signatures.h"
#include <cstdio>
#include <cstring>
#include <cstdarg>
#include <map>
#include <string>
#include <vector>
namespace parity302 {
extern std::map<std::string,uint8_t*> deps;
void emit(const char*,const char*,const char*,const sco::Image&,sco::SigResult);
inline void put(uint8_t* at,const char* pattern) {
    while (*pattern) {
        while (*pattern==' ') ++pattern;
        if (!*pattern) break;
        if (*pattern=='?') { *at++=0; while (*pattern=='?') ++pattern; }
        else { char token[3]={pattern[0],pattern[1],0}; *at++=static_cast<uint8_t>(strtoul(token,nullptr,16)); pattern+=2; }
    }
}
inline void rel(uint8_t* at,size_t disp,size_t len,const uint8_t* to) { int32_t d=static_cast<int32_t>(to-(at+len));memcpy(at+disp,&d,4); }
inline uint8_t* str(sco::Image& img,size_t off,const char* s) { auto* p=img.rdata.base+off;p[-1]=0;strcpy(reinterpret_cast<char*>(p),s);return p; }
inline void pdata(sco::Image& img,uint8_t* fn) {img.pdata={img.base+0x33000,12};uint32_t a[3]={static_cast<uint32_t>(fn-img.base),static_cast<uint32_t>(fn-img.base+0x2000),0x34000};memcpy(img.pdata.base,a,12);img.base[0x34000]=1;}
inline void extra(const char* id,sco::Image& img,uint8_t* at) {
    if(!strcmp(id,"npc.direct_remove_call")) pdata(img,at);
    if(!strcmp(id,"asop.on_request_open"))rel(at+0x95e,1,5,at+0xdc0);
    if(!strcmp(id,"asop.shard_gate_open"))deps["asop.on_request_open"]=at-0x21;
    if(!strcmp(id,"asop.shard_gate_validation")){pdata(img,at);deps["asop.validate_caller"]=at;}
    if(!strcmp(id,"actor.health_site")||!strcmp(id,"actor.state_site")) {
        put(img.text.base+0x5000,"48 8B 81 08 02 00 00 48 05 A0 74 00 00 C3");rel(at,1,5,img.text.base+0x5000);
        if(!strcmp(id,"actor.health_site")){rel(at+0x1d,1,5,img.text.base+0x5000);rel(at+0x0d,1,5,img.text.base+0x5100);rel(at+0x2a,1,5,img.text.base+0x5100);}
    }
    if(!strcmp(id,"event.player_spawn")){auto* s=str(img,0x100,"void __cdecl SCigEventDispatcher::QueueEvent<struct SPI_Player_OnSpawn>(const struct SPI_Player_OnSpawn &)");rel(at+0x60,3,7,s);pdata(img,at);}
    if(!strcmp(id,"offline.asop_gate_open")||!strcmp(id,"offline.asop_gate_validation")){
        deps["offline.is_online_flag"]=img.rdata.base+0x800;
        if(!strcmp(id,"offline.asop_gate_open"))rel(at+7,3,7,img.rdata.base+0x801);
        else{rel(at,2,7,img.rdata.base+0x801);rel(at+27,3,7,str(img,0x100,"Can only perform ASOP operations in the PU."));}
    }
    if(!strcmp(id,"offline.restricted_area_impound")){rel(at+0x59,3,7,str(img,0x100,"$$: Vehicle '$$' [$$] impounded (or destroyed) by restricted area '$$' [$$]"));rel(at+0x79,3,7,str(img,0x300,"Boundary Violation"));}
    if(!strcmp(id,"fleet.entitlements_result"))rel(at+0x55,3,7,str(img,0x100,"QueryEntitlements failed with error: $$"));
}
}
'''

DRIVER = r'''
#include "sco/game/signatures.h"
#include <cstdlib>
namespace parity302 {
std::map<std::string,uint8_t*> deps;
void quote(const char* s) {
    putchar('"');
    if(s) for(;*s;++s) { if(*s=='"'||*s=='\\') putchar('\\'); if(*s=='\n') printf("\\n"); else putchar(*s); }
    putchar('"');
}
void emit(const char* file,const char* id,const char* fixture,const sco::Image& img,sco::SigResult r) {
    printf("{\"file\":");quote(file);printf(",\"id\":");quote(id);printf(",\"fixture\":");quote(fixture);
    printf(",\"state\":");quote(sco::SigStateName(r.state));printf(",\"rva\":");
    if(r.at) printf("%lld",static_cast<long long>(reinterpret_cast<uintptr_t>(r.at)-reinterpret_cast<uintptr_t>(img.base))); else printf("null");
    printf(",\"matches\":%d,\"reason\":",r.matches);if(r.why)quote(r.why);else printf("null");
    uint64_t hash=14695981039346656037ULL;
    for(size_t i=0;i<img.size;++i){uint8_t byte=img.base[i];const uintptr_t p=reinterpret_cast<uintptr_t>(img.base+i),rd=reinterpret_cast<uintptr_t>(img.rdata.base);if(p>=rd&&p-rd<img.rdata.size){size_t off=(p-rd)&~size_t(7);if(off+8<=img.rdata.size){uintptr_t value=0;memcpy(&value,img.rdata.base+off,8);uintptr_t base=reinterpret_cast<uintptr_t>(img.base);if(value>=base&&value-base<img.size)byte=static_cast<uint8_t>((value-base)>>(((p-rd)%8)*8));}}hash^=byte;hash*=1099511628211ULL;}
    printf(",\"fixture_hash\":\"%016llx\"}",static_cast<unsigned long long>(hash));putchar('\n');
}
}
namespace sco { uint8_t* OriginalSig(const char*); }
#define Sig OriginalSig
#include "src/sco_signatures.cpp"
#undef Sig
namespace sco {
uint8_t* Sig(const char* id) { auto it=parity302::deps.find(id);return it==parity302::deps.end()?OriginalSig(id):it->second; }
void Log(const char*,...) {}
}
'''

def generate(source, build):
    files = sorted(source.glob('src/game/*_sigs.cpp'))
    all_sources = [*files, source/'src/game/sig_rows.h', source/'src/sco_scan.cpp', source/'src/sco_signatures.cpp', source/'src/game/signatures.cpp']
    source_hashes = {str(p.relative_to(source)):hashlib.sha256(p.read_bytes()).hexdigest() for p in all_sources}
    metadata = {}
    units = []
    for idx,p in enumerate(files):
        text = p.read_text()
        match = re.search(r'extern const SigDef (k\w+Signatures)\[\]\s*=\s*\{',text)
        assert match, p
        table = match.group(1)
        entries=[]
        # Resolver templates are compared by actual function pointer, not inferred row counts.
        for kind,spec in dict.fromkeys(re.findall(r'(?:rows::)?Resolve(Fn|Rip)<(\w+)>',text)):
            entries.append((kind,spec))
        body=[COMMON, '#include <cstdlib>', '#include "'+str(p)+'"',f'void unit{idx}() {{', 'using namespace sco; using namespace sco::game;',f'for(const auto& row : {table}) {{',
              'std::vector<uint8_t> mem(0x40000,0xCC); Image img{}; img.base=mem.data();img.size=mem.size();img.text={mem.data()+0x1000,0x20000};img.rdata={mem.data()+0x22000,0x10000};',
              'uint8_t* at=img.text.base+0x100; parity302::deps.clear(); const char* fixture="gap:no-valid-fixture"; bool exercise=false;',
              'if(row.pattern) {parity302::put(at,row.pattern); if(row.ripDisp>0) {int32_t d=static_cast<int32_t>((img.rdata.base+0x100)-(at+row.ripSize)); memcpy(at+row.ripDisp,&d,4);} fixture="pattern";exercise=true;}']
        for kind,spec in entries:
            body.append(f'if(row.resolve==rows::Resolve{kind}<{spec}>) {{ fixture="shared-{kind}:{spec}";exercise=true;')
            if kind=='Fn':
                body += [f'parity302::put(at,{spec}.pattern);',f'for(size_t j=0;j<{spec}.nChecks;++j) parity302::put(at+{spec}.checks[j].off,{spec}.checks[j].bytes);']
            else:
                body += [f'parity302::deps[{spec}.from]=at;', f'if({spec}.size==5 && {spec}.dispOff==1)parity302::put(at+{spec}.off,"E8");else if({spec}.dispOff==3)parity302::put(at+{spec}.off,"48 8D 05");else if({spec}.size==6 && {spec}.dispOff==2)parity302::put(at+{spec}.off,"FF 15");else if({spec}.size==7 && {spec}.dispOff==2){{parity302::put(at+{spec}.off,"80 3D");at[{spec}.off+6]=0;}}', f'int32_t d=static_cast<int32_t>((img.rdata.base+0x100)-(at+{spec}.off+{spec}.size)); memcpy(at+{spec}.off+{spec}.dispOff,&d,4);']
            body.append('}')
        direct_entries=[(k,s) for k,s in dict.fromkeys(re.findall(r'Resolve(Call|StrFn|FnOf|InRange|VtblFn|Vtbl|String)<(\w+)>',text)) if s.startswith('k')]
        direct_entries += [('StrFn',s) for s in re.findall(r'SCO_STR\("[^"]+",\s*(k\w+)\)',text)]
        for kind,spec in direct_entries:
            body.append(f'if(row.resolve==Resolve{kind}<{spec}>) {{ fixture="direct-{kind}:{spec}";exercise=true;')
            if kind=='String':
                body.append(f'parity302::str(img,0x100,{spec});')
            elif kind=='Call':
                body += [f'parity302::deps[{spec}.from]=at;at[{spec}.off]=0xE8;parity302::rel(at+{spec}.off,1,5,img.text.base+0x5000);']
                if p.name=='contracts_sigs.cpp':body.append(f'if({spec}.prologue)parity302::put(img.text.base+0x5000,{spec}.prologue);')
            elif kind=='StrFn':
                body += [f'auto* s=parity302::str(img,0x100,{spec}.str);if({spec}.prologue)parity302::put(at,{spec}.prologue);',f'for(size_t j=0;j<{spec}.nChecks;++j)parity302::put(at+{spec}.checks[j].off,{spec}.checks[j].bytes);', 'parity302::put(at+0x1000,"48 8D 15");parity302::rel(at+0x1000,3,7,s);parity302::pdata(img,at);']
            elif kind=='FnOf':
                body += [f'parity302::put(at,{spec}.prologue);parity302::put(at+0x100,{spec}.pattern);parity302::pdata(img,at);']
            elif kind=='InRange':
                body += [f'parity302::deps[{spec}.from]=at;parity302::put(at,{spec}.pattern);']
            elif kind=='VtblFn':
                body += [f'parity302::deps[{spec}.vtbl]=img.rdata.base;auto target=reinterpret_cast<uintptr_t>(at);memcpy(img.rdata.base+{spec}.slot,&target,8);parity302::put(at,{spec}.prologue);']
            elif kind=='Vtbl':
                body += [f'parity302::deps[{spec}.fn]=at;auto target=reinterpret_cast<uintptr_t>(at);memcpy(img.rdata.base+0x100+{spec}.slot,&target,8);']
            body.append('}')
        if p.name == 'contracts_sigs.cpp':
            body.append('if(!strcmp(row.id,"contracts.mission_settings")){parity302::put(at,kMissionSettings);parity302::rel(at,3,7,img.rdata.base+0x100);fixture="explicit:mission-settings";exercise=true;}')
        body.append('parity302::extra(row.id,img,at);')
        for setup in (custom_setup, offline_setup, world_setup):
            body.append(setup(p.name))
        body += ['SigResult r=SigFail("no valid fixture implemented"); if(exercise) {if(row.resolve) r=row.resolve(img);else {r=SigPattern(img.text,row.pattern);if(r.state==SigState::Ok&&row.ripDisp>0)r.at=RipTarget(r.at,row.ripDisp,row.ripSize);}}',f'parity302::emit("{p.name}",row.id,fixture,img,r);','}}']
        unit=build/f'unit{idx}.cpp';unit.write_text('\n'.join(body));units.append(unit)
        metadata[p.name]={'sha256':hashlib.sha256(p.read_bytes()).hexdigest(),'shared_templates':[{'kind':k,'spec':s} for k,s in entries]}
    # Reuse only the existing positive fixture builders, not registration/capability assertions.
    core=(source/'tests/game_contracts/core_cases.inc').read_text()
    builders=[]
    for name,test in [('g_quit','TestSystemQuit'),('g_pakImg','TestPakRows'),('g_asopImg','TestAsopRows')]:
        start=core.find('static uint8_t '+name+'[')
        if start<0:
            if name=='g_asopImg':
                m=re.search(r'static uint8_t (g_\w+)[^\n]*;\nstruct AsopLayout',core)
                if m: start=m.start()
        if start>=0: builders.append(core[start:core.index('static void '+test,start)])
    driver=[COMMON,DRIVER,*builders]
    for i in range(len(files)):driver.append(f'void unit{i}();')
    driver += ['int main(){',*[f'unit{i}();' for i in range(len(files))], 'parity302::deps.clear(); if(!sco::game::RegisterGameSignatures()) return 2;',
               'for(int fixture=0;fixture<3;++fixture){sco::Image img{};if(fixture==0){QuitImage(img,true,-1);img.size=sizeof(g_quit);}else if(fixture==1)PakImage(img,true);else AsopImage(img); if(!img.pdata.base){img.pdata={img.base+img.size-16,12};memset(img.pdata.base,0,12);} sco::ResolveAll(img);',
               'for(size_t i=0;i<sco::SignatureCount();++i)parity302::emit("registry",sco::SignatureDef(i)->id,fixture==0?"existing:QuitImage":fixture==1?"existing:PakImage":"existing:AsopImage",img,sco::SignatureResult(i));}','}']
    main=build/'driver.cpp';main.write_text('\n'.join(driver))
    exe=build/'parity'
    cmd=[os.environ.get('CXX','c++'),'-std=c++20','-O0','-g','-I'+str(source/'include'),'-I'+str(source/'src'),'-I'+str(source),main,*units,source/'src/sco_scan.cpp',source/'src/game/signatures.cpp','-o',exe]
    (build/'compile-command.json').write_text(json.dumps([str(x) for x in cmd],indent=2))
    run(cmd,stdout=subprocess.PIPE,stderr=(build/'compile.log').open('w'))
    output=run([exe],stdout=subprocess.PIPE).stdout
    (build/'results.jsonl').write_text(output)
    assert source_hashes == {str(p.relative_to(source)):hashlib.sha256(p.read_bytes()).hexdigest() for p in all_sources}, 'source changed during build; rerun'
    (build/'source-hashes.json').write_text(json.dumps(source_hashes,indent=2)+'\n')
    return [json.loads(line) for line in output.splitlines()],metadata

def main():
    parser=argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--out',type=Path,required=True)
    args=parser.parse_args();out=args.out.resolve();out.mkdir(parents=True,exist_ok=False)
    archive=out/'baseline.tar'
    with archive.open('wb') as f:run(['git','-C',ROOT,'archive',BASELINE],stdout=f)
    baseline=out/'baseline-source';baseline.mkdir()
    with tarfile.open(archive) as tf:tf.extractall(baseline,filter='data')
    results={}; audits={}
    for name,source in [('baseline',baseline),('current',ROOT)]:
        build=out/name;build.mkdir();results[name],audits[name]=generate(source,build)
    b,c=results['baseline'],results['current']
    key=lambda r:(r['file'],r['id'],r['fixture'])
    bm={key(r):r for r in b};cm={key(r):r for r in c}
    diffs=[{'key':k,'baseline':bm.get(k),'current':cm.get(k)} for k in sorted(bm.keys()|cm.keys()) if bm.get(k)!=cm.get(k)]
    rows=[r for r in c if r['file']!='registry'];ids={r['id'] for r in rows}
    shared_rip=sorted(r['id'] for r in rows if r['fixture'].startswith('shared-Rip:'))
    successful=sorted({r['id'] for r in c if r['state']=='OK' and bm.get(key(r))==r})
    coverage=[]
    for r in rows:
        positives=[x for x in c if x['id']==r['id'] and x['state']=='OK' and bm.get(key(x))==x]
        coverage.append({'id':r['id'],'source':r['file'],'synthetic_fixture':r['fixture'],'positive_parity':bool(positives),'positive_fixtures':sorted({x['fixture'] for x in positives}),'probe_state':r['state'],'probe_reason':r['reason']})
    report={'baseline':BASELINE,'current_head':run(['git','-C',ROOT,'rev-parse','HEAD'],stdout=subprocess.PIPE).stdout.strip(),'source_audits':audits,'source_file_count':len(audits['current']),'row_count':len(rows),'unique_row_count':len(ids),'shared_rip_consumer_count':len(shared_rip),'shared_rip_consumer_ids':shared_rip,'shared_fn_consumer_ids':sorted(r['id'] for r in rows if r['fixture'].startswith('shared-Fn:')),'observations_per_build':len(c),'equal_observation_count':sum(bm.get(key(r))==r for r in c),'differences':diffs,'successful_resolution_parity_count':len(successful),'successful_resolution_parity_ids':successful,'coverage_gap_ids':sorted(ids-set(successful)),'coverage':coverage,'limitations':['Independent per-row synthetic fixtures stub declared dependency addresses; existing positive core fixtures also run the production registry.','Pattern/check materialization is not proof of real game behavior; failed extra validators are explicit coverage gaps.','Non-OK matching outputs do not count as successful resolution parity.','Synthetic fixtures only; no proprietary executable bytes.']}
    report['fixture_generator_hashes'] = {p.name: hashlib.sha256(p.read_bytes()).hexdigest() for p in [Path(__file__), *sorted(Path(__file__).parent.glob('issue302_fixtures_*.py'))]}
    report['per_source_row_count'] = dict(sorted(collections.Counter(x['source'] for x in coverage).items()))
    report['full_valid_input_parity_proven'] = not diffs and not report['coverage_gap_ids']
    report['shared_rip_positive_parity_count'] = len(set(shared_rip) & set(successful))
    report['shared_fn_positive_parity_count'] = len(set(report['shared_fn_consumer_ids']) & set(successful))
    report['fixture_hash_method'] = 'FNV-1a 64-bit over complete synthetic image; in-image .rdata pointer slots normalized to RVAs'
    (out/'scope-census.json').write_text(json.dumps(report,indent=2)+'\n')
    print(json.dumps({k:report[k] for k in ['row_count','source_file_count','shared_rip_consumer_count','observations_per_build','equal_observation_count','successful_resolution_parity_count']},indent=2))
    print('differences:',len(diffs));print('coverage gaps:',len(report['coverage_gap_ids']));print('report:',out/'scope-census.json')
    return 1 if diffs else 0
if __name__=='__main__':raise SystemExit(main())
