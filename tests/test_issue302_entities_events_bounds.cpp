#include "../src/game/entities_sigs.cpp"
#include <cassert>
#include <cstdlib>
#include <cstring>
static uint8_t* source;
namespace sco { uint8_t* Sig(const char*) { return source; } }
namespace sco {
SigResult SigPattern(const Section& text, const char* pattern) {
    int n = 0; auto p = FindUniquePattern(text, pattern, n);
    return n == 1 ? SigOk(p) : SigResult{n ? SigState::Ambiguous : SigState::Missing, nullptr, n, nullptr};
}
}
// Invoke the real resolver with a registry-created context and synthetic dependencies.
#include "sco/signature_registry.h"
#include <functional>
#include <cstdlib>
namespace {
std::function<sco::SigResult(const sco::SigResolveContext&)> fixtureCall;
sco::SigResult fixtureResult;
sco::SigResult fixtureDependency(const sco::Image&, const sco::SigResolveContext&) { return sco::SigOk(source); }
sco::SigResult fixtureTarget(const sco::Image&, const sco::SigResolveContext& ctx) { fixtureResult = fixtureCall(ctx); return sco::SigOk(nullptr); }
sco::SigResult withContext(std::function<sco::SigResult(const sco::SigResolveContext&)> call) {
    fixtureCall = std::move(call);
    sco::SigDef rows[] = { {"actor.get_stat", nullptr, 0, 0, fixtureDependency, {nullptr}}, {"actor.adjust_stat", nullptr, 0, 0, fixtureDependency, {nullptr}}, {"inventory.unequip_item", nullptr, 0, 0, fixtureDependency, {nullptr}}, {"datacore_live.owner_vtable", nullptr, 0, 0, fixtureDependency, {nullptr}}, {"datacore_live.deleting", nullptr, 0, 0, fixtureDependency, {nullptr}}, {"entities.dump", nullptr, 0, 0, fixtureDependency, {nullptr}}, {"teleport.to_camera", nullptr, 0, 0, fixtureDependency, {nullptr}}, {"teleport.entity_system", nullptr, 0, 0, fixtureDependency, {nullptr}}, {"contracts.reputation_services", nullptr, 0, 0, fixtureDependency, {nullptr}},  {"fixture.target", nullptr, 0, 0, fixtureTarget, {nullptr}} };
    sco::SignatureTable table{rows, sizeof(rows)/sizeof(rows[0])};
    sco::SignatureRegistry registry;
    if (registry.RegisterTables(&table, 1).status != sco::SigRegistryStatus::Ok ||
        registry.Freeze() != sco::SigRegistryStatus::Ok ||
        registry.Resolve(sco::Image{}) != sco::SigRegistryStatus::Ok) std::abort();
    return fixtureResult;
}
}

int main() {
    source = static_cast<uint8_t*>(std::malloc(1));
    *source = 0xe8;
    sco::Image img{}; img.base = source; img.size = 1; img.text = {source, 1};
    assert(withContext([&](const sco::SigResolveContext& ctx) { return sco::game::ResolveClassSite(img, ctx); }).state == sco::SigState::Failed);
    assert(withContext([&](const sco::SigResolveContext& ctx) { return sco::game::ResolveCall<sco::game::kForEach>(img, ctx); }).state == sco::SigState::Failed);
    sco::game::Check check{100, "E8", "short"}; const char* why = nullptr;
    assert(!sco::game::ChecksMatch(img, source, &check, 1, &why));
    assert(std::strcmp(why, "function check outside .text") == 0);
    check = {SIZE_MAX, "E8", "changed"};
    assert(!sco::game::ChecksMatch(img, source, &check, 1, &why));
    check = {0, "90", "changed"};
    assert(!sco::game::ChecksMatch(img, source, &check, 1, &why));
    assert(std::strcmp(why, "changed") == 0);
    std::free(source);
    uint8_t bytes[8] = {0, 0x48, 0x8d, 0x05, 0, 0, 0, 0};
    const uint8_t* refs[1]{};
    assert(sco::game::RipRefs({bytes, sizeof(bytes)}, 0x48, 0x8d, 0x05, bytes + 8, refs, 1) == 1);
    assert(refs[0] == bytes + 1);
    assert(sco::game::RipRefs({reinterpret_cast<uint8_t*>(UINTPTR_MAX - 3), 8}, 0x48, 0x8d, 0x05, bytes, refs, 1) == 0);
    uint8_t storage[512]{};
    img = {}; img.base = storage; img.size = sizeof(storage); img.text = {storage, 128};
    source = storage;
    storage[0x46] = 0xe8;
    int32_t delta = 100 - (0x46 + 5); std::memcpy(storage + 0x47, &delta, 4);
    auto result = withContext([&](const sco::SigResolveContext& ctx) { return sco::game::ResolveCall<sco::game::kForEach>(img, ctx); });
    assert(result.state == sco::SigState::Ok && result.at == storage + 100);
    img.text.size = 0x4a;
    result = withContext([&](const sco::SigResolveContext& ctx) { return sco::game::ResolveCall<sco::game::kForEach>(img, ctx); });
    assert(result.state == sco::SigState::Failed && std::strcmp(result.why, "call instruction outside .text") == 0);
    img.text.size = 128; delta = 140 - (0x46 + 5); std::memcpy(storage + 0x47, &delta, 4);
    result = withContext([&](const sco::SigResolveContext& ctx) { return sco::game::ResolveCall<sco::game::kForEach>(img, ctx); });
    assert(result.state == sco::SigState::Failed && std::strcmp(result.why, "call target outside .text") == 0);
    img.rdata = {reinterpret_cast<uint8_t*>(UINTPTR_MAX - 1), 8};
    result = sco::game::LabelFunction(img, "label", 0x48, 0x8d, 0x05, 0);
    assert(result.state == sco::SigState::Failed && std::strcmp(result.why, "invalid .rdata") == 0);
    source = nullptr;
    assert(std::strcmp(withContext([&](const sco::SigResolveContext& ctx) { return sco::game::ResolveClassSite(img, ctx); }).why, "source row missing") == 0);
}
