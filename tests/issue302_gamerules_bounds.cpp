
// Invoke the real resolver with a context supplied by an owned registry. Dependencies
// intentionally share the fixture address, including forged boundary-test addresses.
#include "sco/signature_registry.h"
#include <cassert>
#include <type_traits>
#include <utility>
namespace fixture302 {
static uint8_t* dependency = nullptr;
template<class F> auto invoke(F&& fn) {
    using T = std::invoke_result_t<F, const sco::SigResolveContext&>;
    struct Capture { F* fn; T value{}; } capture{&fn};
    static Capture* active;
    active = &capture;
    sco::SigDef rows[] = {
        {"asop.on_request_open", nullptr, 0, 0, +[](const sco::Image&, const sco::SigResolveContext&) { return dependency ? sco::SigOk(dependency) : sco::SigFail("fixture missing"); }, {}},
        {"asop.rm_request_deliver", nullptr, 0, 0, +[](const sco::Image&, const sco::SigResolveContext&) { return dependency ? sco::SigOk(dependency) : sco::SigFail("fixture missing"); }, {}},
        {"spawn.god_mode_call", nullptr, 0, 0, +[](const sco::Image&, const sco::SigResolveContext&) { return dependency ? sco::SigOk(dependency) : sco::SigFail("fixture missing"); }, {}},
        {"spawn.landing_helper", nullptr, 0, 0, +[](const sco::Image&, const sco::SigResolveContext&) { return dependency ? sco::SigOk(dependency) : sco::SigFail("fixture missing"); }, {}},
        {"spawn.find_seat", nullptr, 0, 0, +[](const sco::Image&, const sco::SigResolveContext&) { return dependency ? sco::SigOk(dependency) : sco::SigFail("fixture missing"); }, {}},
        {"spawn.seat_callback", nullptr, 0, 0, +[](const sco::Image&, const sco::SigResolveContext&) { return dependency ? sco::SigOk(dependency) : sco::SigFail("fixture missing"); }, {}},
        {"teleport.entity_system", nullptr, 0, 0, +[](const sco::Image&, const sco::SigResolveContext&) { return dependency ? sco::SigOk(dependency) : sco::SigFail("fixture missing"); }, {}},
        {"build.ground_ray", nullptr, 0, 0, +[](const sco::Image&, const sco::SigResolveContext&) { return dependency ? sco::SigOk(dependency) : sco::SigFail("fixture missing"); }, {}},
        {"build.entity_get_rotation", nullptr, 0, 0, +[](const sco::Image&, const sco::SigResolveContext&) { return dependency ? sco::SigOk(dependency) : sco::SigFail("fixture missing"); }, {}},
        {"build.entity_vtable", nullptr, 0, 0, +[](const sco::Image&, const sco::SigResolveContext&) { return dependency ? sco::SigOk(dependency) : sco::SigFail("fixture missing"); }, {}},
        {"teleport.to_camera", nullptr, 0, 0, +[](const sco::Image&, const sco::SigResolveContext&) { return dependency ? sco::SigOk(dependency) : sco::SigFail("fixture missing"); }, {}},
        {"cvars.reference_int_get", nullptr, 0, 0, +[](const sco::Image&, const sco::SigResolveContext&) { return dependency ? sco::SigOk(dependency) : sco::SigFail("fixture missing"); }, {}},
        {"cvars.reference_int_set", nullptr, 0, 0, +[](const sco::Image&, const sco::SigResolveContext&) { return dependency ? sco::SigOk(dependency) : sco::SigFail("fixture missing"); }, {}},
        {"cvars.reference_int_vtable", nullptr, 0, 0, +[](const sco::Image&, const sco::SigResolveContext&) { return dependency ? sco::SigOk(dependency) : sco::SigFail("fixture missing"); }, {}},
        {"test", nullptr, 0, 0, +[](const sco::Image&, const sco::SigResolveContext&) { return dependency ? sco::SigOk(dependency) : sco::SigFail("fixture missing"); }, {}},
        {"contracts.query_reply", nullptr, 0, 0, +[](const sco::Image&, const sco::SigResolveContext&) { return dependency ? sco::SigOk(dependency) : sco::SigFail("fixture missing"); }, {}},
        {"contracts.mission_system_site", nullptr, 0, 0, +[](const sco::Image&, const sco::SigResolveContext&) { return dependency ? sco::SigOk(dependency) : sco::SigFail("fixture missing"); }, {}},
        {"pak.datacore_loader", nullptr, 0, 0, +[](const sco::Image&, const sco::SigResolveContext&) { return dependency ? sco::SigOk(dependency) : sco::SigFail("fixture missing"); }, {}},
        {"fixture.invoke", nullptr, 0, 0, +[](const sco::Image&, const sco::SigResolveContext& ctx) { active->value = (*active->fn)(ctx); static uint8_t marker; return sco::SigOk(&marker); }, {}},
    };
    sco::SignatureRegistry registry;
    const sco::SignatureTable table{rows, sizeof(rows)/sizeof(rows[0])};
    assert(registry.RegisterTables(&table, 1).status == sco::SigRegistryStatus::Ok);
    assert(registry.Freeze() == sco::SigRegistryStatus::Ok);
    assert(registry.Resolve({}) == sco::SigRegistryStatus::Ok);
    return capture.value;
}
}

#include "sco/scan.h"
#include "sco/signatures.h"
#include <cstdio>
#include <cstring>
namespace sco::game { extern const SigDef kGameRulesSignatures[]; }
namespace sco {   SigResult SigPattern(const Section&, const char*) { return {}; } }
int main() {
    uint8_t bytes[256]{};
    sco::Image img{}; img.base=bytes; img.size=sizeof(bytes);
    img.text={bytes+128, 16}; img.rdata={bytes+32, 24}; img.pdata={bytes+64,12};
    std::memcpy(bytes+32,"IGameRulesParamsModule",23);
    uint32_t pdata[] = {128,143,80}; std::memcpy(bytes+64,pdata,sizeof(pdata));
    bytes[80]=0x20; uint32_t parent[] = {143,150,100}; std::memcpy(bytes+84,parent,sizeof(parent));
    bytes[136]=0x48; bytes[137]=0x8d; bytes[138]=0x05;
    int32_t disp = static_cast<int32_t>((bytes+32)-(bytes+143)); std::memcpy(bytes+139,&disp,4);
    const uint8_t prologue[] = {0x40,0x55,0x53,0x57,0x41,0x54,0x41,0x55,0x41,0x56,0x41,0x57,0x48,0x8b,0xec,0x48};
    std::memcpy(bytes+143,prologue,sizeof(prologue));
    auto result=fixture302::invoke([&](const sco::SigResolveContext& ctx) { return sco::game::kGameRulesSignatures[1].resolve(img, ctx); });
    if (result.state==sco::SigState::Ok) { std::puts("FAIL: truncated prologue accepted"); return 1; }
    std::puts("truncated prologue rejected");
}

namespace sco { uint8_t* Sig(const char*) { return nullptr; } }
