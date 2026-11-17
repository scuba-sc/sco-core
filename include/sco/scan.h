#pragma once
// Byte scanning over a loaded PE image. Pure C++: no Windows headers, so the same code runs
// inside the game and in host tools that map StarCitizen.exe from disk.
#include <cstddef>
#include <cstdint>
#include <memory>
#include <type_traits>
#include <vector>

namespace sco {

struct Section { uint8_t* base = nullptr; size_t size = 0; };
class ScanIndex;

// The parts of the game image the scanners need. `base` is where the image starts in memory
// (the module handle in game, a buffer laid out at section RVAs in host tools).
struct Image {
    uint8_t* base      = nullptr;
    Section  text, rdata;
    Section  pdata;           // the exception directory: RUNTIME_FUNCTION entries (12 bytes each)
    uint32_t timestamp = 0;   // PE header TimeDateStamp
    uint32_t size      = 0;   // SizeOfImage
    // The resolve pass's scan index (see ScanIndex); null outside a pass. Set by the registry on its
    // own copy of the image, so resolvers reach it through the `img` they already receive.
    ScanIndex* index   = nullptr;
};

// "48 8B ?? 05" style patterns: two hex digits per byte, `?` or `??` for any byte.
const uint8_t* FindCString(const Section& s, const char* str);          // NUL-bounded match
int32_t        Rel32(const uint8_t* p);
bool           BytesMatch(const uint8_t* p, const char* pattern);
uint8_t*       FindRipLea(const Section& text, uint8_t reg0, uint8_t reg1, uint8_t reg2, const uint8_t* target);
// Every match, up to `max` written to `out`; returns the total count. A pattern longer than
// kMaxPatternBytes matches nothing.
constexpr size_t kMaxPatternBytes = 96;
int            FindPattern(const Section& text, const char* pattern, uint8_t** out, int max);
uint8_t*       FindUniquePattern(const Section& text, const char* pattern, int& matches);

// The start of the function containing `at`, from the image's .pdata, as RtlLookupFunctionEntry
// would find it: the RUNTIME_FUNCTION whose [begin, end) holds at, then its chained unwind info
// (UNW_FLAG_CHAININFO) followed, up to 8 links, to the primary entry. nullptr when at isn't
// inside the image, no entry holds it, or an entry or its unwind info points outside the image.
uint8_t*       FunctionStart(const Image& img, const uint8_t* at);

// Target of a RIP-relative operand: the instruction at `insn`, displacement at `insn + dispOffset`,
// instruction length `insnSize`.
inline uint8_t* RipTarget(const uint8_t* insn, size_t dispOffset, size_t insnSize) {
    return const_cast<uint8_t*>(insn) + insnSize + Rel32(insn + dispOffset);
}

// Which RIP-relative instruction a scan looks for, as a position-by-position test on the seven bytes
// at p: (p[0] & mask0) == b0, p[1] == b1, (p[2] & mask2) == b2; the disp32 sits at p + 3 and the
// target is p + 7 + disp32. A match may start inside another instruction: scans work on byte
// positions, not on disassembly.
struct RipForm {
    uint8_t b0, mask0, b1, b2, mask2;
    static constexpr RipForm Exact(uint8_t r0, uint8_t r1, uint8_t r2) { return {r0, 0xFF, r1, r2, 0xFF}; }
    static constexpr RipForm AnyLea() { return {0x48, 0xFB, 0x8D, 0x05, 0xC7}; }   // lea r64, [rip+disp32]: REX 48/4C, any register
};

// The result of a site scan that every row of a templated family repeats identically.
struct SiteMemo {
    bool     done = false;
    int      n    = 0;
    bool     flag = false;
    uint8_t* at[32] = {};
};
enum class MemoKey { SocialGroup, Handshake, OfflineDb, kCount };

// A per-resolve-pass scan cache, owned by the registry for the length of one Resolve and reached
// through Image::index. One writer thread; the image is read-only for the pass, so nothing here
// is invalidated (a different image or .text rebuilds it).
//
// Rip-relative sites: the first RipForm::AnyLea()-shaped scan indexes every byte position in .text
// holding `lea r64, [rip+disp32]` (REX 48/4C, 8D, ModRM mod 00 rm 101) with one pass, sorted by
// target and then position. Later scans for any RipForm that lea shape implies (an exact REX/
// ModRM byte is just a filter) look their target up instead of walking .text again, and see the
// same positions in the same ascending order a byte-by-byte scan sees. Any other form, a .text
// beyond 4 GiB, a range that wraps the address space or a failed allocation scans directly.
// The index is not charged to a registry's byte budget.
class ScanIndex {
public:
    static constexpr size_t kNone = ~size_t{0};
    struct Site { uint32_t off; int32_t disp; };   // instruction offset in .text, displacement

    // maxText 0 never builds the index: every scan walks .text (one-off callers outside a pass).
    explicit ScanIndex(size_t maxText = 0xFFFFFFFFu) noexcept : maxText_(maxText) {}
    ScanIndex(const ScanIndex&) = delete;
    ScanIndex& operator=(const ScanIndex&) = delete;

    // visit(offset), ascending, for every position in [first, last] (last is clamped to the final
    // seven-byte instruction) matching `form` whose p + 7 + disp32 is `target`; stop when it
    // returns false.
    template <class Visit>
    void ForEachRip(const Section& text, const RipForm& form, const uint8_t* target,
                    size_t first, size_t last, Visit&& visit) {
        using V = std::remove_reference_t<Visit>;
        ForEachRipImpl(text, form, target, first, last,
                       [](void* c, size_t off) { return (*static_cast<V*>(c))(off); },
                       const_cast<void*>(static_cast<const void*>(&visit)));
    }
    // The first such position at or after `first`, or kNone.
    size_t FindRip(const Section& text, const RipForm& form, const uint8_t* target, size_t first) {
        size_t hit = kNone;
        ForEachRip(text, form, target, first, ~size_t{0}, [&](size_t off) { hit = off; return false; });
        return hit;
    }
    // The memo slot for `key`, reset when `img` isn't the image it was filled for.
    SiteMemo& Memo(const Image& img, MemoKey key);

private:
    bool Build(const Section& text);
    void ForEachRipImpl(const Section& text, const RipForm& form, const uint8_t* target,
                        size_t first, size_t last, bool (*visit)(void*, size_t), void* ctx);

    size_t maxText_;
    const uint8_t* textBase_ = nullptr;
    size_t textSize_ = 0;
    bool built_ = false, usable_ = false;
    // One nothrow allocation made by Build. Not a std::vector: checked-iterator builds allocate in
    // vector's noexcept constructor, so a failed allocation there would terminate instead of
    // falling back to walking .text.
    std::unique_ptr<Site[]> sites_;
    size_t siteCount_ = 0;
    Image memoImage_{};
    bool memoBound_ = false;
    SiteMemo memo_[static_cast<size_t>(MemoKey::kCount)];
};

// The pass's index, or outside a pass one that scans directly.
class ScanIndexRef {
public:
    explicit ScanIndexRef(const Image& img) noexcept : p_(img.index ? img.index : &direct_) {}
    ScanIndex* operator->() const noexcept { return p_; }
private:
    ScanIndex direct_{0};
    ScanIndex* p_;
};

// The family memo for `key` in the pass, or `local` (recomputed per row) outside a pass.
inline SiteMemo& PassMemo(const Image& img, MemoKey key, SiteMemo& local) {
    return img.index ? img.index->Memo(img, key) : local;
}

#ifdef _WIN32
Image ModuleImage();   // the process's main executable
#endif

}  // namespace sco
