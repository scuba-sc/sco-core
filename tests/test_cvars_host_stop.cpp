// SPDX-License-Identifier: GPL-3.0-only
// Actual host Stop and native CVar adapter over synthetic engine objects.
// CVar interface slot positions and the write-blocking flag: F-4.10.197.2878-052 (with
// F-4.10.196.36804-007). Not run in game.
#include "../src/game/services/cvars_engine.h"
#include "../src/game/services/spawn.h"
#include "../src/game/admission.h"
#include "sco/host.h"
#include <sc_cvars_write.h>
#include <array>
#include <cstdio>
#include <cstring>
#include <thread>

namespace gh=sco::game::host;
namespace cs=sco::game::services;
namespace {
int checks=0,failures=0;
#define CHECK(x) do{++checks;if(!(x)){++failures;std::printf("FAIL %d: %s\n",__LINE__,#x);}}while(0)
gh::Admission evidence{gh::Session::Offline,gh::Check::Clear,gh::Check::Clear,gh::Check::Clear};
gh::Admission Inspect(){return evidence;}
gh::ConsumerRetirement RetireConsumer(void*) { return gh::ConsumerRetirement::Quiesced; }
bool Fresh(){return gh::FreshAdmissionAllowed();}
bool Cleanup(){return Fresh()||gh::detail::CvarCleanupAllowed();}
const char* name="i_target_selector.targeting2_enabled";
struct Variable{uintptr_t* vt;const char* name;int32_t value;};
struct Console{uintptr_t* vt;};
std::array<uintptr_t,32> variable_vt{},console_vt{};
Variable variable{};Console console{};uintptr_t console_pointer=0;
unsigned writes=0;
const sc_cvars_write_v1* write_table=nullptr;
sco_plugin* owner=nullptr;
bool in_restore=false;
uintptr_t Lookup(uintptr_t,const char* requested){return !std::strcmp(requested,name)?reinterpret_cast<uintptr_t>(&variable):0;}
int32_t Get(uintptr_t){return variable.value;}
uint32_t Flags(uintptr_t){return 0;}
const char* Name(uintptr_t){return name;}
void Set(uintptr_t,int32_t value){
    ++writes;
    if(value==0){
        in_restore=true;
        CHECK(!Fresh());CHECK(gh::detail::CvarCleanupAllowed());
        CHECK(write_table->set_bool(owner,name,SC_CVAR_BOOL,1)==SCO_UNAVAILABLE);
        bool worker_admitted=true;
        std::thread worker([&]{worker_admitted=gh::detail::CvarCleanupAllowed();});worker.join();
        CHECK(!worker_admitted);
    }
    variable.value=value;
}
void ExitConsumer(const char*,const void*,void*){
    CHECK(!Fresh());
    CHECK(write_table->set_bool(owner,name,SC_CVAR_BOOL,1)==SCO_UNAVAILABLE);
}
}
int main(int argc,char** argv){
    const bool lost=argc>1&&!std::strcmp(argv[1],"loss");
    const bool refusal=argc>1&&!std::strcmp(argv[1],"refusal");
    uint8_t image_bytes[8192]{};
    sco::Image image{image_bytes,{image_bytes,4096},{image_bytes+4096,4096},{},0,sizeof(image_bytes)};
    gh::Options options;options.inspect=Inspect;options.image=&image;options.quit=gh::QuitPolicy::External;options.retireConsumer=RetireConsumer;
    auto ownerToken = std::make_shared<int>(1); options.imageOwner = ownerToken; options.resolverOwner = ownerToken;
    const auto prepared = gh::Prepare(options);
    CHECK(prepared==sco::Result::Ok);
    if (prepared!=sco::Result::Ok) return 1;
    const auto activated = gh::Activate();
    if (activated==sco::Result::Unavailable) {
        // The original restoration case requires the old unowned host. The
        // current host refuses it before native/service work; report the old
        // scenario as skipped rather than dereferencing unavailable tables.
        CHECK(gh::Status().phase==gh::Phase::Prepared);
        CHECK(gh::Start()==sco::Result::Unavailable && !Fresh());
        if (refusal) return failures ? 1 : 0;
        std::printf("SKIP cvars host restoration: unowned host activation is refused\n");
        return failures ? 1 : 77;
    }
    CHECK(!refusal && activated==sco::Result::Ok);
    if (refusal || activated!=sco::Result::Ok) return 1;
    const auto started=gh::Start();
    CHECK(started==sco::Result::Ok);
    if (started!=sco::Result::Ok) return 1;
    CHECK(cs::StartSpawn()==sco::Result::Ok);
    CHECK(cs::SpawnReleaseHooked());
    variable_vt[0x10/8]=reinterpret_cast<uintptr_t>(&Get);variable_vt[0x30/8]=reinterpret_cast<uintptr_t>(&Set);
    variable_vt[0x58/8]=reinterpret_cast<uintptr_t>(&Flags);variable_vt[0x70/8]=reinterpret_cast<uintptr_t>(&Name);
    console_vt[0xc0/8]=reinterpret_cast<uintptr_t>(&Lookup);
    variable={variable_vt.data(),name,0};console={console_vt.data()};console_pointer=reinterpret_cast<uintptr_t>(&console);
    cs::CvarsEngineRows rows{reinterpret_cast<uintptr_t>(&console_pointer),reinterpret_cast<uintptr_t>(&Lookup),
        reinterpret_cast<uintptr_t>(variable_vt.data()),reinterpret_cast<uintptr_t>(&Get),reinterpret_cast<uintptr_t>(&Set),
        reinterpret_cast<uintptr_t>(&Flags),reinterpret_cast<uintptr_t>(&Name)};
    cs::NativeCvars native(rows,Fresh,Cleanup);
    CHECK(native.Install(rows,true)==sco::Result::Ok);CHECK(cs::StartCvars()==sco::Result::Ok);
    const auto* api=sco::host::BuildApi({"CVar host-stop fixture"});const void* raw=nullptr;
    const auto query=api ? api->query_service(SC_CVARS_WRITE_NAME,SC_CVARS_WRITE_VERSION,&raw) : SCO_UNAVAILABLE;
    CHECK(query==SCO_OK && raw);
    if (query!=SCO_OK || !raw) return 1;
    write_table=static_cast<const sc_cvars_write_v1*>(raw);owner=sco::host::NewPlugin("cvar_stop_owner");
    CHECK(owner);
    if (!owner) return 1;
    CHECK(write_table->set_bool(owner,name,SC_CVAR_BOOL,1)==SCO_OK&&writes==1&&variable.value==1);
    CHECK(cs::PendingCvarRestores()==1);
    CHECK(sco::Subscribe(owner,"game.exit",ExitConsumer,nullptr)==sco::Result::Ok);
    if(lost)evidence.session=gh::Session::Unknown;
    CHECK(gh::Stop()==sco::Result::Ok);
    CHECK(!Fresh()&&!gh::detail::CvarCleanupAllowed());
    if(lost){
        CHECK(!in_restore&&writes==1&&variable.value==1&&cs::PendingCvarRestores()==1);
        CHECK(cs::StopCvars()==sco::Result::Failed);
    }else{
        CHECK(in_restore&&writes==2&&variable.value==0&&cs::PendingCvarRestores()==0);
        CHECK(cs::StopCvars()==sco::Result::Ok);
    }
    cs::StopSpawn();
    std::printf("cvars host stop: %d checks, %d failures\n",checks,failures);return failures?1:0;
}
