#include "sco/signature_registry.h"
#include "../src/game/sig_rows.h"
#include <cassert>
#include <cstring>
#include <limits>
#include <cstdio>
#include "issue302_test_support.h"
namespace sco {
static uint8_t* source;
uint8_t* Sig(const char*) { return source; }
SigResult SigPattern(const Section& section, const char* pattern) {
    int matches = 0;
    auto* at = FindUniquePattern(section, pattern, matches);
    return matches == 1 ? SigOk(at) : SigResult{matches ? SigState::Ambiguous : SigState::Missing, nullptr, matches, nullptr};
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
constexpr sco::game::rows::RipSpec spec{"source", 0, 3, 7};
int main(int argc,char**) {
 using namespace sco; using namespace sco::game::rows;
 issue302::GuardPages memory;
 const size_t page=memory.page;
 auto* p=memory.data;
 Image img{}; img.base=p; img.size=static_cast<uint32_t>(page); img.text={p,page};
 source=p+page-3;
 if(argc>1) { auto r=issue302_context::Run(img, ResolveRip<spec>, source); assert(r.state==SigState::Failed); return 0; }
 uint8_t exact[]={0x48,0x8d,0x05,0,0,0,0}; uint8_t* hit=nullptr;
 assert(FindPattern({exact,7},"48 8D 05 ?? ?? ?? ??",&hit,1)==1);
 assert(FindRipLea({exact,7},0x48,0x8d,5,exact+7)==exact);
 source=p; std::memset(p,0,7);
 auto valid=issue302_context::Run(img, ResolveRip<spec>, source); assert(valid.state==SigState::Ok && valid.at==p+7);
 img.base=nullptr; img.size=0; assert(issue302_context::Run(img, ResolveRip<spec>, source).state==SigState::Failed);
 img.base=p; img.size=static_cast<uint32_t>(page);
 source=p+page-3; auto truncated=issue302_context::Run(img, ResolveRip<spec>, source); assert(truncated.state==SigState::Failed); assert(std::strcmp(truncated.why,"RIP source instruction outside .text")==0);
 puts("issue302 shared/scanner PASS");
}
