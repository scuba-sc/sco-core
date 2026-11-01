
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

#include <cstdio>
#include <cstring>
#include "../src/game/asop_sigs.cpp"

namespace sco {

SigResult SigPattern(const Section&, const char*) { return {}; }
}

int main(int argc, char** argv) {
    const char* mode = argc > 1 ? argv[1] : "jump";
    uint8_t bytes[0xe00]{};
    sco::Image img{}; img.base = bytes; img.size = sizeof(bytes); img.text = {bytes, sizeof(bytes)};
    if (!std::strcmp(mode, "jump")) {
        int32_t displacement = sco::game::asop::kOpenExit - sco::game::asop::kOpenClientHalf;
        std::memcpy(bytes + sco::game::asop::kOpenPatchSite + 1, &displacement, 4);
        if (fixture302::invoke([&](const sco::SigResolveContext& ctx) { return sco::game::OpenExtra(img, bytes, ctx); })) { std::puts("FAIL: valid jump rejected"); return 1; }
        img.text.size = sco::game::asop::kOpenPatchSite + 4;
        if (!fixture302::invoke([&](const sco::SigResolveContext& ctx) { return sco::game::OpenExtra(img, bytes, ctx); })) { std::puts("FAIL: truncated jump accepted"); return 1; }
    } else if (!std::strcmp(mode, "gate")) {
        fixture302::dependency = bytes;
        if (fixture302::invoke([&](const sco::SigResolveContext& ctx) { return sco::game::GateOpenExtra(img, bytes + 0x21, ctx); })) { std::puts("FAIL: valid gate rejected"); return 1; }
        fixture302::dependency = nullptr;
        if (!fixture302::invoke([&](const sco::SigResolveContext& ctx) { return sco::game::GateOpenExtra(img, reinterpret_cast<const uint8_t*>(0x21), ctx); })) { std::puts("FAIL: missing dependency accepted"); return 1; }
    } else if (!std::strcmp(mode, "inventory")) {
        fixture302::dependency = bytes;
        if (fixture302::invoke([&](const sco::SigResolveContext& ctx) { return sco::game::ResolveInventoryKind(img, ctx); }).at != bytes + 0x109) { std::puts("FAIL: valid immediate rejected"); return 1; }
        img.text.size = 0x109;
        if (fixture302::invoke([&](const sco::SigResolveContext& ctx) { return sco::game::ResolveInventoryKind(img, ctx); }).state == sco::SigState::Ok) { std::puts("FAIL: out-of-text immediate accepted"); return 1; }
    } else return 2;
    std::puts("rejected");
}

namespace sco { uint8_t* Sig(const char*) { return nullptr; } }
