// SPDX-License-Identifier: GPL-3.0-only
#include "cvars_engine.h"
#include "../admission.h"
#include "sco/game/host.h"
#include "sco/signatures.h"
#include <windows.h>

namespace sco::game::services {
namespace {
bool Fresh() { return sco::game::host::FreshAdmissionAllowed(); }
bool Cleanup() { return Fresh() || sco::game::host::detail::CvarCleanupAllowed(); }
NativeCvars native({},Fresh,Cleanup);
bool Enabled(const char* name) {
    char value[8]{};
    return GetEnvironmentVariableA(name,value,sizeof(value))==1 && value[0]=='1';
}
}
Result ConfigureProductionCvars() {
    if(!OnGameThread()) return Result::WrongThread;
    if(!Enabled("SCO_CVARS_NATIVE")) return SetCvarsPort({});
    const char* rows[]{"cvars.console","cvars.lookup","cvars.reference_int_vtable",
        "cvars.reference_int_get","cvars.reference_int_set","cvars.reference_int_flags","cvars.reference_int_name"};
    for(const auto* row:rows) if(!SigReady(row)) return SetCvarsPort({});
    CvarsEngineRows resolved{reinterpret_cast<uintptr_t>(Sig(rows[0])),reinterpret_cast<uintptr_t>(Sig(rows[1])),
        reinterpret_cast<uintptr_t>(Sig(rows[2])),reinterpret_cast<uintptr_t>(Sig(rows[3])),
        reinterpret_cast<uintptr_t>(Sig(rows[4])),reinterpret_cast<uintptr_t>(Sig(rows[5])),
        reinterpret_cast<uintptr_t>(Sig(rows[6]))};
    // Commit rows only after the stopped/fault-free service installation gate.
    // Native registration lifetime across setter callbacks and retained leases is
    // not established. An environment switch cannot supply that evidence.
    return native.Install(resolved,false);
}
}
