#include "sco/scan.h"
#include "sco/signatures.h"
#include <cstdio>
#include <cstring>
namespace sco::game { extern const SigDef kGameRulesSignatures[]; }
namespace sco {
uint8_t* Sig(const char*) { return nullptr; }

SigResult SigPattern(const Section&, const char*) { return {}; }
}
int main() {
    uint8_t bytes[256]{};
    sco::Image img{}; img.base=bytes; img.size=sizeof(bytes);
    img.rdata={bytes+16,24}; img.pdata={bytes+48,12}; img.text={bytes+128,23};
    std::memcpy(bytes+16,"IGameRulesParamsModule",23);
    uint32_t pdata[] = {128,151,80}; std::memcpy(bytes+48,pdata,sizeof(pdata));
    const uint8_t prologue[] = {0x40,0x55,0x53,0x57,0x41,0x54,0x41,0x55,0x41,0x56,0x41,0x57,0x48,0x8b,0xec,0x48};
    std::memcpy(bytes+128,prologue,sizeof(prologue));
    bytes[144]=0x48; bytes[145]=0x8d; bytes[146]=0x05;
    int32_t disp=static_cast<int32_t>((bytes+16)-(bytes+151)); std::memcpy(bytes+147,&disp,4);
    auto r=sco::game::kGameRulesSignatures[1].resolve(img);
    if (r.state!=sco::SigState::Ok) { std::puts("FAIL: final complete label LEA skipped"); return 1; }
    std::puts("final complete label LEA resolves");
}
