// SPDX-License-Identifier: GPL-3.0-only
// Production CVar configuration over synthetic objects. CVar interface slot positions and the
// write-blocking flag: F-4.10.197.2878-052 (with F-4.10.196.36804-007). Not run in game.
#include "../src/game/services/cvars_engine.h"
#include "../src/game/services/spawn.h"
#include "../src/game/admission.h"
#include "sco/signatures.h"
#include "sco/host.h"
#include <sc_cvars_write.h>
#include <array>
#include <cstdio>
#include <cstring>
#include <windows.h>

namespace gh = sco::game::host;
namespace cs = sco::game::services;
namespace sco::game::services { bool SpawnReleaseHooked() { return true; } }
namespace {
int checks = 0, failures = 0;
#define CHECK(x) do { ++checks; if (!(x)) { ++failures; std::printf("FAIL %d: %s\n", __LINE__, #x); } } while (0)
gh::Admission Inspect() { return {gh::Session::Offline, gh::Check::Clear, gh::Check::Clear, gh::Check::Clear}; }
const char* names[]{"i_target_selector.targeting2_enabled", "pl_lensdisplay.minimap_enabled",
    "pl_lensdisplay.greebles_enabled", "g_squadron_frontend"};
struct Variable { uintptr_t* vtable; const char* name; int32_t value; uint32_t flags; };
struct Console { uintptr_t* vtable; };
std::array<uintptr_t, 32> variable_vtable{}, console_vtable{};
Variable variables[4]{};
Console console{};
uintptr_t console_pointer = 0;
unsigned setters = 0;
uintptr_t Lookup(uintptr_t, const char* name) {
    for (unsigned i = 0; i < 4; ++i) if (!std::strcmp(name, names[i])) return reinterpret_cast<uintptr_t>(&variables[i]);
    return 0;
}
int32_t Get(uintptr_t p) { return reinterpret_cast<Variable*>(p)->value; }
void Set(uintptr_t p, int32_t value) { ++setters; reinterpret_cast<Variable*>(p)->value = value; }
uint32_t Flags(uintptr_t p) { return reinterpret_cast<Variable*>(p)->flags; }
const char* Name(uintptr_t p) { return reinterpret_cast<Variable*>(p)->name; }
sco::SigResult ResolveConsole(const sco::Image&) { return sco::SigOk(&console_pointer); }
sco::SigResult ResolveLookup(const sco::Image&) { return sco::SigOk(reinterpret_cast<void*>(&Lookup)); }
sco::SigResult ResolveVtable(const sco::Image&) { return sco::SigOk(variable_vtable.data()); }
sco::SigResult ResolveGet(const sco::Image&) { return sco::SigOk(reinterpret_cast<void*>(&Get)); }
sco::SigResult ResolveSet(const sco::Image&) { return sco::SigOk(reinterpret_cast<void*>(&Set)); }
sco::SigResult ResolveFlags(const sco::Image&) { return sco::SigOk(reinterpret_cast<void*>(&Flags)); }
sco::SigResult ResolveName(const sco::Image&) { return sco::SigOk(reinterpret_cast<void*>(&Name)); }
}
int main() {
    char previous[32768]{};
    const DWORD previous_size = GetEnvironmentVariableA("SCO_CVARS_NATIVE", previous, sizeof(previous));
    const bool had_previous = previous_size != 0 && previous_size < sizeof(previous);
    SetEnvironmentVariableA("SCO_CVARS_NATIVE", "1");
    sco::SetGameThread();
    variable_vtable[0x10 / 8] = reinterpret_cast<uintptr_t>(&Get);
    variable_vtable[0x30 / 8] = reinterpret_cast<uintptr_t>(&Set);
    variable_vtable[0x58 / 8] = reinterpret_cast<uintptr_t>(&Flags);
    variable_vtable[0x70 / 8] = reinterpret_cast<uintptr_t>(&Name);
    console_vtable[0xc0 / 8] = reinterpret_cast<uintptr_t>(&Lookup);
    console = {console_vtable.data()};
    console_pointer = reinterpret_cast<uintptr_t>(&console);
    for (unsigned i = 0; i < 4; ++i) variables[i] = {variable_vtable.data(), names[i], static_cast<int32_t>(i % 2), 0};
    const sco::SigDef rows[] = {
        {"cvars.console", nullptr, 0, 0, ResolveConsole, {}},
        {"cvars.lookup", nullptr, 0, 0, ResolveLookup, {}},
        {"cvars.reference_int_vtable", nullptr, 0, 0, ResolveVtable, {}},
        {"cvars.reference_int_get", nullptr, 0, 0, ResolveGet, {}},
        {"cvars.reference_int_set", nullptr, 0, 0, ResolveSet, {}},
        {"cvars.reference_int_flags", nullptr, 0, 0, ResolveFlags, {}},
        {"cvars.reference_int_name", nullptr, 0, 0, ResolveName, {}}
    };
    CHECK(sco::RegisterSignatures(rows, std::size(rows)));
    sco::Image image{};
    sco::ResolveAll(image);
    gh::detail::SetFreshInspector(Inspect);
    gh::detail::SetFreshAdmissionEnabled(true);
    CHECK(gh::FreshAdmissionAllowed());
    const auto* api = sco::host::BuildApi({"CVar production refusal fixture"});
    auto* owner = sco::host::NewPlugin("cvar_production_boundary");
    CHECK(owner != nullptr);
    CHECK(cs::ConfigureProductionCvars() == sco::Result::Ok);
    CHECK(cs::StartCvars() == sco::Result::Ok);
    const void* raw = nullptr;
    CHECK(api->query_service(SC_CVARS_NAME, SC_CVARS_VERSION, &raw) == SCO_OK);
    const auto* read = static_cast<const sc_cvars_v1*>(raw);
    uint32_t value = 99;
    CHECK(read->get_bool(owner, names[0], SC_CVAR_BOOL, &value) == SCO_OK && value == 0);
    CHECK(api->query_service(SC_CVARS_WRITE_NAME, SC_CVARS_WRITE_VERSION, &raw) == SCO_OK);
    const auto* write = static_cast<const sc_cvars_write_v1*>(raw);
    CHECK(write->set_bool(owner, names[0], SC_CVAR_BOOL, 1) == SCO_UNAVAILABLE);
    CHECK(setters == 0 && variables[0].value == 0);
    CHECK(sco::Release(owner) == sco::Result::Ok);
    CHECK(cs::StopCvars() == sco::Result::Ok);
    gh::detail::SetFreshAdmissionEnabled(false);
    gh::detail::SetFreshInspector(nullptr);
    if (had_previous) SetEnvironmentVariableA("SCO_CVARS_NATIVE", previous);
    else SetEnvironmentVariableA("SCO_CVARS_NATIVE", nullptr);
    std::printf("cvars production: %d checks, %d failures\n", checks, failures);
    return failures ? 1 : 0;
}
