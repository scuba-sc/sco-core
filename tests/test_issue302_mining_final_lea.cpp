#include "sco/scan.h"
#include "sco/signatures.h"
#include <cstdio>
#include <cstring>
namespace sco::game { extern const SigDef kMiningSignatures[]; }
namespace sco {
uint8_t* Sig(const char*) { return nullptr; }

SigResult SigPattern(const Section&, const char*) { return {}; }
}
// Invoke the real resolver with a registry-created context and synthetic dependencies.
#include "sco/signature_registry.h"
#include <functional>
#include <cstdlib>
namespace {
std::function<sco::SigResult(const sco::SigResolveContext&)> fixtureCall;
sco::SigResult fixtureResult;
sco::SigResult fixtureDependency(const sco::Image&, const sco::SigResolveContext&) { return sco::SigOk(nullptr); }
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
    uint8_t bytes[128]{};
    sco::Image img{}; img.base=bytes; img.size=sizeof(bytes);
    img.rdata={bytes, 64}; img.text={bytes+64, 7};
    std::memcpy(bytes, "CBiomeBuilder::BuildLargeScaleEcoSystem", 39);
    bytes[64]=0x4c; bytes[65]=0x8d; bytes[66]=0x05;
    int32_t disp = static_cast<int32_t>(bytes-(bytes+71));
    std::memcpy(bytes+67, &disp, sizeof(disp));
    auto r = withContext([&](const sco::SigResolveContext& ctx) { return sco::game::kMiningSignatures[0].resolve(img, ctx); });
    if (r.state == sco::SigState::Missing) { std::puts("FAIL: complete final LEA was skipped"); return 1; }
    std::puts("complete final LEA was inspected");
}
