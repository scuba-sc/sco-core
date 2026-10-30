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
    unsigned resolves = 0, plays = 0, stops = 0, once = 0, active = 0;
    bool fail_stop = false;
    uint64_t fail_stop_key = 0;
    bool active_keys[512]{};
    bool fail_play = false, fault_once = false, release_once = false, revoke_play = false, recurse = false;
    sco::sdk::game::Audio* consumer = nullptr;
    sco_plugin* release_during_play = nullptr;
    sco_plugin* release_during_stop = nullptr;
};
int hook_calls[6]{};
void Hook1(const void*) { ++hook_calls[0]; }
void Hook2(const void*) { ++hook_calls[1]; }
void Hook3(const void*) { ++hook_calls[2]; }
void Hook4(const void*) { ++hook_calls[3]; }
void Hook5(const void*) { ++hook_calls[4]; }
void Hook6(const void*) { ++hook_calls[5]; }
const void* offered = nullptr;
sco_result Query(const char*, uint32_t, const void** out) { *out=offered; return SCO_OK; }
sco_result Resolve(void* ctx, const char* name, uint64_t* out) {
    auto& f = *static_cast<Fixture*>(ctx); ++f.resolves;
    if (std::strcmp(name,"cue") == 0) *out = 7;
    else if (std::strcmp(name,"fresh-after-once-fault") == 0) *out = 8;
    else return SCO_NOT_FOUND;
    return SCO_OK;
}
sco_result Play(void* ctx, uint64_t key, uint64_t* out) {
    auto& f = *static_cast<Fixture*>(ctx); CHECK(key==7);
    if (f.fail_play) return SCO_FAILED;
    ++f.plays; ++f.active; *out=f.next++;
    CHECK(*out < 512); if (*out < 512) f.active_keys[*out] = true;
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
    if (f.release_during_stop) {
        auto* owner=f.release_during_stop; f.release_during_stop=nullptr;
        CHECK(sco::Release(owner)==sco::Result::Ok);
    }
    if (f.fail_stop || key == f.fail_stop_key) return SCO_FAILED;
    CHECK(f.active>0 && key < 512 && f.active_keys[key]);
    if (key < 512) f.active_keys[key] = false;
    --f.active; return SCO_OK;
}
sco_result PlayOnce(void* ctx, uint64_t key) {
    auto& f = *static_cast<Fixture*>(ctx); CHECK(key == 7 || key == 8); ++f.once;
    if (f.release_once) {
        f.release_once = false;
        CHECK(sco::Release(f.release_during_play) == sco::Result::Ok);
    }
    if (f.fault_once) return SCO_CRASHED;
    return f.fail_play ? SCO_FAILED : SCO_OK;
}
}
int main(int argc, char** argv) {
    sco::SetGameThread();
    CHECK(G::AddGameReleaseHook(Hook1)==sco::Result::Ok);
    CHECK(G::AddGameReleaseHook(Hook2)==sco::Result::Ok);
    CHECK(G::AddGameReleaseHook(Hook3)==sco::Result::Ok);
    CHECK(G::AddGameReleaseHook(Hook4)==sco::Result::Ok);
    CHECK(G::AddGameReleaseHook(Hook5)==sco::Result::Ok);
    CHECK(G::AddGameReleaseHook(Hook6)==sco::Result::TooMany);
    auto* dispatch_owner=sco::host::NewPlugin("audio_dispatch");
    CHECK(sco::Release(dispatch_owner)==sco::Result::Ok);
    CHECK(hook_calls[0]==1 && hook_calls[1]==1 && hook_calls[2]==1 && hook_calls[3]==1 && hook_calls[4]==1 && hook_calls[5]==0);
    CHECK(G::RemoveGameReleaseHook(Hook1)==sco::Result::Ok);
    CHECK(G::RemoveGameReleaseHook(Hook2)==sco::Result::Ok);
    CHECK(G::RemoveGameReleaseHook(Hook3)==sco::Result::Ok);
    CHECK(G::RemoveGameReleaseHook(Hook4)==sco::Result::Ok);
    CHECK(G::RemoveGameReleaseHook(Hook5)==sco::Result::Ok);
    const auto* api=sco::host::BuildApi({"audio fixture"});
    auto* a=sco::host::NewPlugin("audio_a");
    auto* b=sco::host::NewPlugin("audio_b");
    sco::sdk::game::Audio A,B;
    CHECK(A.Open(api,a)==SCO_NOT_FOUND);
    CHECK(G::StartAudio()==sco::Result::Ok);
    CHECK(A.Open(api,a)==SCO_OK);
    uint64_t ta=9,tb=9,pa=9,pb=9,repeat=0;
    CHECK(A.Resolve("cue",ta)==SCO_UNAVAILABLE && ta==0);
    CHECK(G::StopAudio()==sco::Result::Ok);
    Fixture f;
    CHECK(G::StartAudio({&f,Resolve,nullptr,nullptr,PlayOnce})==sco::Result::Ok);
    CHECK(A.Open(api,a)==SCO_OK);
    CHECK(!sco::caps::Has("game.audio.triggers") && !sco::caps::Has("game.audio.play_once"));
    uint64_t once_token=0;
    CHECK(A.Resolve("cue",once_token)==SCO_UNAVAILABLE && once_token==0 && f.resolves==0);
    sco::caps::Set("game.audio.play_once",true,"synthetic-only provider");
    CHECK(A.Resolve("cue",once_token)==SCO_OK && once_token!=0);
    CHECK(A.PlayOnce(once_token)==SCO_OK && f.once==1);
    CHECK(A.PlayOnce(999)==SCO_NOT_FOUND && f.once==1);
    // Owner released during an accepted callout: reported UNAVAILABLE, no rollback.
    f.release_during_play=a; f.release_once=true;
    CHECK(A.PlayOnce(once_token)==SCO_UNAVAILABLE && f.once==2);
    f.release_during_play=nullptr;
    CHECK(A.PlayOnce(once_token)==SCO_BAD_ARG && f.once==2);
    a=sco::host::NewPlugin("audio_a_again"); CHECK(A.Open(api,a)==SCO_OK);
    CHECK(G::StopAudio()==sco::Result::Ok);
    CHECK(G::StartAudio({&f,Resolve,Play,Stop,PlayOnce})==sco::Result::Ok);
    CHECK(A.Open(api,a)==SCO_OK);
    sco::caps::Set("game.audio.triggers",true,"legacy provider");
    sco::caps::Set("game.audio.play_once",true,"synthetic one-shot provider");
    CHECK(A.Resolve("cue",ta)==SCO_OK);
    CHECK(B.Open(api,b)==SCO_OK);
    const auto foreign_once=f.once;
    CHECK(B.PlayOnce(ta)==SCO_FAILED && f.once==foreign_once); // foreign owner: no callout
    CHECK(B.Resolve("cue",tb)==SCO_OK);
    CHECK(B.Play(tb,pb)==SCO_OK && A.Play(ta,pa)==SCO_OK && f.active==2);
    const bool nested_cleanup = argc > 1 && std::strcmp(argv[1], "nested-cleanup") == 0;
    const bool nested_failure = argc > 1 && std::strcmp(argv[1], "nested-cleanup-failure") == 0;
    if (nested_cleanup || nested_failure) {
        // B's earlier slot was already visited when A's stop releases B.
        const uint64_t b_effect=f.next-2;
        if (nested_failure) f.fail_stop_key=b_effect;
        f.release_during_stop=b;
        CHECK(sco::Release(a)==sco::Result::Ok);
        CHECK(f.stops==2); // no immediate repeated attempt after a failed stop
        if (nested_failure) {
            CHECK(f.active==1 && f.active_keys[b_effect] && !sco::caps::Has("game.audio.triggers"));
            CHECK(G::StopAudio()==sco::Result::Failed && f.active==1);
            const auto stops_after_failure=f.stops;
            f.fail_stop_key=0;
            CHECK(G::StopAudio()==sco::Result::Failed && f.active==1 && f.stops==stops_after_failure);
        } else {
            CHECK(f.active==0);
            CHECK(G::StopAudio()==sco::Result::Ok);
            // Failure in the first teardown stop must not call the second effect,
            // nor retry automatically. Live owners can explicitly retry under the
            // backend's contract that a failed stop retains its effect/context.
            CHECK(G::StartAudio({&f,Resolve,Play,Stop,PlayOnce})==sco::Result::Ok);
            auto* c=sco::host::NewPlugin("audio_teardown_c");
            auto* d=sco::host::NewPlugin("audio_teardown_d");
            auto* e=sco::host::NewPlugin("audio_teardown_e");
            sco::sdk::game::Audio C,D,E;
            uint64_t tc=0,td=0,te=0,pc=0,pd=0,pe=0;
            CHECK(C.Open(api,c)==SCO_OK && D.Open(api,d)==SCO_OK && E.Open(api,e)==SCO_OK);
            CHECK(C.Resolve("cue",tc)==SCO_OK && D.Resolve("cue",td)==SCO_OK && E.Resolve("cue",te)==SCO_OK);
            CHECK(C.Play(tc,pc)==SCO_OK && D.Play(td,pd)==SCO_OK && E.Play(te,pe)==SCO_OK && f.active==3);
            f.fail_stop_key=f.next-3;
            const auto before_teardown=f.stops;
            CHECK(G::StopAudio()==sco::Result::Failed && f.stops==before_teardown+1 && f.active==3);
            f.fail_stop_key=0;
            CHECK(G::StopAudio()==sco::Result::Failed && f.stops==before_teardown+1 && f.active==3);
            CHECK(sco::Release(e)==sco::Result::Ok && f.stops==before_teardown+1 && f.active==3);
            CHECK(C.Stop(pc)==SCO_OK && D.Stop(pd)==SCO_OK && f.active==1);
            CHECK(G::StopAudio()==sco::Result::Failed && f.stops==before_teardown+3);
        }
        std::printf("audio nested cleanup: %d passed, %d failed\n",passed,failed);
        return failed ? 1 : 0;
    }
    const auto stops_before_fault=f.stops;
    const bool fault_cleanup_failure = argc > 1 && std::strcmp(argv[1], "fault-cleanup-failure") == 0;
    const uint64_t a_effect=f.next-1;
    if (fault_cleanup_failure) f.fail_stop_key=a_effect;
    f.fault_once=true; f.release_once=true; f.release_during_play=a;
    CHECK(A.PlayOnce(ta)==SCO_CRASHED && f.once==foreign_once+1);
    f.fault_once=false; f.release_during_play=nullptr;
    if (fault_cleanup_failure) {
        CHECK(f.active==2 && f.stops==stops_before_fault+1 && f.active_keys[a_effect]);
        CHECK(!sco::caps::Has("game.audio.play_once") && !sco::caps::Has("game.audio.triggers"));
        CHECK(B.Stop(pb)==SCO_OK && f.active==1 && f.active_keys[a_effect]);
        CHECK(G::StopAudio()==sco::Result::Failed && f.active==1 && f.active_keys[a_effect]);
        const auto stops_after_failure=f.stops;
        f.fail_stop_key=0;
        CHECK(G::StopAudio()==sco::Result::Failed && f.active==1 && f.active_keys[a_effect] && f.stops==stops_after_failure);
        CHECK(G::StartAudio({&f,Resolve,Play,Stop,PlayOnce})==sco::Result::Failed);
        std::printf("audio fault cleanup failure: %d passed, %d failed\n",passed,failed);
        return failed ? 1 : 0;
    }
    // A release deferred during a faulting callout must drain before return,
    // without stopping another owner's independently tracked effect.
    CHECK(f.active==1 && f.stops==stops_before_fault+1);
    CHECK(B.Stop(pb)==SCO_OK && f.active==0);
    CHECK(!sco::caps::Has("game.audio.play_once") && sco::caps::Has("game.audio.triggers"));
    a=sco::host::NewPlugin("audio_a_after_fault"); CHECK(A.Open(api,a)==SCO_OK);
    CHECK(A.Resolve("cue",ta)==SCO_OK);
    const auto resolves_before_fresh=f.resolves;
    CHECK(A.Resolve("fresh-after-once-fault",repeat)==SCO_OK && repeat!=0 && repeat!=ta);
    CHECK(f.resolves==resolves_before_fresh+1); // uncached name reaches the resolver after the fault
    CHECK(A.PlayOnce(ta)==SCO_UNAVAILABLE && f.once==foreign_once+1);
    CHECK(A.Play(ta,pa)==SCO_OK && f.active==1);
    CHECK(A.Stop(pa)==SCO_OK && f.active==0);
    CHECK(G::StopAudio()==sco::Result::Ok);
    CHECK(G::StartAudio({&f,Resolve,Play,Stop,PlayOnce})==sco::Result::Ok);
    CHECK(A.Open(api,a)==SCO_OK);
    sco::caps::Set("game.audio.play_once",true,"synthetic re-enable attempt");
    CHECK(A.Resolve("cue",ta)==SCO_OK); // legacy mode is still healthy
    const auto once_after_restart=f.once;
    CHECK(A.PlayOnce(ta)==SCO_UNAVAILABLE && f.once==once_after_restart); // fault survives Stop/Start: restart required
    CHECK(G::StopAudio()==sco::Result::Ok);
    CHECK(G::StartAudio({&f,Resolve,nullptr,nullptr,nullptr})==sco::Result::BadArg);
    CHECK(G::StartAudio({&f,nullptr,nullptr,nullptr,PlayOnce})==sco::Result::BadArg);
    CHECK(G::StartAudio({&f,Resolve,Play,nullptr})==sco::Result::BadArg);
    f = Fixture{}; // legacy v1.0 section below uses absolute counters
    CHECK(G::StartAudio({&f,Resolve,Play,Stop})==sco::Result::Ok);
    CHECK(A.Open(api,a)==SCO_OK && B.Open(api,b)==SCO_OK);
    const auto resolves_before_legacy=f.resolves;
    CHECK(A.Resolve("cue",ta)==SCO_OK && ta!=0);
    CHECK(A.Resolve("cue",repeat)==SCO_OK && repeat==ta && f.resolves==resolves_before_legacy+1);
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
    for (size_t extent : {size_t(8),size_t(16),size_t(24),size_t(32)}) {
        auto bytes=std::make_unique<unsigned char[]>(extent+7);
        std::memcpy(bytes.get(),raw,extent+7);
        const uint32_t size=static_cast<uint32_t>(extent+7);
        std::memcpy(bytes.get(),&size,sizeof(size)); offered=bytes.get();
        CHECK(short_consumer.Open(&short_api,b)==SCO_OK);
        if (extent==8) CHECK(short_consumer.Resolve("cue",repeat)==SCO_UNAVAILABLE && repeat==0);
        if (extent==16) CHECK(short_consumer.Play(tb,repeat)==SCO_UNAVAILABLE && repeat==0);
        if (extent==24) CHECK(short_consumer.Stop(pb)==SCO_UNAVAILABLE);
        if (extent==32) CHECK(short_consumer.PlayOnce(ta)==SCO_UNAVAILABLE);
    }
    sc_audio_v1 null_table{}; null_table.size=sizeof(null_table); offered=&null_table;
    CHECK(short_consumer.Open(&short_api,b)==SCO_OK);
    CHECK(short_consumer.Resolve("cue",repeat)==SCO_UNAVAILABLE);
    CHECK(short_consumer.Play(tb,repeat)==SCO_UNAVAILABLE);
    CHECK(short_consumer.Stop(pb)==SCO_UNAVAILABLE);
    CHECK(short_consumer.PlayOnce(ta)==SCO_UNAVAILABLE);
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
    if (argc > 1) {
        // Owner release must retain only the failed effect; another owner can
        // still stop its own playback while new operations are latched off.
        auto* d=sco::host::NewPlugin("audio_cleanup_failure"); sco::sdk::game::Audio D;
        uint64_t td=0,pd=0;
        CHECK(D.Open(api,d)==SCO_OK && D.Resolve("cue",td)==SCO_OK);
        CHECK(D.Play(td,pd)==SCO_OK && f.active==2);
        const uint64_t b_effect=f.next-2, d_effect=f.next-1;
        CHECK(b_effect < 512 && d_effect < 512 && f.active_keys[b_effect] && f.active_keys[d_effect]);
        f.fail_stop_key=d_effect;
        CHECK(sco::Release(d)==sco::Result::Ok);
        CHECK(f.active==2 && f.active_keys[b_effect] && f.active_keys[d_effect]);
        CHECK(B.Play(tb,repeat)==SCO_UNAVAILABLE && repeat==0);
        CHECK(B.Stop(pb)==SCO_OK && f.active==1 && !f.active_keys[b_effect] && f.active_keys[d_effect]);
        CHECK(G::StopAudio()==sco::Result::Failed && f.active==1 && f.active_keys[d_effect]);
        const auto stops_after_failure=f.stops;
        f.fail_stop_key=0;
        CHECK(G::StopAudio()==sco::Result::Failed && f.active==1 && f.active_keys[d_effect] && f.stops==stops_after_failure);
    } else {
        f.fail_stop=true;
        CHECK(B.Stop(pb)==SCO_FAILED && f.active==1);
        CHECK(B.Play(tb,repeat)==SCO_UNAVAILABLE && repeat==0);
        CHECK(G::StopAudio()==sco::Result::Failed && f.active==1);
        f.fail_stop=false;
        CHECK(B.Stop(pb)==SCO_OK && f.active==0);
        const auto stops_after_retry=f.stops;
        CHECK(G::StopAudio()==sco::Result::Failed && f.stops==stops_after_retry);
    }
    CHECK(G::StartAudio({&f,Resolve,Play,Stop})==sco::Result::Failed);
    std::printf("audio synthetic: %d passed, %d failed\n",passed,failed);
    return failed ? 1 : 0;
}
