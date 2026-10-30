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
    assert(defs[2].resolve(img).state==sco::SigState::Failed);
    sco::fixtureTable=buffer+512+252;
    assert(defs[2].resolve(img).state==sco::SigState::Failed);
    sco::fixtureTable=reinterpret_cast<uint8_t*>(std::numeric_limits<uintptr_t>::max()-3);
    assert(defs[3].resolve(img).state==sco::SigState::Failed);
    sco::fixtureTable=buffer+512;
    uint8_t reset[]={0x4c,0x8b,0xdc,0x53,0x56,0x57,0x48,0x81,0xec,0x80,0,0,0};
    uint8_t marker[]={0xc7,0x81,0xc8,1,0,0,3,0,0,0};
    std::memcpy(buffer+128,reset,sizeof(reset));
    std::memcpy(buffer+128+0x37,marker,sizeof(marker));
    uint64_t ptr=reinterpret_cast<uintptr_t>(buffer+128);
    std::memcpy(sco::fixtureTable+8,&ptr,8);
    assert(defs[2].resolve(img).state==sco::SigState::Ok);
    auto truncated=img; truncated.text.size=0x40;
    assert(defs[2].resolve(truncated).state==sco::SigState::Failed);
    sco::fixtureWrapper=nullptr;
    assert(defs[4].resolve(img).state==sco::SigState::Failed);
    sco::fixtureWrapper=buffer+128+img.text.size-0x10;
    assert(defs[4].resolve(img).state==sco::SigState::Failed);
    sco::fixtureWrapper=buffer+128;
    rip(sco::fixtureWrapper+0xf,1,5,buffer+512);
    assert(defs[4].resolve(img).state==sco::SigState::Failed);
    sco::match=buffer+128;
    rip(sco::match,3,7,buffer+1017);
    assert(defs[1].resolve(img).state==sco::SigState::Failed);
    rip(sco::match,3,7,buffer+600);
    assert(defs[1].resolve(img).state==sco::SigState::Ok);
    auto noImage=img; noImage.base=nullptr; noImage.size=0;
    assert(defs[1].resolve(noImage).state==sco::SigState::Failed);
    rip(sco::match,3,7,buffer+512+17);
    assert(defs[0].resolve(img).state==sco::SigState::Failed);
}
