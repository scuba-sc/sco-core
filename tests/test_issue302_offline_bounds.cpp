#include "sco/signature_registry.h"
#include "../src/game/offline_sigs.cpp"
#include <cassert>
#include <cstring>
#include <vector>
#include <string>
#include "issue302_test_support.h"
namespace sco {
uint8_t* dependency = nullptr;
uint8_t* Sig(const char*) { return dependency; }
SigResult SigPattern(const Section& s, const char* pattern) {
    uint8_t* at = nullptr;
    int n = FindPattern(s, pattern, &at, 1);
    return n == 1 ? SigOk(at) : SigResult{SigState::Missing, nullptr, n, nullptr};
}
}

// Capture a real registry-owned resolver context while retaining direct-call coverage.
namespace issue302_context {
static const sco::Image* image;
static sco::SigResolver resolver;
static uint8_t* dependency;
static sco::SigResult result;
static sco::SigResult Dependency(const sco::Image&, const sco::SigResolveContext&) {
    return dependency ? sco::SigOk(dependency) : sco::SigFail("missing fixture dependency");
}
static sco::SigResult Invoke(const sco::Image&, const sco::SigResolveContext& context) {
    result = resolver(*image, context);
    static uint8_t marker;
    return sco::SigOk(&marker);
}
static sco::SigResult Run(const sco::Image& img, sco::SigResolver fn, uint8_t* dep = nullptr) {
    image = &img; resolver = fn; dependency = dep; result = {};
    const sco::SigDef rows[] = {
        {"source", nullptr, 0, 0, Dependency, {}},
        {"system.quit", nullptr, 0, 0, Dependency, {}},
        {"offline.is_online_flag", nullptr, 0, 0, Dependency, {}},
        {"fixture", nullptr, 0, 0, Invoke, {}}
    };
    const sco::SignatureTable table{rows, 4};
    sco::SignatureRegistry registry;
    assert(registry.RegisterTables(&table, 1).status == sco::SigRegistryStatus::Ok);
    assert(registry.Freeze() == sco::SigRegistryStatus::Ok);
    assert(registry.Resolve({}) == sco::SigRegistryStatus::Ok);
    return result;
}
}
namespace issue302_context {
static const uint8_t* extraAt;
static const char* extraResult;
static const char* (*extra)(const sco::Image&, const uint8_t*, const sco::SigResolveContext&);
static sco::SigResult ExtraResolver(const sco::Image& img, const sco::SigResolveContext& ctx) {
    extraResult = extra(img, extraAt, ctx);
    return sco::SigFail("fixture");
}
static const char* RunExtra(const sco::Image& img, const uint8_t* at,
    const char* (*fn)(const sco::Image&, const uint8_t*, const sco::SigResolveContext&)) {
    extra = fn; extraAt = at; extraResult = nullptr;
    Run(img, ExtraResolver, sco::dependency);
    return extraResult;
}
}
namespace {
struct Fixture {
    std::vector<uint8_t> bytes;
    sco::Image img{};
    explicit Fixture(size_t textSize) : bytes(2048) {
        img.base = bytes.data(); img.size = static_cast<uint32_t>(bytes.size());
        img.text = {bytes.data(), textSize}; img.rdata = {bytes.data() + 1024, 1024};
    }
    uint8_t* str(size_t off, const char* s) {
        auto* p = img.rdata.base + off; std::memcpy(p, s, std::strlen(s) + 1); return p;
    }
    void rip(size_t off, const uint8_t* target, uint8_t rex = 0x48, uint8_t mod = 0x15) {
        auto* p = bytes.data() + off; p[0] = rex; p[1] = 0x8d; p[2] = mod;
        int32_t d = static_cast<int32_t>(target - (p + 7)); std::memcpy(p + 3, &d, 4);
    }
};
void boot() {
    Fixture f(14);
    auto* a = f.str(0, "Frontend_Main"); auto* b = f.str(32, "SC_Frontend");
    f.rip(0, b, 0x4c, 0x05); f.rip(7, a);
    auto r = issue302_context::Run(f.img, sco::game::ResolveBootFrontend, sco::dependency);
    assert(r.state == sco::SigState::Ok && r.at == f.img.text.base);
}
void shortScans() {
    Fixture f(0);
    f.str(0, "sessionGuid"); f.str(32, "Libs/OfflineDB");
    uint8_t* sites[32]{}; bool same = true;
    for (size_t n = 0; n < 180; ++n) {
        f.img.text.size = n;
        assert(sco::game::HandshakeSites(f.img, sites, 32, same) == 0);
        assert(sco::game::SocialGroupSites(f.img, sites, 32) == 0);
        assert(issue302_context::Run(f.img, sco::game::ResolveOfflineDb<1>, sco::dependency).state != sco::SigState::Ok);
        assert(!sco::game::FindLeaFrom(f.img.text, f.img.rdata.base, f.img.text.base));
    }
}
void extras() {
    using namespace sco::game;
    Fixture f(0x7c);
    f.str(0, "$$: Vehicle '$$' [$$] impounded (or destroyed) by restricted area '$$' [$$]");
    f.str(128, "Boundary Violation");
    f.rip(0x59, f.img.rdata.base, 0x4c, 0x0d);
    f.rip(0x79, f.img.rdata.base + 128, 0x4c, 0x05);
    assert(issue302_context::RunExtra(f.img, f.img.text.base, ImpoundExtra));
    f.str(256, "QueryEntitlements failed with error: $$");
    f.rip(0x55, f.img.rdata.base + 256, 0x4c, 0x0d);
    f.img.text.size = 0x58;
    assert(issue302_context::RunExtra(f.img, f.img.text.base, EntitlementsExtra));
    f.img.text.size = 30; sco::dependency = f.img.rdata.base;
    f.str(384, "Can only perform ASOP operations in the PU.");
    f.rip(27, f.img.rdata.base + 384, 0x4c, 0x0d);
    auto* p = f.img.text.base; p[0] = 0x80; p[1] = 0x3d;
    int32_t d = static_cast<int32_t>((sco::dependency + 1) - (p + 7));
    std::memcpy(p + 2, &d, 4);
    assert(issue302_context::RunExtra(f.img, p, AsopGateValidationExtra));
    sco::dependency = nullptr;
    assert(issue302_context::RunExtra(f.img, p, AsopGateOpenExtra));
}
void megamap() {
    Fixture f(7); auto* help = f.str(0, "Load a map, same usage as 'megamap' cvar.");
    f.rip(0, help); f.rip(7, f.img.text.base + 64, 0x4c, 0x05);
    const uint8_t pair[] = {0x48,0x8d,0x4c,0x24,0x30,0xe8};
    std::memcpy(f.bytes.data() + 64, pair, 6); std::memcpy(f.bytes.data() + 74, pair, 6);
    const uint8_t prologue[] = {0x4c,0x8b,0x09,0x4c,0x8b,0xd1,0x41,0x0f,0xb6,0x01,0x84,0xc0};
    std::memcpy(f.bytes.data() + 512, prologue, sizeof(prologue));
    int32_t d = 512 - 84; std::memcpy(f.bytes.data() + 80, &d, 4);
    assert(issue302_context::Run(f.img, sco::game::ResolveMegamapCall, sco::dependency).state != sco::SigState::Ok);
    f.img.text.size = 524;
    assert(issue302_context::Run(f.img, sco::game::ResolveMegamapCall, sco::dependency).state == sco::SigState::Ok);
}
void exactTail() {
    issue302::GuardPages memory;
    auto* text = memory.data + memory.page - 7;
    auto* help = memory.data;
    std::strcpy(reinterpret_cast<char*>(help), "Load a map, same usage as 'megamap' cvar.");
    text[0] = 0x48; text[1] = 0x8d; text[2] = 0x15;
    const int32_t displacement = static_cast<int32_t>(help - (text + 7));
    std::memcpy(text + 3, &displacement, 4);
    sco::Image img{};
    img.base = memory.data; img.size = static_cast<uint32_t>(memory.page);
    img.text = {text, 7}; img.rdata = {help, 64};
    assert(issue302_context::Run(img, sco::game::ResolveMegamapCall, sco::dependency).state != sco::SigState::Ok);
}
void boundaryResolvers() {
    using namespace sco::game;
    Fixture f(7);
    auto* cvar = f.str(0, kUseService); f.rip(0, cvar);
    assert(issue302_context::Run(f.img, ResolveCvarDefault<kUseService>, sco::dependency).state != sco::SigState::Ok);
    auto* usage = f.str(128, "Invalid arguments. Usage: g_ATC_requestTakeOff <atc_name (autocompletable)> [<ship_archetype>] [<pad_name_filter>]");
    f.rip(0, usage);
    assert(issue302_context::Run(f.img, ResolveTakeoffCommand, sco::dependency).state != sco::SigState::Ok);
    auto* console = f.str(384, "Remote console listening on: %u\n");
    f.img.text.size = 263; f.rip(256, console);
    const uint8_t bind[] = {0x33,0xc9,0xff,0x15,0,0,0,0,0x0f,0xb7,0xcb,0x66,0x89,0x7c,0x24,0x48,0x89,0x44,0x24,0x4c};
    std::memcpy(f.bytes.data() + 250, bind, sizeof(bind));
    f.rip(256, console);
    assert(issue302_context::Run(f.img, ResolveRemoteConsoleBind, sco::dependency).state != sco::SigState::Ok);
    sco::dependency = reinterpret_cast<uint8_t*>(UINTPTR_MAX - 4);
    assert(issue302_context::Run(f.img, ResolveQuitFastShutdown, sco::dependency).state != sco::SigState::Ok);
    assert(issue302_context::RunExtra(f.img, f.img.text.base, AsopGateOpenExtra));
    sco::dependency = nullptr;
    assert(!FindLeaFrom(f.img.text, console, nullptr));
    assert(!FindLeaFrom(f.img.text, console, reinterpret_cast<uint8_t*>(1)));
}
void socialTails() {
    using namespace sco::game;
    Fixture f(32);
    auto* p = f.img.text.base;
    p[0] = 0x48; p[1] = 0x8b; p[2] = 0x0d;
    int32_t d = 1024 - 7; std::memcpy(p + 3, &d, 4);
    const uint8_t body[] = {0x48,0x8b,0x01,0xff,0x50,0x18,0xff,0x50,0x70,0xff,0x90,0xb0,0,0,0};
    std::memcpy(p + 7, body, sizeof(body));
    uint8_t* sites[1]{};
    for (size_t n = 0; n <= 32; ++n) {
        f.img.text.size = n;
        assert(SocialGroupSites(f.img, sites, 1) == (n >= 22 ? 1 : 0));
    }
    f.img.text = {};
    bool same = true;
    f.str(64, "sessionGuid");
    assert(HandshakeSites(f.img, sites, 1, same) == 0);
    assert(SocialGroupSites(f.img, sites, 1) == 0);
}
void scanEndpoints() {
    using namespace sco::game;
    Fixture f(31); auto* guid = f.str(0, "sessionGuid");
    auto* p = f.img.text.base;
    p[0] = 0x44; p[1] = 0x38; p[2] = 0x05; p[7] = 0x0f; p[8] = 0x84;
    int32_t flag = 900 - 7; std::memcpy(p + 3, &flag, 4);
    f.rip(24, guid);
    uint8_t* sites[4]{}; bool same = true;
    assert(HandshakeSites(f.img, sites, 4, same) == 1);
    f.img.text.size = 30;
    assert(HandshakeSites(f.img, sites, 4, same) == 0);
    auto* db = f.str(64, "Libs/OfflineDB");
    std::memset(p, 0, 31); f.rip(0, db, 0x4c, 0x05); f.rip(7, db, 0x4c, 0x05);
    f.img.text.size = 14;
    assert(issue302_context::Run(f.img, ResolveOfflineDb<2>, sco::dependency).state == sco::SigState::Ok);
    f.img.text.size = 13;
    assert(issue302_context::Run(f.img, ResolveOfflineDb<2>, sco::dependency).state != sco::SigState::Ok);
}
void fastShutdownTail() {
    const uint8_t pattern[] = {
        0x49,0x8b,0x8e,0,0,0,0,0x48,0x85,0xc9,0x74,0x0a,0x48,0x8b,0x01,0xff,
        0x50,0x10,0x85,0xc0,0x75,0x0e,0x41,0x80,0xbe,0,0,0,0,0,0x0f,0x84
    };
    for (size_t available = sizeof pattern - 1; available <= sizeof pattern + 2; ++available) {
        issue302::GuardPages memory;
        auto* q = memory.data + memory.page - 0x31A - available;
        auto* test = q + 0x31A;
        std::memcpy(test, pattern, available < sizeof pattern ? available : sizeof pattern);
        sco::Image img{}; img.base = memory.data; img.size = static_cast<uint32_t>(memory.page);
        img.text = {q, 0x31A + available}; sco::dependency = q;
        auto r = issue302_context::Run(img, sco::game::ResolveQuitFastShutdown, sco::dependency);
        if (available < sizeof pattern) {
            assert(r.state == sco::SigState::Failed && !r.at && r.why && std::strstr(r.why, "outside .text"));
        } else {
            if (r.state != sco::SigState::Ok || r.at != test)
                std::fprintf(stderr, "FAIL fastshutdown available=%zu: %s\n", available, r.why ? r.why : "no reason");
            assert(r.state == sco::SigState::Ok && r.at == test);
            test[0] ^= 1;
            r = issue302_context::Run(img, sco::game::ResolveQuitFastShutdown, sco::dependency);
            assert(r.state == sco::SigState::Failed && !r.at && r.why && std::strstr(r.why, "moved"));
        }
    }
    sco::dependency = nullptr;
}
void finalLea() {
    Fixture f(7); auto* s = f.str(0, "test"); f.rip(0, s);
    assert(sco::game::FindLeaFrom(f.img.text, s, f.img.text.base) == f.img.text.base);
    f.img.text.size = 6;
    assert(!sco::game::FindLeaFrom(f.img.text, s, f.img.text.base));
}
}
int main(int argc, char** argv) {
    std::string mode = argc > 1 ? argv[1] : "all";
    if (mode == "all" || mode == "fastshutdown") fastShutdownTail();
    if (mode == "all" || mode == "endpoints") scanEndpoints();
    if (mode == "all" || mode == "boundaries") boundaryResolvers();
    if (mode == "all" || mode == "social") socialTails();
    if (mode == "all" || mode == "tail") exactTail();
    if (mode == "all" || mode == "boot") boot();
    if (mode == "all" || mode == "short") shortScans();
    if (mode == "all" || mode == "extras") extras();
    if (mode == "all" || mode == "megamap") megamap();
    if (mode == "all" || mode == "lea") finalLea();
}
