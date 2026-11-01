#include "sco/scan.h"
#include "sco/signatures.h"
#include <array>
#include <cstdio>
#include <cstring>

namespace sco::game {
extern const SigDef kActorStatsSignatures[];
extern const size_t kActorStatsSignatureCount;
extern const SigDef kInventorySignatures[];
extern const size_t kInventorySignatureCount;
}
namespace sco {
static uint8_t* dependency;
uint8_t* Sig(const char*) { return dependency; }
SigResult SigPattern(const Section& text, const char* pattern) {
    int n = 0; auto* p = FindUniquePattern(text, pattern, n);
    return p ? SigOk(p) : SigResult{n ? SigState::Ambiguous : SigState::Missing, nullptr, n, nullptr};
}
}
// Invoke the real resolver with a registry-created context and synthetic dependencies.
#include "sco/signature_registry.h"
#include <functional>
#include <cstdlib>
namespace {
std::function<sco::SigResult(const sco::SigResolveContext&)> fixtureCall;
sco::SigResult fixtureResult;
sco::SigResult fixtureDependency(const sco::Image&, const sco::SigResolveContext&) { return sco::SigOk(sco::dependency); }
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

static sco::SigResolver row(const sco::SigDef* rows, size_t n, const char* id) {
    for (size_t i = 0; i < n; ++i) if (!std::strcmp(rows[i].id, id)) return rows[i].resolve;
    return nullptr;
}
static void put_rel(uint8_t* insn, uint8_t* target) {
    int32_t d = static_cast<int32_t>(target - (insn + 7)); std::memcpy(insn + 3, &d, 4);
}
int main() {
    std::array<uint8_t, 2048> b{};
    sco::Image img{}; img.base = b.data(); img.size = static_cast<uint32_t>(b.size());
    auto actor = row(sco::game::kActorStatsSignatures, sco::game::kActorStatsSignatureCount, "actor.stat_array");
    auto inventory = row(sco::game::kInventorySignatures, sco::game::kInventorySignatureCount, "inventory.container_type_id");
    if (!actor || !inventory) return 2;
    const uint8_t body[] = {0x48,0x63,0xc2,0x48,0x8b,0x94,0xc1,0,0,0,0,0x48,0x85,0xd2,0x74,0,0xc5,0xfa,0x10,0x02,0xc3};
    std::memcpy(b.data()+100, body, sizeof(body));
    img.text = {b.data()+100, sizeof(body)}; img.base = b.data()+500; img.size = 1000;
    sco::dependency = b.data()+100;
    if (withContext([&](const sco::SigResolveContext& ctx) { return actor(img, ctx); }).state == sco::SigState::Ok) { std::puts("actor accepted text outside image"); return 1; }
    img.base = b.data(); img.size = static_cast<uint32_t>(b.size());
    auto* code = b.data()+0x400;
    const uint8_t type[] = {0xb9,0xff,0xff,0,0,0x48,0x8b,0x03,0x48,0x8b,0xb8,0x90,3,0,0,0x0f,0xb7,0x05,0,0,0,0,0x66,0x3b,0xc1,0x75,0,0x48,0x8b,0x0d,0,0,0,0,0x4c,0x8d,0x05,0,0,0,0};
    std::memcpy(code,type,sizeof(type)); std::memcpy(b.data()+0x700,"IItemPortContainer",19);
    put_rel(code+0x22,b.data()+0x700); put_rel(code+0x0f,b.data()+0x740);
    img.text = {code, sizeof(type)}; sco::dependency = code-0x2e0;
    if (withContext([&](const sco::SigResolveContext& ctx) { return inventory(img, ctx); }).state == sco::SigState::Ok) { std::puts("inventory accepted dependency outside text"); return 1; }
    img.text = {b.data()+100, 0x500};
    sco::dependency = b.data()+100;
    if (withContext([&](const sco::SigResolveContext& ctx) { return actor(img, ctx); }).state != sco::SigState::Ok) { std::puts("actor rejected valid body"); return 1; }
    sco::dependency = code-0x2e0;
    if (withContext([&](const sco::SigResolveContext& ctx) { return inventory(img, ctx); }).state != sco::SigState::Ok) { std::puts("inventory rejected valid registration"); return 1; }
    img.size = 0;
    if (withContext([&](const sco::SigResolveContext& ctx) { return inventory(img, ctx); }).state == sco::SigState::Ok) { std::puts("inventory accepted missing image"); return 1; }
    sco::dependency = b.data()+100;
    if (withContext([&](const sco::SigResolveContext& ctx) { return actor(img, ctx); }).state == sco::SigState::Ok) { std::puts("actor accepted missing image"); return 1; }
    std::puts("issue302 actor/inventory bounds passed");
}
