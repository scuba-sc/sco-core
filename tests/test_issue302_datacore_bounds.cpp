// SPDX-License-Identifier: GPL-3.0-only
// SPDX-FileCopyrightText: 2026 the sco-core contributors
// Portable resolver regression: compile with src/sco_scan.cpp.
#include "sco/signatures.h"
#include <cassert>
#include <cstring>
#include <cstdint>
#include <limits>

namespace sco {
static uint8_t* fixtureTable;
static uint8_t* fixtureWrapper;
static uint8_t* match;
uint8_t* Sig(const char* id) {
    return std::strcmp(id,"datacore_live.deleting")==0 ? fixtureWrapper : fixtureTable;
}
SigResult SigPattern(const Section&, const char*) {
    return match ? SigOk(match) : SigResult{SigState::Missing};
}
}
#include "../src/game/datacore_live_sigs.cpp"

// Invoke the real resolver with a registry-created context and synthetic dependencies.
#include "sco/signature_registry.h"
#include <functional>
#include <cstdlib>
namespace {
std::function<sco::SigResult(const sco::SigResolveContext&)> fixtureCall;
sco::SigResult fixtureResult;
sco::SigResult fixtureDependency(const sco::Image&, const sco::SigResolveContext&) { return sco::SigOk(sco::fixtureTable); }
sco::SigResult fixtureWrapper(const sco::Image&, const sco::SigResolveContext&) { return sco::SigOk(sco::fixtureWrapper); }
sco::SigResult fixtureTarget(const sco::Image&, const sco::SigResolveContext& ctx) { fixtureResult = fixtureCall(ctx); return sco::SigOk(nullptr); }
sco::SigResult withContext(std::function<sco::SigResult(const sco::SigResolveContext&)> call) {
    fixtureCall = std::move(call);
    sco::SigDef rows[] = { {"actor.get_stat", nullptr, 0, 0, fixtureDependency, {nullptr}}, {"actor.adjust_stat", nullptr, 0, 0, fixtureDependency, {nullptr}}, {"inventory.unequip_item", nullptr, 0, 0, fixtureDependency, {nullptr}}, {"datacore_live.owner_vtable", nullptr, 0, 0, fixtureDependency, {nullptr}}, {"datacore_live.deleting", nullptr, 0, 0, fixtureWrapper, {nullptr}}, {"entities.dump", nullptr, 0, 0, fixtureDependency, {nullptr}}, {"teleport.to_camera", nullptr, 0, 0, fixtureDependency, {nullptr}}, {"teleport.entity_system", nullptr, 0, 0, fixtureDependency, {nullptr}}, {"contracts.reputation_services", nullptr, 0, 0, fixtureDependency, {nullptr}},  {"fixture.target", nullptr, 0, 0, fixtureTarget, {nullptr}} };
    sco::SignatureTable fixtureTable{rows, sizeof(rows)/sizeof(rows[0])};
    sco::SignatureRegistry registry;
    if (registry.RegisterTables(&fixtureTable, 1).status != sco::SigRegistryStatus::Ok ||
        registry.Freeze() != sco::SigRegistryStatus::Ok ||
        registry.Resolve(sco::Image{}) != sco::SigRegistryStatus::Ok) std::abort();
    return fixtureResult;
}
}

static void rip(uint8_t* instruction, size_t disp, size_t len, const uint8_t* dest) {
    auto d=static_cast<int32_t>(dest-(instruction+len));
    std::memcpy(instruction+disp,&d,4);
}
int main() {
    uint8_t buffer[1024]{};
    sco::Image img{};
    img.base=buffer; img.size=sizeof(buffer);
    img.text={buffer+128,256}; img.rdata={buffer+512,256};
    const auto* defs=sco::game::kDataCoreLiveSignatures;
    sco::fixtureTable=nullptr;
    assert(withContext([&](const sco::SigResolveContext& ctx) { return defs[2].resolve(img, ctx); }).state==sco::SigState::Failed);
    sco::fixtureTable=buffer+512+252;
    assert(withContext([&](const sco::SigResolveContext& ctx) { return defs[2].resolve(img, ctx); }).state==sco::SigState::Failed);
    sco::fixtureTable=reinterpret_cast<uint8_t*>(std::numeric_limits<uintptr_t>::max()-3);
    assert(withContext([&](const sco::SigResolveContext& ctx) { return defs[3].resolve(img, ctx); }).state==sco::SigState::Failed);
    sco::fixtureTable=buffer+512;
    uint8_t reset[]={0x4c,0x8b,0xdc,0x53,0x56,0x57,0x48,0x81,0xec,0x80,0,0,0};
    uint8_t marker[]={0xc7,0x81,0xc8,1,0,0,3,0,0,0};
    std::memcpy(buffer+128,reset,sizeof(reset));
    std::memcpy(buffer+128+0x37,marker,sizeof(marker));
    uint64_t ptr=reinterpret_cast<uintptr_t>(buffer+128);
    std::memcpy(sco::fixtureTable+8,&ptr,8);
    assert(withContext([&](const sco::SigResolveContext& ctx) { return defs[2].resolve(img, ctx); }).state==sco::SigState::Ok);
    auto truncated=img; truncated.text.size=0x40;
    assert(withContext([&](const sco::SigResolveContext& ctx) { return defs[2].resolve(truncated, ctx); }).state==sco::SigState::Failed);
    sco::fixtureWrapper=nullptr;
    assert(withContext([&](const sco::SigResolveContext& ctx) { return defs[4].resolve(img, ctx); }).state==sco::SigState::Failed);
    sco::fixtureWrapper=buffer+128+img.text.size-0x10;
    assert(withContext([&](const sco::SigResolveContext& ctx) { return defs[4].resolve(img, ctx); }).state==sco::SigState::Failed);
    sco::fixtureWrapper=buffer+128;
    rip(sco::fixtureWrapper+0xf,1,5,buffer+512);
    assert(withContext([&](const sco::SigResolveContext& ctx) { return defs[4].resolve(img, ctx); }).state==sco::SigState::Failed);
    sco::match=buffer+128;
    rip(sco::match,3,7,buffer+1017);
    assert(withContext([&](const sco::SigResolveContext& ctx) { return defs[1].resolve(img, ctx); }).state==sco::SigState::Failed);
    rip(sco::match,3,7,buffer+600);
    assert(withContext([&](const sco::SigResolveContext& ctx) { return defs[1].resolve(img, ctx); }).state==sco::SigState::Ok);
    auto noImage=img; noImage.base=nullptr; noImage.size=0;
    assert(withContext([&](const sco::SigResolveContext& ctx) { return defs[1].resolve(noImage, ctx); }).state==sco::SigState::Failed);
    rip(sco::match,3,7,buffer+512+17);
    assert(withContext([&](const sco::SigResolveContext& ctx) { return defs[0].resolve(img, ctx); }).state==sco::SigState::Failed);
}