// Standalone resolver regression: link only src/sco_scan.cpp, never the shared build.
#include "../src/game/contracts_sigs.cpp"
#include <cassert>
#include <cstdio>
#include <memory>
namespace sco {
uint8_t* test_source = nullptr;
uint8_t* Sig(const char*) { return test_source; }
SigResult SigPattern(const Section& text, const char* pattern) {
    int n = 0;
    auto* p = FindUniquePattern(text, pattern, n);
    return n == 1 ? SigOk(p) : SigResult{n ? SigState::Ambiguous : SigState::Missing, nullptr, n, nullptr};
}
}
namespace {
int cases = 0;
void Pass(const char* name) { ++cases; std::printf("PASS %02d %s\n", cases, name); }
void Rip(uint8_t* p, const uint8_t* target, size_t disp = 3, size_t len = 7) {
    const int32_t d = static_cast<int32_t>(target - (p + len));
    std::memcpy(p + disp, &d, sizeof d);
}
constexpr sco::game::VtblFnSpec slotSpec{"test", 0, "C3"};
constexpr sco::game::VtblFnSpec hugeSlotSpec{"test", SIZE_MAX, "C3"};
constexpr sco::game::CallSpec callSpec{"test", SIZE_MAX, "C3"};
constexpr sco::game::InRangeSpec rangeSpec{"test", 8, "E8 ?? ?? ?? ??", nullptr};
}
void TestAddPlayer(bool invalidFirst) {
    using namespace sco;
    using namespace sco::game;
    uint8_t b[4096]{};
    Image img{}; img.base = b; img.size = 2048;
    img.text = {b, 1024}; img.rdata = {b + 1024, 1024};
    test_source = b + 700;
    auto build = [&](const uint8_t* first, const uint8_t* second) {
        std::memset(b, 0, sizeof b);
        for (int i = 0; i < 2; ++i) {
            auto* p = b + 64 + i * 128;
            p[0] = 0xE8; Rip(p, test_source, 1, 5);
            const uint8_t lea[] = {0x48, 0x8D, 0x90, 0x80, 0, 0, 0};
            std::memcpy(p + 5, lea, sizeof lea);
            p[12] = 0xE8; Rip(p + 12, i == 0 ? first : second, 1, 5);
        }
    };
    build(b + 800, b + 800);
    auto r = ResolveAddPlayer(img);
    assert(r.state == SigState::Ok && r.at == b + 800);
    for (const auto off : {3000, 1500}) {
        const auto* invalid = b + off;
        build(invalidFirst ? invalid : b + 800, invalidFirst ? b + 800 : invalid);
        const auto rejected = ResolveAddPlayer(img);
        assert(rejected.state != SigState::Ok);
        assert(rejected.why && !std::strcmp(rejected.why, "AddPlayer target outside .text"));
    }
    build(b + 800, b + 900);
    assert(ResolveAddPlayer(img).state != SigState::Ok);
    Pass(invalidFirst ? "AddPlayer invalid then valid refuses out-of-image/non-text targets" : "AddPlayer valid then invalid refuses out-of-image/non-text targets");
}
int main(int argc, char**) {
    using namespace sco;
    using namespace sco::game;
    if (argc > 1) { TestAddPlayer(argc == 2); return 0; }
    TestAddPlayer(true);
    TestAddPlayer(false);
    auto bytes = std::make_unique<uint8_t[]>(512);
    auto* b = bytes.get();
    Image img{};
    img.base = b; img.size = 512;
    img.text = {b + 64, 128}; img.rdata = {b + 256, 64};
    auto* p = b + 64;
    auto* slot = b + 256;
    p[0] = 0xC3;
    uint64_t v = reinterpret_cast<uintptr_t>(p);
    std::memcpy(slot, &v, 8);
    assert(SlotTarget(img, slot) == p);
    Pass("loaded slot positive");
    assert(SlotTarget(img, p) == nullptr);
    test_source = p;
    assert(ResolveVtblFn<slotSpec>(img).state != SigState::Ok);
    Pass("slot inside image but outside rdata refused");
    test_source = slot;
    assert(ResolveVtblFn<slotSpec>(img).at == p);
    assert(ResolveVtblFn<hugeSlotSpec>(img).state != SigState::Ok);
    Pass("vtable positive and overflowing slot offset");
    img.rdata.size = 7;
    assert(SlotTarget(img, slot) == nullptr);
    Pass("declared rdata partial pointer inside full image refused");
    img.rdata.size = 64;
    auto shortslot = std::make_unique<uint8_t[]>(7);
    Image shortimg{}; shortimg.base = shortslot.get(); shortimg.size = 7;
    shortimg.rdata = {shortslot.get(), 7};
    assert(SlotTarget(shortimg, shortslot.get()) == nullptr);
    assert(HeaderBase(shortimg) == 0);
    Pass("physical seven-byte allocation pointer/header refused");
    auto shorttext = std::make_unique<uint8_t[]>(6);
    shortimg.base = shorttext.get(); shortimg.size = 6;
    shortimg.text = {shorttext.get(), 6};
    shorttext[0] = 0x48; shorttext[1] = 0x8D; shorttext[2] = 0x05;
    assert(LeaTo(shortimg, shorttext.get(), nullptr) == nullptr);
    assert(CheckedRip(shortimg, shorttext.get(), 3, 7) == nullptr);
    assert(ShardStoreExtra(shortimg, shorttext.get()) != nullptr);
    Pass("physical truncated LEA, displacement and extra call refused");
    auto exact = std::make_unique<uint8_t[]>(7);
    Image exactimg{}; exactimg.base = exact.get(); exactimg.size = 7;
    exactimg.text = {exact.get(), 7};
    exact[0] = 0x4C; exact[1] = 0x8D; exact[2] = 0x05;
    Rip(exact.get(), exact.get());
    assert(LeaTo(exactimg, exact.get(), nullptr) == exact.get());
    assert(ExactLeaTo(exactimg, exact.get()) == exact.get());
    Pass("exact seven-byte section final complete LEA accepted");
    p[0] = 0x48; p[1] = 0x8D; p[2] = 0x05;
    Rip(p, slot);
    assert(LeaTo(img, slot, nullptr) == p);
    assert(CheckedRip(img, p, 3, 7) == slot);
    Pass("LEA/RIP image target positive");
    Rip(p, b + 512);
    assert(CheckedRip(img, p, 3, 7) == nullptr);
    assert(LeaTo(img, b + 512, nullptr) == nullptr);
    Pass("one-past-image derived target refused");
    int32_t extreme = INT32_MIN;
    std::memcpy(p + 3, &extreme, 4);
    assert(CheckedRip(img, p, 3, 7) == nullptr);
    extreme = INT32_MAX; std::memcpy(p + 3, &extreme, 4);
    assert(CheckedRip(img, p, 3, 7) == nullptr);
    assert(CheckedRip(img, p, SIZE_MAX, 7) == nullptr);
    Pass("extreme rel32 and invalid displacement refused");
    Image noimage = img; noimage.base = nullptr; noimage.size = 0;
    const uint8_t* at = nullptr;
    assert(SlotTarget(noimage, slot) == nullptr);
    assert(!Holds(noimage, v, p));
    assert(SlotsHolding(noimage, p, 0, &at) == 0);
    assert(LeaTo(noimage, slot, nullptr) == nullptr);
    test_source = p;
    assert(ResolveMissionSystemSite(noimage).state != SigState::Ok);
    assert(ResolveMissionSystem(noimage).state != SigState::Ok);
    assert(ResolveAddPlayer(noimage).state != SigState::Ok);
    assert(ResolveStrFn<kQueryReply>(noimage).state != SigState::Ok);
    assert(ResolveFnOf<kFindEntry>(noimage).state != SigState::Ok);
    assert(ResolveInRange<rangeSpec>(noimage).state != SigState::Ok);
    assert(ResolveMissionSettings(noimage).state != SigState::Ok);
    Pass("missing image fails closed across custom resolver families");
    Image wrap{}; wrap.base = reinterpret_cast<uint8_t*>(UINTPTR_MAX - 8); wrap.size = 64;
    wrap.text = {wrap.base, 64}; wrap.rdata = wrap.text;
    assert(!Mapped(wrap, wrap.text)); assert(HeaderBase(wrap) == 0);
    assert(LeaTo(wrap, wrap.base, nullptr) == nullptr);
    Pass("overflowing image/section declaration refused before read");
    test_source = reinterpret_cast<uint8_t*>(UINTPTR_MAX - 3);
    assert(ResolveCall<callSpec>(img).state != SigState::Ok);
    assert(ResolveMissionSystem(img).state != SigState::Ok);
    assert(ShardStoreExtra(img, test_source) != nullptr);
    assert(TextOffset(img, p, SIZE_MAX, 1) == nullptr);
    Pass("forged source and overflowing source offsets refused");
    p[0] = 0xE8; Rip(p, p + 40, 1, 5); p[40] = 0xC3;
    assert(CallAt(img, p) == p + 40);
    img.text.size = 4;
    assert(CallAt(img, p) == nullptr);
    Pass("call full extent required with positive control");
    img.text.size = 128;
    Rip(p, b + 256, 1, 5);
    assert(CallAt(img, p) == nullptr);
    Pass("call derived image target outside text refused");
    test_source = p + 120;
    assert(ResolveInRange<rangeSpec>(img).state != SigState::Ok);
    Pass("range match requires full pattern suffix extent");
    // PE32+ preferred-address slot translation.
    b[0x3C] = 0xC0; std::memcpy(b + 0xC0, "PE\0\0", 4);
    const uint16_t magic = 0x20B; std::memcpy(b + 0xD8, &magic, 2);
    uint64_t preferred = 0x140000000ULL; std::memcpy(b + 0xF0, &preferred, 8);
    v = preferred + 64; std::memcpy(slot, &v, 8);
    assert(HeaderBase(img) == preferred);
    assert(SlotTarget(img, slot) == p);
    assert(Holds(img, v, p));
    assert(SlotsHolding(img, p, 0, &at) == 1 && at == slot);
    Pass("preferred PE32+ slot translation and slot census positive");
    img.size = 0xF7;
    assert(HeaderBase(img) == 0);
    assert(SlotTarget(img, slot) == nullptr);
    Pass("PE preferred-base tail truncated and slot outside image refused");
    std::printf("contracts bounds: %d cases passed\n", cases);
}
