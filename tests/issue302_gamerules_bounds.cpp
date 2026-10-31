#include "sco/scan.h"
#include "sco/signatures.h"
#include <cstdio>
#include <cstring>
namespace sco::game { extern const SigDef kGameRulesSignatures[]; }
namespace sco { uint8_t* Sig(const char*) { return nullptr; }  SigResult SigPattern(const Section&, const char*) { return {}; } }
int main() {
    uint8_t bytes[256]{};
    sco::Image img{}; img.base=bytes; img.size=sizeof(bytes);
    img.text={bytes+128, 16}; img.rdata={bytes+32, 24}; img.pdata={bytes+64,12};
    std::memcpy(bytes+32,"IGameRulesParamsModule",23);
    uint32_t pdata[] = {128,143,80}; std::memcpy(bytes+64,pdata,sizeof(pdata));
    bytes[80]=0x20; uint32_t parent[] = {143,150,100}; std::memcpy(bytes+84,parent,sizeof(parent));
    bytes[136]=0x48; bytes[137]=0x8d; bytes[138]=0x05;
    int32_t disp = static_cast<int32_t>((bytes+32)-(bytes+143)); std::memcpy(bytes+139,&disp,4);
    const uint8_t prologue[] = {0x40,0x55,0x53,0x57,0x41,0x54,0x41,0x55,0x41,0x56,0x41,0x57,0x48,0x8b,0xec,0x48};
    std::memcpy(bytes+143,prologue,sizeof(prologue));
    auto result=sco::game::kGameRulesSignatures[1].resolve(img);
    if (result.state==sco::SigState::Ok) { std::puts("FAIL: truncated prologue accepted"); return 1; }
    std::puts("truncated prologue rejected");
}
