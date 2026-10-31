
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

// Standalone: link src/sco_scan.cpp; use -fsanitize=address,undefined.
#include "../src/game/actors_sigs.cpp"
#include <cassert>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <vector>

static uint8_t*& source = fixture302::dependency;
namespace sco {

SigResult SigPattern(const Section& text, const char* pattern) {
    uint8_t* at = nullptr;
    const int n = FindPattern(text, pattern, &at, 1);
    return n == 1 ? SigOk(at) : SigResult{n ? SigState::Ambiguous : SigState::Missing, nullptr, n, nullptr};
}
}

static void Put(uint8_t* p, const char* pattern) {
    while (*pattern) {
        if (*pattern == ' ') { ++pattern; continue; }
        if (*pattern == '?') { *p++ = 0; ++pattern; if (*pattern == '?') ++pattern; continue; }
        char hex[]{pattern[0], pattern[1], 0};
        *p++ = static_cast<uint8_t>(std::strtoul(hex, nullptr, 16)); pattern += 2;
    }
}
static void Rip(uint8_t* p, const uint8_t* target, size_t disp = 3, size_t len = 7) {
    const auto delta = reinterpret_cast<intptr_t>(target) - reinterpret_cast<intptr_t>(p + len);
    assert(delta >= INT32_MIN && delta <= INT32_MAX);
    const int32_t d = static_cast<int32_t>(delta); std::memcpy(p + disp, &d, 4);
}
struct Fixture {
    std::vector<uint8_t> bytes = std::vector<uint8_t>(4096);
    sco::Image img{};
    Fixture() {
        img.base = bytes.data(); img.size = static_cast<uint32_t>(bytes.size());
        img.text = {bytes.data(), 2048}; img.rdata = {bytes.data() + 2048, 2048};
    }
    uint8_t* String(const char* s) { std::strcpy(reinterpret_cast<char*>(img.rdata.base), s); return img.rdata.base; }
};
int main(int argc, char** argv) {
    using namespace sco;
    using namespace sco::game;
    const bool finalOnly = argc > 1 && std::strcmp(argv[1], "final") == 0;
    if (finalOnly) {
        Fixture f;
        const auto* msg = f.String("FindEntityByName_SlowDebugCodeOnly: %s");
        auto* lea = f.img.text.base + f.img.text.size - 7;
        auto* fn = lea - 0x36;
        Put(fn, "48 89 5C 24 08 48 89 74 24 10 57 48 83 EC 40 48 C7 02 00 00 00 00");
        Put(lea, "48 8D 0D"); Rip(lea, msg);
        auto r = fixture302::invoke([&](const sco::SigResolveContext& ctx) { return ResolveFindEntityByName(f.img, ctx); });
        assert(r.state == SigState::Ok && r.at == fn);
        std::puts("PASS final-position RIP reference"); return 0;
    }
    // Physically short allocation: not merely a shortened advertised section.
    source = static_cast<uint8_t*>(std::malloc(1)); *source = 0xe8;
    Image tiny{}; tiny.base = source; tiny.size = 1; tiny.text = {source, 1};
    assert(fixture302::invoke([&](const sco::SigResolveContext& ctx) { return ResolveSetGodMode(tiny, ctx); }).state == SigState::Failed);
    assert(fixture302::invoke([&](const sco::SigResolveContext& ctx) { return ResolveCall<kTeamCategory>(tiny, ctx); }).state == SigState::Failed);
    assert(fixture302::invoke([&](const sco::SigResolveContext& ctx) { return ResolveSeatCallback(tiny, ctx); }).state == SigState::Failed);
    assert(fixture302::invoke([&](const sco::SigResolveContext& ctx) { return ResolveSeatInteractable(tiny, ctx); }).state == SigState::Failed);
    assert(fixture302::invoke([&](const sco::SigResolveContext& ctx) { return ResolveTeamTag(tiny, ctx); }).state == SigState::Failed);
    assert(fixture302::invoke([&](const sco::SigResolveContext& ctx) { return CheckLoadoutContainer(tiny, source, ctx); }));
    std::free(source); source = nullptr;
    std::puts("PASS physically truncated dependent sources");
    for (size_t size = 1; size <= 0x250; ++size) {
        source = static_cast<uint8_t*>(std::calloc(size, 1));
        Image clipped{}; clipped.base = source; clipped.size = static_cast<uint32_t>(size); clipped.text = {source, size};
        (void)fixture302::invoke([&](const sco::SigResolveContext& ctx) { return ResolveSetGodMode(clipped, ctx); });
        (void)fixture302::invoke([&](const sco::SigResolveContext& ctx) { return ResolveCall<kTeamCategory>(clipped, ctx); });
        (void)fixture302::invoke([&](const sco::SigResolveContext& ctx) { return ResolveSeatCallback(clipped, ctx); });
        (void)fixture302::invoke([&](const sco::SigResolveContext& ctx) { return ResolveSeatInteractable(clipped, ctx); });
        (void)fixture302::invoke([&](const sco::SigResolveContext& ctx) { return ResolveTeamTag(clipped, ctx); });
        (void)fixture302::invoke([&](const sco::SigResolveContext& ctx) { return CheckLoadoutContainer(clipped, source, ctx); });
        std::free(source);
    }
    source = nullptr;
    std::puts("PASS physical source extent sweep 1..0x250");
    Fixture f; source = f.img.text.base;
    auto* call = source + 0x12;
    Put(call, "E8"); Rip(call, source + 512, 1, 5);
    Put(source + 512, "48 89 5C 24 08 57 48 83 EC 20 88 91");
    assert(fixture302::invoke([&](const sco::SigResolveContext& ctx) { return ResolveSetGodMode(f.img, ctx); }).state == SigState::Ok);
    Image missing = f.img; missing.base = nullptr; missing.size = 0;
    assert(fixture302::invoke([&](const sco::SigResolveContext& ctx) { return ResolveSetGodMode(missing, ctx); }).state != SigState::Ok);
    Image shortImage = f.img; shortImage.size = 512;
    assert(fixture302::invoke([&](const sco::SigResolveContext& ctx) { return ResolveSetGodMode(shortImage, ctx); }).state != SigState::Ok);
    Rip(call, f.img.rdata.base, 1, 5);
    assert(fixture302::invoke([&](const sco::SigResolveContext& ctx) { return ResolveSetGodMode(f.img, ctx); }).state != SigState::Ok);
    Rip(call, f.img.base + f.img.size, 1, 5);
    assert(fixture302::invoke([&](const sco::SigResolveContext& ctx) { return ResolveSetGodMode(f.img, ctx); }).state != SigState::Ok);
    int32_t under = INT32_MIN; std::memcpy(call + 1, &under, 4);
    assert(fixture302::invoke([&](const sco::SigResolveContext& ctx) { return ResolveSetGodMode(f.img, ctx); }).state != SigState::Ok);
    std::puts("PASS call target text/image/missing-image/extreme-displacement proofs");
    // A callback target can be inside .text yet lack its checked tail.
    source = f.img.text.base;
    auto* lea = source + 0x21c;
    for (const auto& c : kSeatChecks) Put(source + c.off, c.bytes);
    Put(lea, "48 8D 05"); Rip(lea, f.img.text.base + f.img.text.size - 1);
    assert(fixture302::invoke([&](const sco::SigResolveContext& ctx) { return ResolveSeatCallback(f.img, ctx); }).state != SigState::Ok);
    std::puts("PASS derived callback truncated tail");
    // Flight Ready's LEA occupies the final seven bytes of the declared text.
    Fixture flight;
    const auto* header = flight.String("C:\\workspace\\CryEngine\\Code\\CryEngine\\CryCommon\\Events/ISC/Dashboards.h");
    auto* last = flight.img.text.base + flight.img.text.size - 7;
    auto* start = last - 48;
    start[-2] = start[-1] = 0xcc;
    Put(start, "48 89 5C 24 48 8B FA 4C 89 44 24 41 B8 49 00 00 00");
    Put(last, "48 8D 15"); Rip(last, header);
    auto ready = fixture302::invoke([&](const sco::SigResolveContext& ctx) { return ResolveFlightReady(flight.img, ctx); });
    assert(ready.state == SigState::Ok && ready.at == start);
    flight.img.base = nullptr; flight.img.size = 0;
    assert(fixture302::invoke([&](const sco::SigResolveContext& ctx) { return ResolveFlightReady(flight.img, ctx); }).state != SigState::Ok);
    std::puts("PASS Flight Ready final LEA and missing image");
    // String target requires the whole NUL-terminated name in declared .rdata.
    Fixture container;
    source = container.img.text.base;
    auto* typeLea = source + 0xbb;
    const auto* type = container.String("IItemPortContainer");
    Put(typeLea, "4C 8D 05"); Rip(typeLea, type);
    assert(!fixture302::invoke([&](const sco::SigResolveContext& ctx) { return CheckLoadoutContainer(container.img, source, ctx); }));
    container.img.rdata.size = std::strlen("IItemPortContainer");
    assert(fixture302::invoke([&](const sco::SigResolveContext& ctx) { return CheckLoadoutContainer(container.img, source, ctx); }));
    container.img.rdata.size = 2048;
    container.img.base = nullptr; container.img.size = 0;
    assert(fixture302::invoke([&](const sco::SigResolveContext& ctx) { return CheckLoadoutContainer(container.img, source, ctx); }));
    std::puts("PASS type name NUL extent and missing image");
    std::puts("actors bounds regressions passed");
}

namespace sco { uint8_t* Sig(const char*) { return nullptr; } }
