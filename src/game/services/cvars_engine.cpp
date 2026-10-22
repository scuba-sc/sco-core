// SPDX-License-Identifier: GPL-3.0-only
// Native reference-integer CVar adapter. The console lookup slot, the CVar interface slots (get,
// set, flags, name) and the write-blocking flag bit are F-4.10.197.2878-052 (lookup and set also
// F-4.10.196.36804-007); g_squadron_frontend is F-4.10.197.2878-046. Production does not install
// the write path. Not run in game.
#include "cvars_engine.h"
#include <cstring>
#include <windows.h>

namespace sco::game::services {
namespace {
constexpr const char* names[]{"i_target_selector.targeting2_enabled", "pl_lensdisplay.minimap_enabled",
    "pl_lensdisplay.greebles_enabled", "g_squadron_frontend"};
bool Pointer(uintptr_t p) { return p >= 0x10000 && p < 0x0000800000000000ull && !(p & 7); }
template<class T> T ReadAt(uintptr_t p) { return *reinterpret_cast<const T*>(p); }
using Lookup = uintptr_t(__fastcall*)(uintptr_t, const char*);
using GetInt = int32_t(__fastcall*)(uintptr_t);
using SetInt = void(__fastcall*)(uintptr_t, int32_t);
using Flags = uint32_t(__fastcall*)(uintptr_t);
using Name = const char*(__fastcall*)(uintptr_t);
}
CvarsPort NativeCvars::Port(bool writes) {
    CvarsPort p{}; p.ctx=this;
    p.verified=rows_.console && rows_.lookup && rows_.reference_vtable && rows_.get_int &&
        rows_.set_int && rows_.flags && rows_.name && admitted_;
    p.offline_admitted=p.verified; // every native operation also checks fresh admission
    p.read_bool=Read; p.write_verified=p.verified && writes; p.write_bool=writes ? Write : nullptr;
    p.restore_bool=writes ? Restore : nullptr;
    return p;
}
Result NativeCvars::Install(CvarsEngineRows rows,bool writes) {
    NativeCvars candidate(rows,admitted_,cleanupAdmitted_);
    auto port=candidate.Port(writes);
    port.ctx=this;
    const auto result=SetCvarsPort(port);
    if(result==Result::Ok) *this=candidate;
    return result;
}
sco_result NativeCvars::Read(void* ctx,uint32_t index,uint32_t* out) {
    return static_cast<NativeCvars*>(ctx)->Access(index,out,false);
}
sco_result NativeCvars::Write(void* ctx,uint32_t index,uint32_t value) {
    return static_cast<NativeCvars*>(ctx)->Access(index,&value,true);
}
sco_result NativeCvars::Restore(void* ctx,uint32_t index,uint32_t value) {
    return static_cast<NativeCvars*>(ctx)->Access(index,&value,true,true);
}
sco_result NativeCvars::Access(uint32_t index,uint32_t* value,bool write,bool restore) {
    if(index>=4 || !value || (write && *value>1)) return SCO_BAD_ARG;
    __try {
        const auto admitted=restore && cleanupAdmitted_ ? cleanupAdmitted_ : admitted_;
        if(!admitted || !admitted()) return SCO_UNAVAILABLE;
        if(!rows_.console || !rows_.lookup || !rows_.reference_vtable || !rows_.get_int ||
           !rows_.set_int || !rows_.flags || !rows_.name) return SCO_UNAVAILABLE;
        const uintptr_t console=ReadAt<uintptr_t>(rows_.console);
        if(!Pointer(console)) return SCO_UNAVAILABLE;
        const uintptr_t console_vt=ReadAt<uintptr_t>(console);
        if(!Pointer(console_vt) || ReadAt<uintptr_t>(console_vt+0xc0)!=rows_.lookup) return SCO_FAILED;
        const uintptr_t object=reinterpret_cast<Lookup>(rows_.lookup)(console,names[index]);
        if(!object) return SCO_NOT_FOUND;
        if(!Pointer(object) || ReadAt<uintptr_t>(object)!=rows_.reference_vtable) return SCO_FAILED;
        if(identities_[index] && identities_[index]!=object) return SCO_FAILED;
        const uintptr_t vt=rows_.reference_vtable;
        if(ReadAt<uintptr_t>(vt+0x10)!=rows_.get_int || ReadAt<uintptr_t>(vt+0x30)!=rows_.set_int ||
           ReadAt<uintptr_t>(vt+0x58)!=rows_.flags || ReadAt<uintptr_t>(vt+0x70)!=rows_.name) return SCO_FAILED;
        const char* actual=reinterpret_cast<Name>(rows_.name)(object);
        if(!actual || std::memcmp(actual,names[index],std::strlen(names[index])+1)) return SCO_FAILED;
        identities_[index]=object;
        const int32_t before=reinterpret_cast<GetInt>(rows_.get_int)(object);
        if(before<0 || before>1) return SCO_FAILED;
        if(write) {
            if(reinterpret_cast<Flags>(rows_.flags)(object)&0x40000000u) return SCO_UNAVAILABLE;
            // Recheck immediately before mutation; the setter owns engine callbacks.
            if(!admitted()) return SCO_UNAVAILABLE;
            reinterpret_cast<SetInt>(rows_.set_int)(object,static_cast<int32_t>(*value));
            // Setter callbacks may close admission or replace console/method state.
            // Refuse before making a subsequent native call through that state.
            if(!admitted()) return SCO_UNAVAILABLE;
            if(ReadAt<uintptr_t>(rows_.console)!=console || ReadAt<uintptr_t>(console)!=console_vt ||
               ReadAt<uintptr_t>(console_vt+0xc0)!=rows_.lookup) return SCO_FAILED;
            const uintptr_t current=reinterpret_cast<Lookup>(rows_.lookup)(console,names[index]);
            if(current!=object || ReadAt<uintptr_t>(object)!=vt ||
               ReadAt<uintptr_t>(vt+0x10)!=rows_.get_int || ReadAt<uintptr_t>(vt+0x30)!=rows_.set_int ||
               ReadAt<uintptr_t>(vt+0x58)!=rows_.flags || ReadAt<uintptr_t>(vt+0x70)!=rows_.name) return SCO_FAILED;
            actual=reinterpret_cast<Name>(rows_.name)(object);
            if(!actual || std::memcmp(actual,names[index],std::strlen(names[index])+1)) return SCO_FAILED;
            const int32_t observed=reinterpret_cast<GetInt>(rows_.get_int)(object);
            return observed==static_cast<int32_t>(*value) ? SCO_OK : SCO_FAILED;
        }
        *value=static_cast<uint32_t>(before);
        return SCO_OK;
    } __except(EXCEPTION_EXECUTE_HANDLER) { return SCO_CRASHED; }
}
}
