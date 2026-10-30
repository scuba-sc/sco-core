
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
#include <memory>

namespace sco::game {
extern const SigDef kMiningSignatures[];
}
namespace sco {


SigResult SigPattern(const Section&, const char*) { return {}; }
}
int main() {
    auto text = std::make_unique<uint8_t[]>(1);
    text[0] = 0x4c;
    char label[] = "CBiomeBuilder::BuildLargeScaleEcoSystem";
    sco::Image img{};
    img.text = {nullptr, 0};
    img.rdata = {reinterpret_cast<uint8_t*>(label), sizeof(label)};
    const auto result = fixture302::invoke([&](const sco::SigResolveContext& ctx) { return sco::game::kMiningSignatures[0].resolve(img, ctx); });
    if (result.state == sco::SigState::Ok) return 1;
    std::puts("short mining text rejected");
}

namespace sco { uint8_t* Sig(const char*) { return nullptr; } }
