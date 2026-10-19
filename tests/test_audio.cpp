// SPDX-License-Identifier: GPL-3.0-only
#include "../src/game/services/audio.h"
#include "../src/game/services/spawn.h"
#include <scosdk/game/audio.hpp>
#include <sco/host.h>
#include <sco/runtime.h>
#include <sco/caps.h>
#include <cstdio>
#include <cstring>
#include <thread>
#include <memory>
namespace G = sco::game::services;
namespace {
int passed = 0, failed = 0;
#define CHECK(x) do { if (x) ++passed; else { ++failed; std::printf("FAIL %d: %s\n", __LINE__, #x); } } while (0)
struct Fixture {
    uint64_t next = 10;
    unsigned resolves = 0, plays = 0, stops = 0, active = 0;
    bool fail_stop = false;
    bool fail_play = false, revoke_play = false, recurse = false;
    sco::sdk::game::Audio* consumer = nullptr;
    sco_plugin* release_during_play = nullptr;
};
int hook_calls[5]{};
void Hook1(const void*) { ++hook_calls[0]; }
void Hook2(const void*) { ++hook_calls[1]; }
void Hook3(const void*) { ++hook_calls[2]; }
void Hook4(const void*) { ++hook_calls[3]; }
void Hook5(const void*) { ++hook_calls[4]; }
const void* offered = nullptr;
sco_result Query(const char*, uint32_t, const void** out) { *out=offered; return SCO_OK; }
sco_result Resolve(void* ctx, const char* name, uint64_t* out) {
    auto& f = *static_cast<Fixture*>(ctx); ++f.resolves;
    if (std::strcmp(name,"cue") != 0) return SCO_NOT_FOUND;
    *out = 7; return SCO_OK;
}
sco_result Play(void* ctx, uint64_t key, uint64_t* out) {
    auto& f = *static_cast<Fixture*>(ctx); CHECK(key==7);
    if (f.fail_play) return SCO_FAILED;
    ++f.plays; ++f.active; *out=f.next++;
    if (f.recurse) {
        uint64_t nested=9;
        CHECK(f.consumer->Play(1,nested)==SCO_FAILED && nested==0);
        CHECK(G::StopAudio()==sco::Result::Failed);
    }
    if (f.revoke_play) sco::caps::Set("game.audio.triggers",false,"fixture revocation");
    if (f.release_during_play) {
        auto* owner=f.release_during_play; f.release_during_play=nullptr;
        CHECK(sco::Release(owner)==sco::Result::Ok);
    }
    return SCO_OK;
}
sco_result Stop(void* ctx, uint64_t key) {
    auto& f = *static_cast<Fixture*>(ctx); CHECK(key>=10); ++f.stops;
    if (f.fail_stop) return SCO_FAILED;
    CHECK(f.active>0); --f.active; return SCO_OK;
}
}
int main() {
    sco::SetGameThread();
    CHECK(G::AddGameReleaseHook(Hook1)==sco::Result::Ok);
    CHECK(G::AddGameReleaseHook(Hook2)==sco::Result::Ok);
    CHECK(G::AddGameReleaseHook(Hook3)==sco::Result::Ok);
    CHECK(G::AddGameReleaseHook(Hook4)==sco::Result::Ok);
    CHECK(G::AddGameReleaseHook(Hook5)==sco::Result::TooMany);
    auto* dispatch_owner=sco::host::NewPlugin("audio_dispatch");
    CHECK(sco::Release(dispatch_owner)==sco::Result::Ok);
    CHECK(hook_calls[0]==1 && hook_calls[1]==1 && hook_calls[2]==1 && hook_calls[3]==1 && hook_calls[4]==0);
    CHECK(G::RemoveGameReleaseHook(Hook1)==sco::Result::Ok);
    CHECK(G::RemoveGameReleaseHook(Hook2)==sco::Result::Ok);
    CHECK(G::RemoveGameReleaseHook(Hook3)==sco::Result::Ok);
    CHECK(G::RemoveGameReleaseHook(Hook4)==sco::Result::Ok);
    const auto* api=sco::host::BuildApi({"audio fixture"});
    auto* a=sco::host::NewPlugin("audio_a");
    auto* b=sco::host::NewPlugin("audio_b");
    sco::sdk::game::Audio A,B;
    CHECK(A.Open(api,a)==SCO_NOT_FOUND);
    CHECK(G::StartAudio()==sco::Result::Ok);
    CHECK(A.Open(api,a)==SCO_OK);
    uint64_t ta=9,tb=9,pa=9,pb=9;
    CHECK(A.Resolve("cue",ta)==SCO_UNAVAILABLE && ta==0);
    CHECK(G::StopAudio()==sco::Result::Ok);
    Fixture f;
    CHECK(G::StartAudio({&f,Resolve,Play,nullptr})==sco::Result::BadArg);
    CHECK(G::StartAudio({&f,Resolve,Play,Stop})==sco::Result::Ok);
    CHECK(A.Open(api,a)==SCO_OK && B.Open(api,b)==SCO_OK);
    CHECK(A.Resolve("cue",ta)==SCO_OK && ta!=0);
    uint64_t repeat=0; CHECK(A.Resolve("cue",repeat)==SCO_OK && repeat==ta && f.resolves==1);
    CHECK(B.Resolve("cue",tb)==SCO_OK && tb!=ta);
    CHECK(A.Resolve("CUE",repeat)==SCO_NOT_FOUND && repeat==0);
    CHECK(A.Resolve("",repeat)==SCO_BAD_ARG);
    char long_name[129]; std::memset(long_name,'a',128); long_name[128]=0;
    CHECK(A.Resolve(long_name,repeat)==SCO_BAD_ARG);
    CHECK(A.Resolve("bad name",repeat)==SCO_BAD_ARG);
    CHECK(A.Play(tb,pa)==SCO_FAILED && pa==0 && f.plays==0);
    CHECK(A.Play(ta,pa)==SCO_OK && pa!=ta && f.active==1);
    CHECK(B.Play(tb,pb)==SCO_OK && f.active==2);
    CHECK(B.Stop(pa)==SCO_FAILED && f.stops==0);
    CHECK(A.Play(pa,repeat)==SCO_NOT_FOUND);
    CHECK(A.Stop(ta)==SCO_NOT_FOUND);
    sco_result offthread=SCO_OK;
    std::thread thread([&]{ uint64_t id=0; offthread=A.Resolve("cue",id); }); thread.join();
    CHECK(offthread==SCO_WRONG_THREAD);
    CHECK(sco::Release(a)==sco::Result::Ok && f.active==1 && f.stops==1);
    CHECK(A.Stop(pa)==SCO_BAD_ARG);
    CHECK(B.Stop(pb)==SCO_OK && f.active==0);
    CHECK(B.Stop(pb)==SCO_NOT_FOUND);
    f.consumer=&B; f.recurse=true;
    CHECK(B.Play(tb,pb)==SCO_OK && f.active==1);
    f.recurse=false; CHECK(B.Stop(pb)==SCO_OK && f.active==0);
    f.fail_play=true; CHECK(B.Play(tb,pb)==SCO_FAILED && pb==0 && f.active==0); f.fail_play=false;
    f.revoke_play=true;
    CHECK(B.Play(tb,pb)==SCO_UNAVAILABLE && pb==0 && f.active==0);
    f.revoke_play=false; sco::caps::Set("game.audio.triggers",true,"fixture resume");
    CHECK(B.Resolve("cue",tb)==SCO_OK);
    const void* raw=nullptr;
    CHECK(api->query_service(SC_AUDIO_NAME,SC_AUDIO_VERSION_1_0,&raw)==SCO_OK);
    sco_api short_api{}; short_api.size=sizeof(short_api); short_api.query_service=Query;
    sco::sdk::game::Audio short_consumer;
    for (size_t extent : {size_t(8),size_t(16),size_t(24)}) {
        auto bytes=std::make_unique<unsigned char[]>(extent+7);
        std::memcpy(bytes.get(),raw,extent+7);
        const uint32_t size=static_cast<uint32_t>(extent+7);
        std::memcpy(bytes.get(),&size,sizeof(size)); offered=bytes.get();
        CHECK(short_consumer.Open(&short_api,b)==SCO_OK);
        if (extent==8) CHECK(short_consumer.Resolve("cue",repeat)==SCO_UNAVAILABLE && repeat==0);
        if (extent==16) CHECK(short_consumer.Play(tb,repeat)==SCO_UNAVAILABLE && repeat==0);
        if (extent==24) CHECK(short_consumer.Stop(pb)==SCO_UNAVAILABLE);
    }
    sc_audio_v1 null_table{}; null_table.size=sizeof(null_table); offered=&null_table;
    CHECK(short_consumer.Open(&short_api,b)==SCO_OK);
    CHECK(short_consumer.Resolve("cue",repeat)==SCO_UNAVAILABLE);
    CHECK(short_consumer.Play(tb,repeat)==SCO_UNAVAILABLE);
    CHECK(short_consumer.Stop(pb)==SCO_UNAVAILABLE);
    auto* c=sco::host::NewPlugin("audio_c"); sco::sdk::game::Audio C;
    CHECK(C.Open(api,c)==SCO_OK); uint64_t tc=0,pc=0;
    CHECK(C.Resolve("cue",tc)==SCO_OK);
    f.release_during_play=c;
    CHECK(C.Play(tc,pc)==SCO_UNAVAILABLE && pc==0 && f.active==0);
    uint64_t tokens[SC_AUDIO_MAX_PLAYBACKS]{};
    for (auto& token:tokens) CHECK(B.Play(tb,token)==SCO_OK);
    const auto before=f.plays;
    CHECK(B.Play(tb,repeat)==SCO_TOO_MANY && repeat==0 && f.plays==before);
    for (auto token:tokens) CHECK(B.Stop(token)==SCO_OK);
    CHECK(G::StopAudio()==sco::Result::Ok && f.active==0);
    CHECK(B.Resolve("cue",repeat)==SCO_UNAVAILABLE);
    CHECK(G::StartAudio({&f,Resolve,Play,Stop})==sco::Result::Ok);
    CHECK(B.Play(tb,pb)==SCO_NOT_FOUND);
    CHECK(B.Resolve("cue",tb)==SCO_OK && B.Play(tb,pb)==SCO_OK);
    f.fail_stop=true;
    CHECK(B.Stop(pb)==SCO_FAILED && f.active==1);
    CHECK(B.Play(tb,repeat)==SCO_UNAVAILABLE && repeat==0);
    CHECK(G::StopAudio()==sco::Result::Failed && f.active==1);
    f.fail_stop=false;
    CHECK(B.Stop(pb)==SCO_OK && f.active==0);
    CHECK(G::StartAudio({&f,Resolve,Play,Stop})==sco::Result::Failed);
    std::printf("audio synthetic: %d passed, %d failed\n",passed,failed);
    return failed ? 1 : 0;
}
