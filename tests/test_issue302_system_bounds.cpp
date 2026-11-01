#include "sco/signature_registry.h"
#include "../src/game/system_sigs.cpp"
#include <cassert>
#include <cstring>
#include <cstdlib>
#include <initializer_list>
#include "issue302_test_support.h"
namespace sco { uint8_t* Sig(const char*) { return nullptr; } }
namespace sco {
SigResult SigPattern(const Section& text, const char* pattern) {
    int n = 0; auto p = FindUniquePattern(text, pattern, n);
    return n == 1 ? SigOk(p) : SigResult{n ? SigState::Ambiguous : SigState::Missing, nullptr, n, nullptr};
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
int main() {
    issue302::GuardPages memory(true);
    const size_t page = memory.page;
    auto* mem = memory.data;
    auto image = mem + page;
    std::memcpy(image + 64, sco::game::kQuitLog, sizeof(sco::game::kQuitLog));
    image[0] = 0x4c; image[1] = 0x8d; image[2] = 0x0d;
    int32_t disp = 64 - 7; std::memcpy(image + 3, &disp, 4);
    sco::Image img{}; img.base=image; img.size=static_cast<uint32_t>(page); img.text={image, 9}; img.rdata={image+64, sizeof(sco::game::kQuitLog)};
    auto result = issue302_context::Run(img, sco::game::ResolveQuit);
    assert(result.state != sco::SigState::Ok);
    img.text = {reinterpret_cast<uint8_t*>(UINTPTR_MAX - 3), 8};
    result = issue302_context::Run(img, sco::game::ResolveQuit);
    assert(result.state == sco::SigState::Failed);
    assert(std::strcmp(result.why, "invalid .text") == 0);
    img.text={image, 0x17d};
    img.rdata={image+512, sizeof(sco::game::kQuitLog)};
    std::memset(image, 0, page);
    std::memcpy(image+512, sco::game::kQuitLog, sizeof(sco::game::kQuitLog));
    const struct { size_t off; const char* pattern; } checks[] = {
        {0, "48 89 5C 24 10 48 89 74 24 18 48 89 7C 24 20"},
        {0xf, "55 41 54 41 55 41 56 41 57"}, {0x18, "48 8D AC 24 00 FF FF FF"},
        {0x20, "48 81 EC 00 02 00 00"}, {0x27, "45 8B F8 48 8B F2 4C 8B F1"},
        {0x35, "41 83 F8 4E"}, {0xbb, "4C 8D 05"}, {0xdd, "E8"},
        {0xa3, "4C 8D 0D"}, {0x176, "4C 8D 0D"},
    };
    for (const auto& c : checks) {
        size_t off=c.off;
        for (const char* p=c.pattern; *p; p += p[2] ? 3 : 2) {
            char byte[3]={p[0],p[1],0}; image[off++]=static_cast<uint8_t>(std::strtoul(byte,nullptr,16));
        }
    }
    for (size_t off : {size_t{0xa3}, size_t{0x176}}) {
        int32_t d=static_cast<int32_t>(512-off-7); std::memcpy(image+off+3,&d,4);
    }
    result=issue302_context::Run(img, sco::game::ResolveQuit);
    assert(result.state==sco::SigState::Ok && result.at==image);
    img.text.size=0x17c;
    result=issue302_context::Run(img, sco::game::ResolveQuit);
    assert(result.state==sco::SigState::Failed && std::strcmp(result.why,"Quit function check outside .text")==0);
    img.text.size=0x17d; image[0x35]=0;
    result=issue302_context::Run(img, sco::game::ResolveQuit);
    assert(std::strcmp(result.why,"layout changed at +0x035")==0);
    img.rdata={reinterpret_cast<uint8_t*>(UINTPTR_MAX-1),8};
    assert(std::strcmp(issue302_context::Run(img, sco::game::ResolveQuit).why,"invalid .rdata")==0);
}
