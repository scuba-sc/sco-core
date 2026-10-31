#include "sco/scan.h"
#include "sco/signatures.h"
#include <cstdio>
#include <cstring>
namespace sco::game { extern const SigDef kMiningSignatures[]; }
namespace sco {
uint8_t* Sig(const char*) { return nullptr; }

SigResult SigPattern(const Section&, const char*) { return {}; }
}
int main() {
    uint8_t bytes[128]{};
    sco::Image img{}; img.base=bytes; img.size=sizeof(bytes);
    img.rdata={bytes, 64}; img.text={bytes+64, 7};
    std::memcpy(bytes, "CBiomeBuilder::BuildLargeScaleEcoSystem", 39);
    bytes[64]=0x4c; bytes[65]=0x8d; bytes[66]=0x05;
    int32_t disp = static_cast<int32_t>(bytes-(bytes+71));
    std::memcpy(bytes+67, &disp, sizeof(disp));
    auto r = sco::game::kMiningSignatures[0].resolve(img);
    if (r.state == sco::SigState::Missing) { std::puts("FAIL: complete final LEA was skipped"); return 1; }
    std::puts("complete final LEA was inspected");
}
