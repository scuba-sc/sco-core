#include "sco/scan.h"
#include "sco/signatures.h"
#include <cstdio>
#include <cstring>
namespace sco::game { extern const SigDef kPakSignatures[]; }
namespace sco {
uint8_t* loader;
uint8_t* Sig(const char*) { return loader; }

SigResult SigPattern(const Section&, const char*) { return {}; }
}
int main() {
    uint8_t bytes[0x500]{};
    sco::Image img{}; img.base = bytes; img.size = sizeof(bytes); img.text = {bytes, sizeof(bytes)};
    sco::loader = bytes;
    const uint8_t pattern[] = {0x48,0x8b,0x0d,0xff,0x7f,0x00,0x00,0x4c,0x8d,0x05,0,0,0,0,0x48,0x8b,0x55,0,0x45,0x33,0xc9,0x48,0x8b,0x01,0xff,0x90,0x48,0x01,0,0};
    std::memcpy(bytes, pattern, sizeof(pattern));
    auto r = sco::game::kPakSignatures[1].resolve(img);
    if (r.state == sco::SigState::Ok) { std::puts("FAIL: outside CryPak target accepted"); return 1; }
    std::puts("outside CryPak target rejected");
}
