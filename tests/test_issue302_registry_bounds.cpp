#include "sco/signature_registry.h"
#include "issue302_test_support.h"
#include <cassert>
#include <cstring>
namespace sco { void Log(const char*, ...) {} }
int main() {
    issue302::GuardPages memory;
    auto* p = memory.data+memory.page-3;
    std::memset(p,0x90,3);
    const sco::SigDef row{"truncated", "90 90 90", 3, 7, nullptr, {}};
    sco::SignatureRegistry registry;
    const sco::SignatureTable table{&row, 1};
    assert(registry.RegisterTables(&table, 1).status == sco::SigRegistryStatus::Ok);
    assert(registry.Freeze() == sco::SigRegistryStatus::Ok);
    sco::Image img{}; img.base=memory.data; img.size=static_cast<uint32_t>(memory.page); img.text={p,3};
    assert(registry.Resolve(img) == sco::SigRegistryStatus::Ok);
    auto snapshot = registry.Snapshot();
    auto* r=snapshot->Lookup("truncated");
    assert(r && r->state==sco::SigState::Failed);
}
