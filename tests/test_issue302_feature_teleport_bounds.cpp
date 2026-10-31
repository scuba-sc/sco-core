#include "sco/scan.h"
#include "sco/signatures.h"
#include <cstdio>
#include <cstring>
#include <vector>
#include <algorithm>
namespace sco {
uint8_t* fixtureSource = nullptr;
uint8_t* Sig(const char*) { return fixtureSource; }
SigResult SigPattern(const Section&, const char*) { return {}; }
}
#include "../src/game/features_sigs.cpp"
#include "../src/game/teleport_sigs.cpp"

// Invoke the real resolver with a registry-created context and synthetic dependencies.
#include "sco/signature_registry.h"
#include <functional>
#include <cstdlib>
namespace {
std::function<sco::SigResult(const sco::SigResolveContext&)> fixtureCall;
sco::SigResult fixtureResult;
sco::SigResult fixtureDependency(const sco::Image&, const sco::SigResolveContext&) { return sco::SigOk(sco::fixtureSource); }
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
    using namespace sco;
    int failures = 0;
    auto check = [&](bool ok, const char* name) { if (!ok) { std::printf("FAIL %s\n", name); ++failures; } };
    std::vector<uint8_t> data(1024);
    Image img{}; img.base = data.data(); img.size = static_cast<uint32_t>(data.size()); img.text = {data.data(), 64};
    fixtureSource = data.data() + 60;
    auto r = withContext([&](const sco::SigResolveContext& ctx) { return game::kTeleportSignatures[1].resolve(img, ctx); });
    check(r.state == SigState::Failed && r.at == nullptr, "teleport truncated derived instruction");
    fixtureSource = data.data(); img.text.size = 128;
    int32_t displacement = 0; std::memcpy(data.data() + 0x27, &displacement, 4);
    img.base = nullptr; img.size = 0;
    r = withContext([&](const sco::SigResolveContext& ctx) { return game::kTeleportSignatures[1].resolve(img, ctx); });
    check(r.state == SigState::Failed, "teleport missing image metadata");
    img.base = data.data(); img.size = static_cast<uint32_t>(data.size());
    r = withContext([&](const sco::SigResolveContext& ctx) { return game::kTeleportSignatures[1].resolve(img, ctx); });
    check(r.state == SigState::Ok && r.at == data.data() + 0x2b, "teleport valid RIP target");
    fixtureSource = nullptr;
    r = withContext([&](const sco::SigResolveContext& ctx) { return game::kTeleportSignatures[1].resolve(img, ctx); });
    check(r.state == SigState::Failed && r.at == nullptr, "teleport missing fixtureSource");
    const char header[] = "C:\\workspace\\CryEngine\\Code\\CryEngine\\CryCommon\\Events/VFX/EntityEffectSystem.h";
    std::memcpy(data.data()+512, header, sizeof header); img.rdata = {data.data()+512, sizeof header};
    const uint8_t sender[] = {0x48,0x89,0x5c,0x24,0x08,0x57,0x48,0x83,0xec,0x50,0x8b,0x05,0,0,0,0,0x48,0x8b,0xfa,0x4c,0x89,0x44,0x24,0x20,0x48,0x8b,0xd9,0x85,0xc0,0x75,0x19,0x41,0xb8,0x17,0,0,0,0x48,0x8d,0x15};
    std::memcpy(data.data(), sender, sizeof sender); img.text.size = sizeof sender + 1;
    r = withContext([&](const sco::SigResolveContext& ctx) { return game::kFeatureSignatures[2].resolve(img, ctx); });
    check(r.state == SigState::Failed && r.why && std::strstr(r.why, "truncat"), "effect sender truncated displacement reason");
    std::fill(data.begin(), data.end(), uint8_t{0});
    const char label[] = "unsigned short __cdecl CSCActorActionHandler::Request<struct SCActorActionHandlerActions::SFlyMode,const enum ESCActorFlyMode&>(const enum ESCActorFlyMode &)";
    std::memcpy(data.data()+512, label, sizeof label); img.rdata = {data.data()+512, sizeof label};
    const uint8_t wrapper[] = {0x89,0x54,0x24,0x10,0x48,0x83,0xec,0x28,0x48,0x8d,0x54,0x24,0x38,0xe8};
    std::memcpy(data.data(), wrapper, sizeof wrapper); img.text.size = sizeof wrapper + 1;
    r = withContext([&](const sco::SigResolveContext& ctx) { return game::kFeatureSignatures[0].resolve(img, ctx); });
    check(r.state == SigState::Failed && r.why && std::strstr(r.why, "truncat"), "fly wrapper truncated displacement reason");
    img.base = nullptr; img.size = 0;
    r = withContext([&](const sco::SigResolveContext& ctx) { return game::kFeatureSignatures[0].resolve(img, ctx); });
    check(r.state == SigState::Failed && r.why && std::strstr(r.why, "image"), "fly rejects missing image before scanning");
    // Rejected generic prefixes must not suppress verified label matches.
    for (int effect = 0; effect < 2; ++effect) {
        for (int outside = 0; outside < 2; ++outside) {
            for (int hits = 0; hits <= 2; ++hits) {
                std::fill(data.begin(), data.end(), uint8_t{0});
                img.base = data.data(); img.size = static_cast<uint32_t>(data.size());
                img.text = {data.data(), 512}; img.rdata = {data.data() + 512, 512};
                const char* wanted = effect ? header : label;
                std::memcpy(img.rdata.base, wanted, std::strlen(wanted) + 1);
                auto rip = [&](size_t off, size_t disp, size_t len, size_t target) {
                    const int32_t d = static_cast<int32_t>(target) - static_cast<int32_t>(off + len);
                    std::memcpy(data.data() + off + disp, &d, sizeof d);
                };
                for (int i = 0; i < hits; ++i) {
                    const size_t at = static_cast<size_t>(i) * 64;
                    if (effect) {
                        std::memcpy(data.data() + at, sender, sizeof sender);
                        rip(at + 0x25, 3, 7, 512);
                    } else {
                        std::memcpy(data.data() + at, wrapper, sizeof wrapper);
                        rip(at, 0xE, 0x12, 96);
                        const size_t lea = 96 + 0xAE;
                        data[lea] = 0x48; data[lea + 1] = 0x8d; data[lea + 2] = 0x05;
                        rip(lea, 3, 7, 512);
                    }
                }
                const size_t bad = outside ? 400 : (effect ? 472 : 498);
                if (effect) {
                    std::memcpy(data.data() + bad, sender, sizeof sender);
                    if (outside) rip(bad + 0x25, 3, 7, 2000);
                } else {
                    std::memcpy(data.data() + bad, wrapper, sizeof wrapper);
                    if (outside) rip(bad, 0xE, 0x12, 2000);
                }
                r = withContext([&](const sco::SigResolveContext& ctx) { return game::kFeatureSignatures[effect ? 2 : 0].resolve(img, ctx); });
                const char* name = effect ? "effect rejected candidate coexistence" : "fly rejected candidate coexistence";
                if (hits == 1) check(r.state == SigState::Ok && r.at == data.data(), name);
                else if (hits == 2) check(r.state == SigState::Ambiguous && r.matches == 2 && !r.at, name);
                else check(r.state == SigState::Failed && !r.at && r.why &&
                           std::strstr(r.why, outside ? "outside" : "truncat"), name);
            }
        }
    }
    // Slot zero must not act as both an observed value and the unseen sentinel.
    for (bool invalidFirst : {true, false}) {
        std::fill(data.begin(), data.end(), uint8_t{0});
        img.base = data.data(); img.size = static_cast<uint32_t>(data.size());
        img.text = {data.data(), 512}; fixtureSource = data.data() + 768;
        const uint8_t site[] = {0x48,0x8B,0x0D,0,0,0,0,0x48,0x8B,0x13,0x48,0x8B,0x01,0xFF,0x90,0,0,0,0,0x48,0x83,0xC3,0x08,0x48,0x3B,0xDF,0x75,0xE4};
        for (int i = 0; i < 2; ++i) {
            auto* at = data.data() + 64 + i * 64;
            std::memcpy(at, site, sizeof site);
            const int32_t disp = static_cast<int32_t>(fixtureSource - (at + 7));
            std::memcpy(at + 3, &disp, 4);
            const int32_t slot = (i == 0) == invalidFirst ? 0 : 0x80;
            std::memcpy(at + game::kRemoveSlotDisp, &slot, 4);
        }
        r = withContext([&](const sco::SigResolveContext& ctx) { return game::ResolveRemoveEntityCall(img, ctx); });
        check(r.state != SigState::Ok, invalidFirst ? "RemoveEntity zero then valid slot refused" : "RemoveEntity valid then zero slot refused");
    }
    std::printf("%d failure(s)\n", failures);
    return failures ? 1 : 0;
}