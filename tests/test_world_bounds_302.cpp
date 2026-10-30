// Standalone real-resolver boundary tests; synthetic bytes only.
#include "../src/game/world_sigs.cpp"
#include <cassert>
#include <cstdio>
#include <vector>
namespace sco {
uint8_t* test_sig = nullptr;
uint8_t* Sig(const char*) { return test_sig; }
SigResult SigPattern(const Section&, const char*) { return {}; }
}
namespace {
void bytes(uint8_t* p, const char* pattern) {
    unsigned value = 0;
    while (*pattern) {
        if (*pattern == ' ') { ++pattern; continue; }
        if (*pattern == '?') { *p++ = 0; while (*pattern == '?') ++pattern; }
        else { std::sscanf(pattern, "%2x", &value); *p++ = static_cast<uint8_t>(value); pattern += 2; }
    }
}
void rip(uint8_t* p, uint8_t* target, size_t disp=3, size_t len=7) {
    const auto d = static_cast<int32_t>(target - (p + len)); std::memcpy(p+disp,&d,4);
}
}
int main() {
    using namespace sco; using namespace sco::game;
    uint8_t arena[4096]{};
    Image img{}; img.base=arena;img.size=sizeof arena;img.text={arena,2048};img.rdata={arena+2048,2048};
    uint8_t* refs[4]{};
    bytes(arena+2041,"48 8D 15 ?? ?? ?? ??");rip(arena+2041,arena+2048);
    assert(RipRefs(img,0x8D,0x15,arena+2048,0,7,refs,4,[](const uint8_t*){return true;})==1);
    auto missing=img;missing.size=0;
    assert(RipRefs(missing,0x8D,0x15,arena+2048,0,7,refs,4,[](const uint8_t*){return true;})==0);
    assert(!InImage(missing,arena));
    Image wrap{};wrap.base=reinterpret_cast<uint8_t*>(UINTPTR_MAX-8);wrap.size=32;assert(!InImage(wrap,wrap.base));
    // mission_load_all: registration at the first instruction cannot subtract 12.
    std::memset(arena,0,sizeof arena);std::strcpy(reinterpret_cast<char*>(arena+2048),"mission_load_all");
    bytes(arena,"48 8D 15 ?? ?? ?? ??");rip(arena,arena+2048);
    assert(ResolveLoadAll(img).state!=SigState::Ok);
    // Physically truncated tail of handler must reject the +0x2e read.
    bytes(arena+32,"48 8D 15 ?? ?? ?? ??");rip(arena+32,arena+2048);std::memset(arena,0,7);
    bytes(arena+20,"4C 8D 05 ?? ?? ?? ??");rip(arena+20,arena+256);
    bytes(arena+256,"40 53 48 83 EC 20 48 8B 01 48 8B D9 FF 50 08 83 F8 02 7C");
    img.text.size=256+0x20;
    auto r=ResolveLoadAll(img);assert(r.state!=SigState::Ok && r.why && std::strcmp(r.why,"layout changed at +0x02e")==0);
    img.text.size=2048;bytes(arena+256+0x2E,"48 8B 0D ?? ?? ?? ?? 48 8B 01 FF 90 A0 00 00 00");
    assert(ResolveLoadAll(img).at==arena+256);
    rip(arena+20,arena+3000);assert(ResolveLoadAll(img).state!=SigState::Ok);
    // Complete handler prologue physically ends at the allocation boundary.
    {
        std::vector<uint8_t> physical(512, 0);
        Image edge{};edge.base=physical.data();edge.size=static_cast<uint32_t>(physical.size());
        edge.rdata={physical.data(),64};edge.text={physical.data()+64,448};
        std::strcpy(reinterpret_cast<char*>(physical.data()),"mission_load_all");
        auto* registration=physical.data()+80;auto* label=registration+12;
        auto* handler=physical.data()+480;
        bytes(registration,"4C 8D 05 ?? ?? ?? ??");rip(registration,handler);
        bytes(label,"48 8D 15 ?? ?? ?? ??");rip(label,physical.data());
        bytes(handler,"40 53 48 83 EC 20 48 8B 01 48 8B D9 FF 50 08 83 F8 02 7C");
        assert(ResolveLoadAll(edge).state!=SigState::Ok);
    }
    // FileChange scan includes section start and checks both prologue and far load.
    std::memset(arena,0,sizeof arena);std::strcpy(reinterpret_cast<char*>(arena+2048),"void __cdecl Subsumption::XmlFileLibrary::OnFileChange(const struct SFileChangeInfo &)");
    bytes(arena,"48 89 5C 24 18 55 56 57 41 54 41 55 41 56 41 57 48 8D AC 24");
    bytes(arena+64,"48 8D 05 ?? ?? ?? ??");rip(arena+64,arena+2048);img.text.size=128;
    assert(ResolveFileChange(img).state!=SigState::Ok);
    img.text.size=2048;bytes(arena+0x203,"48 8B 0D ?? ?? ?? ??");assert(ResolveFileChange(img).at==arena);
    // Vtable subtraction must never form before-section pointers.
    std::memset(arena,0,sizeof arena);test_sig=arena;uint64_t q=reinterpret_cast<uintptr_t>(arena);std::memcpy(arena+2048,&q,8);
    assert(ResolveEntityVtable(img).state!=SigState::Ok);
    test_sig=reinterpret_cast<uint8_t*>(UINTPTR_MAX-8);
    assert(ResolveEntitySlot<0x430>(img).state!=SigState::Ok);
    assert(ResolveReferenceIntMethod<0x70>(img).state!=SigState::Ok);
    assert(ResolveCameraFields(img).state!=SigState::Ok);
    // Relocated and preferred-base slots require complete .rdata extent.
    assert(SlotTarget(img,arena+2048)==arena);assert(!SlotTarget(missing,arena+2048));
    img.rdata.size=7;assert(!SlotTarget(img,arena+2048));
    for(size_t n=1;n<0x250;++n) {
        std::vector<uint8_t> small(n,0); Image tiny{};tiny.base=small.data();tiny.size=static_cast<uint32_t>(n);tiny.text={small.data(),n};tiny.rdata={small.data(),n};
        test_sig=small.data()+n-1;
        assert(!GroundRaySite(tiny,test_sig));
        (void)ResolvePhysWorld(tiny);
        assert(ResolveCameraFields(tiny).state!=SigState::Ok);
        (void)ResolveEntityVtable(tiny);(void)ResolveReferenceIntTable(tiny);
        (void)ResolveEntitySlot<0x430>(tiny);(void)ResolveReferenceIntMethod<0x70>(tiny);
        (void)ResolveLoadAll(tiny);(void)ResolveFileChange(tiny);
    }
    std::puts("world: final RIP, missing/wrapped image, load-all prefix/tail/target, FileChange start/tail, vtable underflow, slots and 591 physical allocation sizes PASS");
}
