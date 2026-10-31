#include "../src/game/entities_sigs.cpp"
#include <cassert>
#include <cstdlib>
#include <cstring>
static uint8_t* source;
namespace sco { uint8_t* Sig(const char*) { return source; } }
namespace sco {
SigResult SigPattern(const Section& text, const char* pattern) {
    int n = 0; auto p = FindUniquePattern(text, pattern, n);
    return n == 1 ? SigOk(p) : SigResult{n ? SigState::Ambiguous : SigState::Missing, nullptr, n, nullptr};
}
}
int main() {
    source = static_cast<uint8_t*>(std::malloc(1));
    *source = 0xe8;
    sco::Image img{}; img.base = source; img.size = 1; img.text = {source, 1};
    assert(sco::game::ResolveClassSite(img).state == sco::SigState::Failed);
    assert(sco::game::ResolveCall<sco::game::kForEach>(img).state == sco::SigState::Failed);
    sco::game::Check check{100, "E8", "short"}; const char* why = nullptr;
    assert(!sco::game::ChecksMatch(img, source, &check, 1, &why));
    assert(std::strcmp(why, "function check outside .text") == 0);
    check = {SIZE_MAX, "E8", "changed"};
    assert(!sco::game::ChecksMatch(img, source, &check, 1, &why));
    check = {0, "90", "changed"};
    assert(!sco::game::ChecksMatch(img, source, &check, 1, &why));
    assert(std::strcmp(why, "changed") == 0);
    std::free(source);
    uint8_t bytes[8] = {0, 0x48, 0x8d, 0x05, 0, 0, 0, 0};
    const uint8_t* refs[1]{};
    assert(sco::game::RipRefs({bytes, sizeof(bytes)}, 0x48, 0x8d, 0x05, bytes + 8, refs, 1) == 1);
    assert(refs[0] == bytes + 1);
    assert(sco::game::RipRefs({reinterpret_cast<uint8_t*>(UINTPTR_MAX - 3), 8}, 0x48, 0x8d, 0x05, bytes, refs, 1) == 0);
    uint8_t storage[512]{};
    img = {}; img.base = storage; img.size = sizeof(storage); img.text = {storage, 128};
    source = storage;
    storage[0x46] = 0xe8;
    int32_t delta = 100 - (0x46 + 5); std::memcpy(storage + 0x47, &delta, 4);
    auto result = sco::game::ResolveCall<sco::game::kForEach>(img);
    assert(result.state == sco::SigState::Ok && result.at == storage + 100);
    img.text.size = 0x4a;
    result = sco::game::ResolveCall<sco::game::kForEach>(img);
    assert(result.state == sco::SigState::Failed && std::strcmp(result.why, "call instruction outside .text") == 0);
    img.text.size = 128; delta = 140 - (0x46 + 5); std::memcpy(storage + 0x47, &delta, 4);
    result = sco::game::ResolveCall<sco::game::kForEach>(img);
    assert(result.state == sco::SigState::Failed && std::strcmp(result.why, "call target outside .text") == 0);
    img.rdata = {reinterpret_cast<uint8_t*>(UINTPTR_MAX - 1), 8};
    result = sco::game::LabelFunction(img, "label", 0x48, 0x8d, 0x05, 0);
    assert(result.state == sco::SigState::Failed && std::strcmp(result.why, "invalid .rdata") == 0);
    source = nullptr;
    assert(std::strcmp(sco::game::ResolveClassSite(img).why, "source row missing") == 0);
}
