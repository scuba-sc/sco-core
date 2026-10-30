#include "sco/scan.h"
#include "sco/signatures.h"
#include <cstdio>
#include <cstring>
#include <memory>

namespace sco::game {
extern const SigDef kMiningSignatures[];
}
namespace sco {
uint8_t* Sig(const char*) { return nullptr; }

SigResult SigPattern(const Section&, const char*) { return {}; }
}
int main() {
    auto text = std::make_unique<uint8_t[]>(1);
    text[0] = 0x4c;
    char label[] = "CBiomeBuilder::BuildLargeScaleEcoSystem";
    sco::Image img{};
    img.text = {nullptr, 0};
    img.rdata = {reinterpret_cast<uint8_t*>(label), sizeof(label)};
    const auto result = sco::game::kMiningSignatures[0].resolve(img);
    if (result.state == sco::SigState::Ok) return 1;
    std::puts("short mining text rejected");
}
