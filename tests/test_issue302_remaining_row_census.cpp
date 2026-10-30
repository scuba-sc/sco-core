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
                    auto result=row.resolve ? row.resolve(img):sco::SigPattern(img.text,row.pattern);
                    assert(result.state!=sco::SigState::Ok);
                }
            }
        }
        std::printf("%s: %zu rows\n",table.name,table.count);
    }
    std::printf("TOTAL: %zu rows; all short-section/dependency probes passed\n",total);
}
