// Portable resolver tests. Only FunctionStart is a synthetic seam; scans and byte checks are real.
#include "sco/scan.h"
#include "sco/signatures.h"
#include <cassert>
#include <cstdio>
#include <cstring>
#include <memory>
#include <string>
namespace { uint8_t* functionStart; }
namespace sco {
uint8_t* Issue302FunctionStart(const Image&, const uint8_t*) { return functionStart; }
uint8_t* Sig(const char*) { return nullptr; }

SigResult SigPattern(const Section&, const char*) { return {}; }
}
#define FunctionStart Issue302FunctionStart
#include "../src/game/gamerules_sigs.cpp"
#include "../src/game/mining_sigs.cpp"
#undef FunctionStart

struct Fixture {
    std::unique_ptr<uint8_t[]> bytes{new uint8_t[4096]{}};
    sco::Image img{};
    Fixture(bool mining) {
        img.base=bytes.get(); img.size=4096;
        img.rdata={bytes.get(),128}; img.text={bytes.get()+256,3000};
        img.pdata={bytes.get()+128,12};
        std::strcpy(reinterpret_cast<char*>(bytes.get()), mining ? "CBiomeBuilder::BuildLargeScaleEcoSystem" : "IGameRulesParamsModule");
        functionStart=img.text.base;
    }
    void lea(size_t off, bool mining) {
        auto* p=img.text.base+off;
        p[0]=mining?0x4c:0x48; p[1]=0x8d; p[2]=0x05;
        int32_t disp=static_cast<int32_t>(img.rdata.base-(p+7));
        std::memcpy(p+3,&disp,4);
    }
    sco::SigResult resolve(bool mining) {
        return mining ? sco::game::kMiningSignatures[0].resolve(img) : sco::game::kGameRulesSignatures[1].resolve(img);
    }
};
static void fillPattern(uint8_t* p, const char* pattern) {
    for (; *pattern;) {
        while (*pattern==' ') ++pattern;
        if (!*pattern) break;
        unsigned value=0;
        if (*pattern!='?') { char token[3]={pattern[0],pattern[1],0}; std::sscanf(token,"%x",&value); }
        *p++=static_cast<uint8_t>(value);
        while (*pattern && *pattern!=' ') ++pattern;
    }
}
int main(int argc, char** argv) {
    const std::string mode=argc>1?argv[1]:"all";
    if (mode=="wrapped-rdata") {
        for (bool mining : {false,true}) {
            Fixture f(mining);
            f.img.rdata={reinterpret_cast<uint8_t*>(UINTPTR_MAX-10),64};
            assert(f.resolve(mining).state!=sco::SigState::Ok);
            if (!mining) assert(sco::game::kGameRulesSignatures[0].resolve(f.img).state!=sco::SigState::Ok);
        }
    } else if (mode=="wrapped-function") {
        Fixture f(true); f.lea(0x9b,true);
        functionStart=reinterpret_cast<uint8_t*>(UINTPTR_MAX-100);
        assert(f.resolve(true).state!=sco::SigState::Ok);
    } else if (mode=="physical-tail") {
        for (bool mining : {false,true}) {
            Fixture f(mining);
            if (mining) {
                for (const auto& c:sco::game::kCellChecks) fillPattern(f.img.text.base+c.off,c.bytes);
                f.lea(0x9b,true);
            } else {
                f.lea(16,false);
            }
            const size_t textSize=mining?0x92f:23;
            const size_t size=256+textSize;
            auto bounded=std::make_unique<uint8_t[]>(size);
            std::memcpy(bounded.get(),f.bytes.get(),size);
            f.img.base=bounded.get(); f.img.size=static_cast<uint32_t>(size);
            f.img.rdata={bounded.get(),128}; f.img.text={bounded.get()+256,textSize};
            f.img.pdata={bounded.get()+128,12};
            functionStart=f.img.text.base+(mining?0:22);
            assert(f.resolve(mining).state!=sco::SigState::Ok);
        }
    } else {
        for (bool mining : {false,true}) {
            Fixture f(mining);
            for (size_t n=0;n<7;++n) { f.img.text.size=n; assert(f.resolve(mining).state!=sco::SigState::Ok); }
            f.img.text.size=3000;
            f.lea(0,mining);
            for (int32_t disp : {INT32_MIN,INT32_MAX}) {
                std::memcpy(f.img.text.base+3,&disp,4);
                assert(f.resolve(mining).state!=sco::SigState::Ok);
            }
            std::memset(f.img.text.base,0,7);
            if (mining) {
                for (const auto& c:sco::game::kCellChecks) fillPattern(f.img.text.base+c.off,c.bytes);
                f.lea(0x9b,true);
                assert(f.resolve(true).state==sco::SigState::Ok);
                for (const auto& c:sco::game::kCellChecks) {
                    auto& first=f.img.text.base[c.off];
                    first^=0xff;
                    assert(f.resolve(true).state!=sco::SigState::Ok);
                    first^=0xff;
                }
                // Every check's full extent is required, including wildcard tails.
                for (const auto& c:sco::game::kCellChecks) {
                    const auto full=c.off+sco::game::rows::PatternBytes(c.bytes);
                    f.img.text.size=full-1;
                    assert(f.resolve(true).state!=sco::SigState::Ok);
                }
                f.img.text.size=0x930;
                assert(f.resolve(true).state==sco::SigState::Ok);
                f.lea(0x200,true);
                assert(f.resolve(true).state==sco::SigState::Ambiguous);
            } else {
                fillPattern(f.img.text.base,sco::game::kSyncPrologue);
                f.lea(16,false); f.img.text.size=23;
                assert(f.resolve(false).state==sco::SigState::Ok);
                f.img.text.size=22; assert(f.resolve(false).state!=sco::SigState::Ok);
                f.img.text.size=3000;
                functionStart=f.img.text.base+2999;
                assert(f.resolve(false).state!=sco::SigState::Ok);
                functionStart=f.img.text.base;
                assert(sco::game::kGameRulesSignatures[0].resolve(f.img).state==sco::SigState::Ok);
                std::strcpy(reinterpret_cast<char*>(f.img.rdata.base+32),"IGameRulesParamsModule");
                assert(sco::game::kGameRulesSignatures[0].resolve(f.img).state!=sco::SigState::Ok);
            }
            f.img.text={reinterpret_cast<uint8_t*>(UINTPTR_MAX-3),16};
            assert(f.resolve(mining).state!=sco::SigState::Ok);
        }
    }
    std::printf("PASS %s\n",mode.c_str());
}
