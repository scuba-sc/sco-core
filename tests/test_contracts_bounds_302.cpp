
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

// Standalone resolver regression: link only src/sco_scan.cpp, never the shared build.
#include "../src/game/contracts_sigs.cpp"
#include <cassert>
#include <cstdio>
#include <memory>
namespace sco {
uint8_t*& test_source = fixture302::dependency;
SigResult SigPattern(const Section& text, const char* pattern) {
    int n = 0;
    auto* p = FindUniquePattern(text, pattern, n);
    return n == 1 ? SigOk(p) : SigResult{n ? SigState::Ambiguous : SigState::Missing, nullptr, n, nullptr};
}
}
namespace {
int cases = 0;
void Pass(const char* name) { ++cases; std::printf("PASS %02d %s\n", cases, name); }
void Rip(uint8_t* p, const uint8_t* target, size_t disp = 3, size_t len = 7) {
    const int32_t d = static_cast<int32_t>(target - (p + len));
    std::memcpy(p + disp, &d, sizeof d);
}
constexpr sco::game::VtblFnSpec slotSpec{"test", 0, "C3"};
constexpr sco::game::VtblFnSpec hugeSlotSpec{"test", SIZE_MAX, "C3"};
constexpr sco::game::CallSpec callSpec{"test", SIZE_MAX, "C3"};
constexpr sco::game::InRangeSpec rangeSpec{"test", 8, "E8 ?? ?? ?? ??", nullptr};
}
int main() {
    using namespace sco;
    using namespace sco::game;
    auto bytes = std::make_unique<uint8_t[]>(512);
    auto* b = bytes.get();
    Image img{};
    img.base = b; img.size = 512;
    img.text = {b + 64, 128}; img.rdata = {b + 256, 64};
    auto* p = b + 64;
    auto* slot = b + 256;
    p[0] = 0xC3;
    uint64_t v = reinterpret_cast<uintptr_t>(p);
    std::memcpy(slot, &v, 8);
    assert(SlotTarget(img, slot) == p);
    Pass("loaded slot positive");
    assert(SlotTarget(img, p) == nullptr);
    test_source = p;
    assert(fixture302::invoke([&](const sco::SigResolveContext& ctx) { return ResolveVtblFn<slotSpec>(img, ctx); }).state != SigState::Ok);
    Pass("slot inside image but outside rdata refused");
    test_source = slot;
    assert(fixture302::invoke([&](const sco::SigResolveContext& ctx) { return ResolveVtblFn<slotSpec>(img, ctx); }).at == p);
    assert(fixture302::invoke([&](const sco::SigResolveContext& ctx) { return ResolveVtblFn<hugeSlotSpec>(img, ctx); }).state != SigState::Ok);
    Pass("vtable positive and overflowing slot offset");
    img.rdata.size = 7;
    assert(SlotTarget(img, slot) == nullptr);
    Pass("declared rdata partial pointer inside full image refused");
    img.rdata.size = 64;
    auto shortslot = std::make_unique<uint8_t[]>(7);
    Image shortimg{}; shortimg.base = shortslot.get(); shortimg.size = 7;
    shortimg.rdata = {shortslot.get(), 7};
    assert(SlotTarget(shortimg, shortslot.get()) == nullptr);
    assert(HeaderBase(shortimg) == 0);
    Pass("physical seven-byte allocation pointer/header refused");
    auto shorttext = std::make_unique<uint8_t[]>(6);
    shortimg.base = shorttext.get(); shortimg.size = 6;
    shortimg.text = {shorttext.get(), 6};
    shorttext[0] = 0x48; shorttext[1] = 0x8D; shorttext[2] = 0x05;
    assert(LeaTo(shortimg, shorttext.get(), nullptr) == nullptr);
    assert(CheckedRip(shortimg, shorttext.get(), 3, 7) == nullptr);
    assert(fixture302::invoke([&](const sco::SigResolveContext& ctx) { return ShardStoreExtra(shortimg, shorttext.get(), ctx); }) != nullptr);
    Pass("physical truncated LEA, displacement and extra call refused");
    auto exact = std::make_unique<uint8_t[]>(7);
    Image exactimg{}; exactimg.base = exact.get(); exactimg.size = 7;
    exactimg.text = {exact.get(), 7};
    exact[0] = 0x4C; exact[1] = 0x8D; exact[2] = 0x05;
    Rip(exact.get(), exact.get());
    assert(LeaTo(exactimg, exact.get(), nullptr) == exact.get());
    assert(ExactLeaTo(exactimg, exact.get()) == exact.get());
    Pass("exact seven-byte section final complete LEA accepted");
    p[0] = 0x48; p[1] = 0x8D; p[2] = 0x05;
    Rip(p, slot);
    assert(LeaTo(img, slot, nullptr) == p);
    assert(CheckedRip(img, p, 3, 7) == slot);
    Pass("LEA/RIP image target positive");
    Rip(p, b + 512);
    assert(CheckedRip(img, p, 3, 7) == nullptr);
    assert(LeaTo(img, b + 512, nullptr) == nullptr);
    Pass("one-past-image derived target refused");
    int32_t extreme = INT32_MIN;
    std::memcpy(p + 3, &extreme, 4);
    assert(CheckedRip(img, p, 3, 7) == nullptr);
    extreme = INT32_MAX; std::memcpy(p + 3, &extreme, 4);
    assert(CheckedRip(img, p, 3, 7) == nullptr);
    assert(CheckedRip(img, p, SIZE_MAX, 7) == nullptr);
    Pass("extreme rel32 and invalid displacement refused");
    Image noimage = img; noimage.base = nullptr; noimage.size = 0;
    const uint8_t* at = nullptr;
    assert(SlotTarget(noimage, slot) == nullptr);
    assert(!Holds(noimage, v, p));
    assert(SlotsHolding(noimage, p, 0, &at) == 0);
    assert(LeaTo(noimage, slot, nullptr) == nullptr);
    test_source = p;
    assert(fixture302::invoke([&](const sco::SigResolveContext& ctx) { return ResolveMissionSystemSite(noimage, ctx); }).state != SigState::Ok);
    assert(fixture302::invoke([&](const sco::SigResolveContext& ctx) { return ResolveMissionSystem(noimage, ctx); }).state != SigState::Ok);
    assert(fixture302::invoke([&](const sco::SigResolveContext& ctx) { return ResolveAddPlayer(noimage, ctx); }).state != SigState::Ok);
    assert(fixture302::invoke([&](const sco::SigResolveContext& ctx) { return ResolveStrFn<kQueryReply>(noimage, ctx); }).state != SigState::Ok);
    assert(fixture302::invoke([&](const sco::SigResolveContext& ctx) { return ResolveFnOf<kFindEntry>(noimage, ctx); }).state != SigState::Ok);
    assert(fixture302::invoke([&](const sco::SigResolveContext& ctx) { return ResolveInRange<rangeSpec>(noimage, ctx); }).state != SigState::Ok);
    assert(fixture302::invoke([&](const sco::SigResolveContext& ctx) { return ResolveMissionSettings(noimage, ctx); }).state != SigState::Ok);
    Pass("missing image fails closed across custom resolver families");
    Image wrap{}; wrap.base = reinterpret_cast<uint8_t*>(UINTPTR_MAX - 8); wrap.size = 64;
    wrap.text = {wrap.base, 64}; wrap.rdata = wrap.text;
    assert(!Mapped(wrap, wrap.text)); assert(HeaderBase(wrap) == 0);
    assert(LeaTo(wrap, wrap.base, nullptr) == nullptr);
    Pass("overflowing image/section declaration refused before read");
    test_source = reinterpret_cast<uint8_t*>(UINTPTR_MAX - 3);
    assert(fixture302::invoke([&](const sco::SigResolveContext& ctx) { return ResolveCall<callSpec>(img, ctx); }).state != SigState::Ok);
    assert(fixture302::invoke([&](const sco::SigResolveContext& ctx) { return ResolveMissionSystem(img, ctx); }).state != SigState::Ok);
    assert(fixture302::invoke([&](const sco::SigResolveContext& ctx) { return ShardStoreExtra(img, test_source, ctx); }) != nullptr);
    assert(TextOffset(img, p, SIZE_MAX, 1) == nullptr);
    Pass("forged source and overflowing source offsets refused");
    p[0] = 0xE8; Rip(p, p + 40, 1, 5); p[40] = 0xC3;
    assert(CallAt(img, p) == p + 40);
    img.text.size = 4;
    assert(CallAt(img, p) == nullptr);
    Pass("call full extent required with positive control");
    img.text.size = 128;
    Rip(p, b + 256, 1, 5);
    assert(CallAt(img, p) == nullptr);
    Pass("call derived image target outside text refused");
    test_source = p + 120;
    assert(fixture302::invoke([&](const sco::SigResolveContext& ctx) { return ResolveInRange<rangeSpec>(img, ctx); }).state != SigState::Ok);
    Pass("range match requires full pattern suffix extent");
    // PE32+ preferred-address slot translation.
    b[0x3C] = 0xC0; std::memcpy(b + 0xC0, "PE\0\0", 4);
    const uint16_t magic = 0x20B; std::memcpy(b + 0xD8, &magic, 2);
    uint64_t preferred = 0x140000000ULL; std::memcpy(b + 0xF0, &preferred, 8);
    v = preferred + 64; std::memcpy(slot, &v, 8);
    assert(HeaderBase(img) == preferred);
    assert(SlotTarget(img, slot) == p);
    assert(Holds(img, v, p));
    assert(SlotsHolding(img, p, 0, &at) == 1 && at == slot);
    Pass("preferred PE32+ slot translation and slot census positive");
    img.size = 0xF7;
    assert(HeaderBase(img) == 0);
    assert(SlotTarget(img, slot) == nullptr);
    Pass("PE preferred-base tail truncated and slot outside image refused");
    std::printf("contracts bounds: %d cases passed\n", cases);
}

namespace sco { uint8_t* Sig(const char*) { return nullptr; } }
