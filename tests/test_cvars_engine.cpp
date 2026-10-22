// SPDX-License-Identifier: GPL-3.0-only
// Real native-access adapter over synthetic vtables, never a game session.
// CVar interface slot positions and the write-blocking flag: F-4.10.197.2878-052 (with
// F-4.10.196.36804-007). Not run in game.
#include "../src/game/services/cvars_engine.h"
#include <sc_cvars_write.h>
#include "sco/host.h"
#include <array>
#include <cstdio>
#include <cstring>
#include <windows.h>

namespace C=sco::game::services;
namespace sco::game::services { bool SpawnReleaseHooked(){return true;} }
namespace {
int passed=0,failed=0;
#define CHECK(x) do{if(x)++passed;else{++failed;std::printf("FAIL %d: %s\n",__LINE__,#x);}}while(0)
bool admitted=true,ignore_write=false,replace_on_write=false;
bool revoke_on_write=false,methods_on_write=false,console_on_write=false;
unsigned changes=0,lookups=0,get_calls=0;
const char* names[]{"i_target_selector.targeting2_enabled","pl_lensdisplay.minimap_enabled",
    "pl_lensdisplay.greebles_enabled","g_squadron_frontend"};
struct Variable { uintptr_t* vt; const char* name; int32_t value; uint32_t flags; };
struct Console { uintptr_t* vt; };
std::array<uintptr_t,32> object_vt{},console_vt{};
Variable variables[4]{},replacement{};
bool use_replacement=false;
uintptr_t console_pointer=0;
bool Admitted(){return admitted;}
uintptr_t __fastcall Lookup(uintptr_t,const char* name){
    ++lookups;
    for(unsigned i=0;i<4;++i)if(!std::strcmp(name,names[i]))return reinterpret_cast<uintptr_t>(use_replacement&&i==0?&replacement:&variables[i]);
    return 0;
}
int32_t __fastcall Get(uintptr_t p){++get_calls;return reinterpret_cast<Variable*>(p)->value;}
void __fastcall Set(uintptr_t p,int32_t value){
    if(!ignore_write)reinterpret_cast<Variable*>(p)->value=value;
    ++changes;
    if(replace_on_write)use_replacement=true;
    if(revoke_on_write)admitted=false;
    if(methods_on_write)object_vt[0x10/8]=0;
    if(console_on_write)console_pointer=0;
}
uint32_t __fastcall Flags(uintptr_t p){return reinterpret_cast<Variable*>(p)->flags;}
const char* __fastcall Name(uintptr_t p){return reinterpret_cast<Variable*>(p)->name;}
C::CvarsEngineRows Rows(){return {reinterpret_cast<uintptr_t>(&console_pointer),reinterpret_cast<uintptr_t>(&Lookup),
    reinterpret_cast<uintptr_t>(object_vt.data()),reinterpret_cast<uintptr_t>(&Get),reinterpret_cast<uintptr_t>(&Set),
    reinterpret_cast<uintptr_t>(&Flags),reinterpret_cast<uintptr_t>(&Name)};}
}
int main(int argc,char** argv){
    sco::SetGameThread();const auto* api=sco::host::BuildApi({"native CVar boundary fixture"});
    object_vt[0x10/8]=reinterpret_cast<uintptr_t>(&Get);object_vt[0x30/8]=reinterpret_cast<uintptr_t>(&Set);
    object_vt[0x58/8]=reinterpret_cast<uintptr_t>(&Flags);object_vt[0x70/8]=reinterpret_cast<uintptr_t>(&Name);
    console_vt[0xc0/8]=reinterpret_cast<uintptr_t>(&Lookup);Console console{console_vt.data()};console_pointer=reinterpret_cast<uintptr_t>(&console);
    for(unsigned i=0;i<4;++i)variables[i]={object_vt.data(),names[i],static_cast<int32_t>(i%2),0};
    replacement={object_vt.data(),names[0],0,0};
    C::NativeCvars native(Rows(),Admitted);auto port=native.Port(true);uint32_t value=9;
    for(unsigned i=0;i<4;++i){CHECK(port.read_bool(port.ctx,i,&value)==SCO_OK&&value==i%2);}
    admitted=false;const auto count=lookups;value=9;
    CHECK(port.read_bool(port.ctx,0,&value)==SCO_UNAVAILABLE&&value==9&&lookups==count);admitted=true;
    variables[0].value=2;CHECK(port.read_bool(port.ctx,0,&value)==SCO_FAILED&&value==9);variables[0].value=0;
    variables[0].flags=0x40000000;CHECK(port.write_bool(port.ctx,0,1)==SCO_UNAVAILABLE&&changes==0);variables[0].flags=0;
    object_vt[0x10/8]=0;CHECK(port.read_bool(port.ctx,0,&value)==SCO_FAILED);object_vt[0x10/8]=reinterpret_cast<uintptr_t>(&Get);
    variables[0].name="wrong";CHECK(port.read_bool(port.ctx,0,&value)==SCO_FAILED);variables[0].name=names[0];
    use_replacement=true;CHECK(port.read_bool(port.ctx,0,&value)==SCO_FAILED);use_replacement=false;
    CHECK(port.read_bool(port.ctx,4,&value)==SCO_BAD_ARG);
    if(argc>1){
        if(!std::strcmp(argv[1],"fault")){
            CHECK(native.Install(Rows(),true)==sco::Result::Ok);
            CHECK(C::StartCvars()==sco::Result::Ok);
            const void* raw=nullptr;CHECK(api->query_service(SC_CVARS_NAME,SC_CVARS_VERSION,&raw)==SCO_OK);
            const auto* table=static_cast<const sc_cvars_v1*>(raw);auto* owner=sco::host::NewPlugin("native_cvar_fault");
            console_pointer=0x10000;
            CHECK(table->get_bool(owner,names[0],SC_CVAR_BOOL,&value)==SCO_CRASHED);
            CHECK(value==9);
            CHECK(native.Install({},false)==sco::Result::Failed);
            console_pointer=reinterpret_cast<uintptr_t>(&console);
            CHECK(port.read_bool(port.ctx,0,&value)==SCO_OK&&value==0);
            CHECK(table->get_bool(owner,names[0],SC_CVAR_BOOL,&value)==SCO_UNAVAILABLE);
            CHECK(C::StopCvars()==sco::Result::Failed);
        }
        else if(!std::strcmp(argv[1],"replace")){replace_on_write=true;CHECK(port.write_bool(port.ctx,0,1)==SCO_FAILED&&replacement.value==0);}
        else if(!std::strcmp(argv[1],"mismatch")){ignore_write=true;CHECK(port.write_bool(port.ctx,0,1)==SCO_FAILED);}
        else if(!std::strcmp(argv[1],"callback-admission")){
            revoke_on_write=true;const auto before=get_calls;const auto queried=lookups;
            CHECK(port.write_bool(port.ctx,0,1)==SCO_UNAVAILABLE);
            CHECK(get_calls==before+1&&lookups==queried+1&&changes==1);
        }
        else if(!std::strcmp(argv[1],"callback-method")){
            methods_on_write=true;const auto before=get_calls;
            CHECK(port.write_bool(port.ctx,0,1)==SCO_FAILED);
            CHECK(get_calls==before+1&&changes==1);
        }
        else if(!std::strcmp(argv[1],"callback-console")){
            console_on_write=true;const auto before=get_calls;const auto queried=lookups;
            CHECK(port.write_bool(port.ctx,0,1)==SCO_FAILED);
            CHECK(get_calls==before+1&&lookups==queried+1&&changes==1);
        }
        else if(!std::strcmp(argv[1],"lease-replace")||!std::strcmp(argv[1],"lease-admission")){
            CHECK(native.Install(Rows(),true)==sco::Result::Ok);
            CHECK(C::StartCvars()==sco::Result::Ok);
            CHECK(sco::AddReleaseHook(C::ReleaseCvarsOwner)==sco::Result::Ok);
            const void* raw=nullptr;CHECK(api->query_service(SC_CVARS_WRITE_NAME,SC_CVARS_WRITE_VERSION,&raw)==SCO_OK);
            const auto* table=static_cast<const sc_cvars_write_v1*>(raw);auto* owner=sco::host::NewPlugin("native_cvar_retained");
            CHECK(table->set_bool(owner,names[0],SC_CVAR_BOOL,1)==SCO_OK&&changes==1);
            if(!std::strcmp(argv[1],"lease-replace"))use_replacement=true;
            else admitted=false;
            CHECK(sco::Release(owner)==sco::Result::Ok);
            CHECK(changes==1&&replacement.value==0&&C::PendingCvarRestores()==1);
            CHECK(C::StopCvars()==sco::Result::Failed);
            CHECK(native.Install({},false)==sco::Result::Failed);
            CHECK(changes==1&&C::PendingCvarRestores()==1);
        }
        else CHECK(false);
    }else{
        CHECK(native.Install(Rows(),true)==sco::Result::Ok);CHECK(C::StartCvars()==sco::Result::Ok);
        CHECK(native.Install({},false)==sco::Result::BadArg);
        CHECK(port.read_bool(port.ctx,0,&value)==SCO_OK&&value==0);
        CHECK(sco::AddReleaseHook(C::ReleaseCvarsOwner)==sco::Result::Ok);
        const void* raw=nullptr;CHECK(api->query_service(SC_CVARS_WRITE_NAME,SC_CVARS_WRITE_VERSION,&raw)==SCO_OK);
        const auto* table=static_cast<const sc_cvars_write_v1*>(raw);auto* owner=sco::host::NewPlugin("native_cvar_owner");
        CHECK(table->set_bool(owner,names[0],SC_CVAR_BOOL,1)==SCO_OK&&changes==1&&variables[0].value==1);
        CHECK(table->set_bool(owner,names[0],SC_CVAR_BOOL,0)==SCO_OK&&changes==2);
        CHECK(table->set_bool(owner,names[0],SC_CVAR_BOOL,1)==SCO_OK&&changes==3);
        CHECK(C::PendingCvarRestores()==1);CHECK(sco::Release(owner)==sco::Result::Ok);
        CHECK(variables[0].value==0&&changes==4&&C::PendingCvarRestores()==0);
        CHECK(C::StopCvars()==sco::Result::Ok);
    }
    std::printf("native cvars: %d passed, %d failed\n",passed,failed);return failed?1:0;
}
