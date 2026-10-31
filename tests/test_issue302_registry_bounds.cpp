#include "sco/signatures.h"
#include "issue302_test_support.h"
#include <cassert>
#include <cstring>
namespace sco { void Log(const char*, ...) {} }
int main() {
    issue302::GuardPages memory;
    auto* p = memory.data+memory.page-3;
    std::memset(p,0x90,3);
    const sco::SigDef row{"truncated", "90 90 90", 3, 7, nullptr, {}};
    assert(sco::RegisterSignatures(&row,1));
    sco::Image img{}; img.base=memory.data; img.size=static_cast<uint32_t>(memory.page); img.text={p,3};
    sco::ResolveAll(img);
    auto* r=sco::SigLookup("truncated");
    assert(r && r->state==sco::SigState::Failed);
}
