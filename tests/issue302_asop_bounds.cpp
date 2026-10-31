#include <cstdio>
#include <cstring>
#include "../src/game/asop_sigs.cpp"

namespace sco {
static uint8_t* dependency;
uint8_t* Sig(const char*) { return dependency; }
SigResult SigPattern(const Section&, const char*) { return {}; }
}

int main(int argc, char** argv) {
    const char* mode = argc > 1 ? argv[1] : "jump";
    uint8_t bytes[0xe00]{};
    sco::Image img{}; img.base = bytes; img.size = sizeof(bytes); img.text = {bytes, sizeof(bytes)};
    if (!std::strcmp(mode, "jump")) {
        int32_t displacement = sco::game::asop::kOpenExit - sco::game::asop::kOpenClientHalf;
        std::memcpy(bytes + sco::game::asop::kOpenPatchSite + 1, &displacement, 4);
        if (sco::game::OpenExtra(img, bytes)) { std::puts("FAIL: valid jump rejected"); return 1; }
        img.text.size = sco::game::asop::kOpenPatchSite + 4;
        if (!sco::game::OpenExtra(img, bytes)) { std::puts("FAIL: truncated jump accepted"); return 1; }
    } else if (!std::strcmp(mode, "gate")) {
        sco::dependency = bytes;
        if (sco::game::GateOpenExtra(img, bytes + 0x21)) { std::puts("FAIL: valid gate rejected"); return 1; }
        sco::dependency = nullptr;
        if (!sco::game::GateOpenExtra(img, reinterpret_cast<const uint8_t*>(0x21))) { std::puts("FAIL: missing dependency accepted"); return 1; }
    } else if (!std::strcmp(mode, "inventory")) {
        sco::dependency = bytes;
        if (sco::game::ResolveInventoryKind(img).at != bytes + 0x109) { std::puts("FAIL: valid immediate rejected"); return 1; }
        img.text.size = 0x109;
        if (sco::game::ResolveInventoryKind(img).state == sco::SigState::Ok) { std::puts("FAIL: out-of-text immediate accepted"); return 1; }
    } else return 2;
    std::puts("rejected");
}
