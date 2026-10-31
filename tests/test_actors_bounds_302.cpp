// Standalone: link src/sco_scan.cpp; use -fsanitize=address,undefined.
#include "../src/game/actors_sigs.cpp"
#include <cassert>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <vector>

static uint8_t* source;
namespace sco {
uint8_t* Sig(const char*) { return source; }
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
        auto r = ResolveFindEntityByName(f.img);
        assert(r.state == SigState::Ok && r.at == fn);
        std::puts("PASS final-position RIP reference"); return 0;
    }
    // Physically short allocation: not merely a shortened advertised section.
    source = static_cast<uint8_t*>(std::malloc(1)); *source = 0xe8;
    Image tiny{}; tiny.base = source; tiny.size = 1; tiny.text = {source, 1};
    assert(ResolveSetGodMode(tiny).state == SigState::Failed);
    assert(ResolveCall<kTeamCategory>(tiny).state == SigState::Failed);
    assert(ResolveSeatCallback(tiny).state == SigState::Failed);
    assert(ResolveSeatInteractable(tiny).state == SigState::Failed);
    assert(ResolveTeamTag(tiny).state == SigState::Failed);
    assert(CheckLoadoutContainer(tiny, source));
    std::free(source); source = nullptr;
    std::puts("PASS physically truncated dependent sources");
    for (size_t size = 1; size <= 0x250; ++size) {
        source = static_cast<uint8_t*>(std::calloc(size, 1));
        Image clipped{}; clipped.base = source; clipped.size = static_cast<uint32_t>(size); clipped.text = {source, size};
        (void)ResolveSetGodMode(clipped);
        (void)ResolveCall<kTeamCategory>(clipped);
        (void)ResolveSeatCallback(clipped);
        (void)ResolveSeatInteractable(clipped);
        (void)ResolveTeamTag(clipped);
        (void)CheckLoadoutContainer(clipped, source);
        std::free(source);
    }
    source = nullptr;
    std::puts("PASS physical source extent sweep 1..0x250");
    Fixture f; source = f.img.text.base;
    auto* call = source + 0x12;
    Put(call, "E8"); Rip(call, source + 512, 1, 5);
    Put(source + 512, "48 89 5C 24 08 57 48 83 EC 20 88 91");
    assert(ResolveSetGodMode(f.img).state == SigState::Ok);
    Image missing = f.img; missing.base = nullptr; missing.size = 0;
    assert(ResolveSetGodMode(missing).state != SigState::Ok);
    Image shortImage = f.img; shortImage.size = 512;
    assert(ResolveSetGodMode(shortImage).state != SigState::Ok);
    Rip(call, f.img.rdata.base, 1, 5);
    assert(ResolveSetGodMode(f.img).state != SigState::Ok);
    Rip(call, f.img.base + f.img.size, 1, 5);
    assert(ResolveSetGodMode(f.img).state != SigState::Ok);
    int32_t under = INT32_MIN; std::memcpy(call + 1, &under, 4);
    assert(ResolveSetGodMode(f.img).state != SigState::Ok);
    std::puts("PASS call target text/image/missing-image/extreme-displacement proofs");
    // A callback target can be inside .text yet lack its checked tail.
    source = f.img.text.base;
    auto* lea = source + 0x21c;
    for (const auto& c : kSeatChecks) Put(source + c.off, c.bytes);
    Put(lea, "48 8D 05"); Rip(lea, f.img.text.base + f.img.text.size - 1);
    assert(ResolveSeatCallback(f.img).state != SigState::Ok);
    std::puts("PASS derived callback truncated tail");
    // Flight Ready's LEA occupies the final seven bytes of the declared text.
    Fixture flight;
    const auto* header = flight.String("C:\\workspace\\CryEngine\\Code\\CryEngine\\CryCommon\\Events/ISC/Dashboards.h");
    auto* last = flight.img.text.base + flight.img.text.size - 7;
    auto* start = last - 48;
    start[-2] = start[-1] = 0xcc;
    Put(start, "48 89 5C 24 48 8B FA 4C 89 44 24 41 B8 49 00 00 00");
    Put(last, "48 8D 15"); Rip(last, header);
    auto ready = ResolveFlightReady(flight.img);
    assert(ready.state == SigState::Ok && ready.at == start);
    flight.img.base = nullptr; flight.img.size = 0;
    assert(ResolveFlightReady(flight.img).state != SigState::Ok);
    std::puts("PASS Flight Ready final LEA and missing image");
    // String target requires the whole NUL-terminated name in declared .rdata.
    Fixture container;
    source = container.img.text.base;
    auto* typeLea = source + 0xbb;
    const auto* type = container.String("IItemPortContainer");
    Put(typeLea, "4C 8D 05"); Rip(typeLea, type);
    assert(!CheckLoadoutContainer(container.img, source));
    container.img.rdata.size = std::strlen("IItemPortContainer");
    assert(CheckLoadoutContainer(container.img, source));
    container.img.rdata.size = 2048;
    container.img.base = nullptr; container.img.size = 0;
    assert(CheckLoadoutContainer(container.img, source));
    std::puts("PASS type name NUL extent and missing image");
    std::puts("actors bounds regressions passed");
}
