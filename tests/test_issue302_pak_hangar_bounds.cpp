#include "sco/scan.h"
#include "sco/signatures.h"
#include <cassert>
#include <cstring>
#include <memory>
#include <cstdio>
namespace sco::game { extern const sco::SigDef kPakSignatures[], kHangarSignatures[]; }
namespace sco {
uint8_t* Sig(const char*) { return nullptr; }

SigResult SigPattern(const Section&, const char*) { return {}; }
}
int main(int argc, char**) {
    using namespace sco;
    if (argc > 1) {
        Image img{};
        img.text = {reinterpret_cast<uint8_t*>(UINTPTR_MAX - 3), 32};
        img.rdata = img.text;
        assert(game::kPakSignatures[0].resolve(img).state == SigState::Failed);
        assert(game::kHangarSignatures[2].resolve(img).state == SigState::Failed);
        return 0;
    }
    constexpr size_t textSize = 96, rdataOff = 128, pdataOff = 224;
    auto bytes = std::make_unique<uint8_t[]>(256);
    Image img{}; img.base = bytes.get(); img.size = 256;
    img.text = {bytes.get(), textSize}; img.rdata = {bytes.get()+rdataOff, 96};
    img.pdata = {bytes.get()+pdataOff, 12};
    uint32_t entry[] = {0, textSize, 248};
    std::memcpy(img.pdata.base, entry, sizeof entry);
    auto run = [&](const char* label, const char* prologue, const SigDef& row, uint8_t reg) {
        std::memset(img.text.base, 0, textSize);
        unsigned v; size_t i=0;
        for (const char* p=prologue; *p; p+= (* (p+2) ? 3 : 2)) {
            assert(std::sscanf(p, "%2x", &v)==1); img.text.base[i++]=static_cast<uint8_t>(v);
        }
        std::memset(img.rdata.base, 0, img.rdata.size);
        std::strcpy(reinterpret_cast<char*>(img.rdata.base), label);
        for (size_t off : {size_t{40}, textSize-7}) {
            auto* p=img.text.base+off; p[0]=0x4c; p[1]=0x8d; p[2]=reg;
            int32_t d=static_cast<int32_t>(rdataOff-off-7); std::memcpy(p+3,&d,4);
        }
        assert(row.resolve(img).state == SigState::Ok);
        img.pdata.size = SIZE_MAX;
        assert(row.resolve(img).state == SigState::Failed);
        img.pdata.size = 12;
        img.text.size = 5;
        assert(row.resolve(img).state != SigState::Ok);
        img.text.size = textSize;
    };
    run("DCB file is smaller than expected", "48 89 5C 24 08 48 89 74 24 10 48 89 7C 24 18 55 41 54 41 55 41 56 41 57", game::kPakSignatures[0],0x0d);
    run("IIM_RequestInstanceImpl_Requesting", "48 89 5C 24 10 48 89 4C 24 08 55 56 57", game::kHangarSignatures[2],0x05);
    std::puts("pak/hangar bounds green");
}
