#include "../src/game/events_sigs.cpp"
#include <cassert>
#include <cstdlib>
#include <cstring>
namespace sco { uint8_t* Sig(const char*) { return nullptr; } }
namespace sco {
SigResult SigPattern(const Section& text, const char* pattern) {
    int n = 0; auto p = FindUniquePattern(text, pattern, n);
    return n == 1 ? SigOk(p) : SigResult{n ? SigState::Ambiguous : SigState::Missing, nullptr, n, nullptr};
}
}
#include "sco/signature_registry.h"
#include <functional>
namespace {
std::function<const char*(const sco::SigResolveContext&)> fixtureCall;
const char* fixtureResult;
sco::SigResult fixtureTarget(const sco::Image&, const sco::SigResolveContext& ctx) {
    fixtureResult = fixtureCall(ctx); return sco::SigOk(nullptr);
}
const char* withContext(std::function<const char*(const sco::SigResolveContext&)> call) {
    fixtureCall = std::move(call);
    sco::SigDef row{"fixture.target", nullptr, 0, 0, fixtureTarget, {nullptr}};
    sco::SignatureTable table{&row, 1};
    sco::SignatureRegistry registry;
    if (registry.RegisterTables(&table, 1).status != sco::SigRegistryStatus::Ok ||
        registry.Freeze() != sco::SigRegistryStatus::Ok ||
        registry.Resolve(sco::Image{}) != sco::SigRegistryStatus::Ok) std::abort();
    return fixtureResult;
}
}
int main() {
    auto p = static_cast<uint8_t*>(std::malloc(1)); *p = 0xe8;
    sco::Image img{}; img.base = p; img.size = 1; img.text = {p, 1};
    assert(std::strcmp(withContext([&](const sco::SigResolveContext& ctx) { return sco::game::HealthExtra(img, p, ctx); }), "status accessor call outside .text") == 0);
    assert(withContext([&](const sco::SigResolveContext& ctx) { return sco::game::StateExtra(img, p, ctx); }) != nullptr);
    assert(withContext([&](const sco::SigResolveContext& ctx) { return sco::game::SpawnExtra(img, p, ctx); }) != nullptr);
    std::free(p);
    uint8_t bytes[8] = {0, 0x48, 0x8d, 0x05, 0, 0, 0, 0};
    const uint8_t* refs[1]{};
    assert(sco::game::LeaRefs({bytes, sizeof(bytes)}, 0x48, 0x8d, 0x05, bytes + 8, refs, 1) == 1);
    assert(refs[0] == bytes + 1);
    assert(sco::game::LeaRefs({reinterpret_cast<uint8_t*>(UINTPTR_MAX - 3), 8}, 0x48, 0x8d, 0x05, bytes, refs, 1) == 0);
    uint8_t storage[512]{};
    img = {}; img.base = storage; img.size = sizeof(storage); img.text = {storage, sizeof(storage)};
    const uint8_t accessor[] = {0x48,0x8b,0x81,0x08,0x02,0,0,0x48,0x05,0xa0,0x74,0,0,0xc3};
    std::memcpy(storage + 128, accessor, sizeof(accessor));
    auto call = [&](size_t off, size_t target) {
        storage[off] = 0xe8; int32_t d = static_cast<int32_t>(target) - static_cast<int32_t>(off + 5);
        std::memcpy(storage + off + 1, &d, 4);
    };
    call(0,128); call(0x1d,128); call(0xd,200); call(0x2a,200);
    assert(withContext([&](const sco::SigResolveContext& ctx) { return sco::game::HealthExtra(img, storage, ctx); }) == nullptr);
    assert(withContext([&](const sco::SigResolveContext& ctx) { return sco::game::StateExtra(img, storage, ctx); }) == nullptr);
    img.text.size = 128 + sizeof(accessor) - 1;
    assert(std::strcmp(withContext([&](const sco::SigResolveContext& ctx) { return sco::game::HealthExtra(img, storage, ctx); }), "status accessor outside .text") == 0);
    // Keep the accessor before the site so a short final displacement is reached.
    std::memcpy(storage, accessor, sizeof(accessor));
    call(200,0); call(200+0x1d,0); call(200+0xd,64); call(200+0x2a,64);
    img.text.size = 200+0x2e;
    assert(std::strcmp(withContext([&](const sco::SigResolveContext& ctx) { return sco::game::HealthExtra(img, storage+200, ctx); }), "GetStat call outside .text") == 0);
    img.text.size = 200+0x2f;
    assert(withContext([&](const sco::SigResolveContext& ctx) { return sco::game::HealthExtra(img, storage+200, ctx); }) == nullptr);
    img.text = {storage, sizeof(storage)};
    img.rdata = {reinterpret_cast<uint8_t*>(UINTPTR_MAX - 1), 8};
    assert(std::strcmp(withContext([&](const sco::SigResolveContext& ctx) { return sco::game::SpawnExtra(img, storage, ctx); }), "invalid .rdata") == 0);
    assert(std::strcmp(sco::game::LabelFunction(img, "label", 0x48, 0x8d, 0x05, nullptr).why, "invalid .rdata") == 0);
    img.rdata = {storage + 256, 6}; std::memcpy(storage + 256, "label", 6);
    storage[0] = 0x48; storage[1] = 0x8d; storage[2] = 0x05;
    int32_t d = 256 - 7; std::memcpy(storage + 3, &d, 4);
    img.pdata = {storage + 500, 24};
    assert(std::strcmp(sco::game::LabelFunction(img, "label", 0x48, 0x8d, 0x05, nullptr).why, "invalid .pdata") == 0);
}
