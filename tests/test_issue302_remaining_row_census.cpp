#include "sco/signature_registry.h"
#include "sco/scan.h"
#include "sco/signatures.h"
#include <cassert>
#include <cstdio>
#include <memory>
namespace sco {
uint8_t* dependency;
uint8_t* Sig(const char*) { return dependency; }

SigResult SigPattern(const Section& s, const char* p) {
    uint8_t* at=nullptr; int n=FindPattern(s,p,&at,1);
    return n==1 ? SigOk(at) : SigResult{SigState::Missing,nullptr,n,nullptr};
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
#define TABLES(X) X(Offline) X(GameRules) X(Mining) X(Entities) X(Events) X(System) X(Pak) X(Hangar) X(Asop) X(Atc)
namespace sco::game {
#define DECLARE(n) extern const SigDef k##n##Signatures[]; extern const size_t k##n##SignatureCount;
TABLES(DECLARE)
#undef DECLARE
}
int main() {
    struct Table { const char* name; const sco::SigDef* rows; size_t count; };
#define ENTRY(n) {#n,sco::game::k##n##Signatures,sco::game::k##n##SignatureCount},
    Table tables[]={TABLES(ENTRY)};
#undef ENTRY
    size_t total=0;
    for (const auto& table:tables) {
        total+=table.count;
        for (size_t n=0;n<=64;++n) {
            auto text=std::make_unique<uint8_t[]>(n ? n:1);
            auto rdata=std::make_unique<uint8_t[]>(n ? n:1);
            auto metadata=std::make_unique<uint8_t[]>(16);
            sco::Image img{}; img.base=metadata.get(); img.size=16;
            img.text={text.get(),n}; img.rdata={rdata.get(),n}; img.pdata={metadata.get(),12};
            for (uint8_t* dependency : {static_cast<uint8_t*>(nullptr),reinterpret_cast<uint8_t*>(UINTPTR_MAX-4)}) {
                sco::dependency=dependency;
                for (size_t i=0;i<table.count;++i) {
                    const auto& row=table.rows[i];
                    auto result=row.resolve ? issue302_context::Run(img, row.resolve, sco::dependency):sco::SigPattern(img.text,row.pattern);
                    assert(result.state!=sco::SigState::Ok);
                }
            }
        }
        std::printf("%s: %zu rows\n",table.name,table.count);
    }
    std::printf("TOTAL: %zu rows; all short-section/dependency probes passed\n",total);
}
