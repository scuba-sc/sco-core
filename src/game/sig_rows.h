#pragma once
// Table-driven resolvers for rows that are "a unique pattern plus byte checks at offsets from
// the match" or "a RIP-relative operand inside another row". Private to src/game/: each table
// declares its specs as constexpr objects and instantiates ResolveFn / ResolveRip with them,
// so every row still gets its own SigResolver and its own static failure reasons.
#include "sco/scan.h"
#include "sco/signatures.h"
#include <cstddef>
#include <cstdint>

namespace sco::game::rows {

struct Check {
    size_t      off;     // from the match
    const char* bytes;   // BytesMatch pattern
    const char* why;     // static reason when it doesn't match
};

// Extra validation after the checks pass: nullptr = OK, else a static reason. May call Sig() on
// the row's needs.
using Extra = const char* (*)(const Image& img, const uint8_t* at);

struct FnSpec {
    const char*  pattern;   // unique in .text
    const Check* checks;
    size_t       nChecks;
    Extra        extra;
};

struct RipSpec {
    const char* from;      // the row holding the instruction (must be in the row's needs)
    size_t      off;       // instruction offset from that row's address
    size_t      dispOff;   // displacement offset within the instruction
    size_t      size;      // instruction length
};

inline bool InRange(const Section& range, const uint8_t* p, size_t n) {
    const auto b = reinterpret_cast<uintptr_t>(range.base);
    const auto a = reinterpret_cast<uintptr_t>(p);
    return b && a >= b && range.size <= UINTPTR_MAX - b &&
        a - b <= range.size && n <= range.size - (a - b);
}
inline bool InText(const Image& img, const uint8_t* p, size_t n) {
    return InRange(img.text, p, n) && InRange({img.base, img.size}, p, n);
}
inline bool InImage(const Image& img, const uint8_t* p, size_t n) {
    return InRange({img.base, img.size}, p, n);
}
inline const uint8_t* RangeAt(const Section& range, const uint8_t* p, ptrdiff_t off, size_t n) {
    const auto a = reinterpret_cast<uintptr_t>(p);
    const auto magnitude = off < 0 ? uintptr_t{0} - static_cast<uintptr_t>(off) : static_cast<uintptr_t>(off);
    if (!p || (off < 0 ? magnitude > a : magnitude > UINTPTR_MAX - a)) return nullptr;
    const auto* result = reinterpret_cast<const uint8_t*>(off < 0 ? a - magnitude : a + magnitude);
    return InRange(range, result, n) ? result : nullptr;
}
inline const uint8_t* TextAt(const Image& img, const uint8_t* p, ptrdiff_t off, size_t n) {
    if (!InRange({img.base, img.size}, p, 0)) return nullptr;
    const auto* at = RangeAt(img.text, p, off, n);
    return InRange({img.base, img.size}, at, n) ? at : nullptr;
}
inline const uint8_t* ImageAt(const Image& img, const uint8_t* p, ptrdiff_t off, size_t n) {
    return RangeAt({img.base, img.size}, p, off, n);
}
inline const uint8_t* CheckedRip(const Image& img, const uint8_t* insn, size_t dispOff, size_t size) {
    if (dispOff > size || size - dispOff < sizeof(int32_t) || !InText(img, insn, size)) return nullptr;
    const auto* end = TextAt(img, insn, static_cast<ptrdiff_t>(size), 0);
    return ImageAt(img, end, Rel32(insn + dispOff), 1);
}

inline size_t PatternBytes(const char* p) {
    size_t n = 0;
    for (; *p; ++p)
        if (*p != ' ' && (p[1] == ' ' || p[1] == 0)) ++n;
    return n;
}

template <const FnSpec& S>
SigResult ResolveFn(const Image& img) {
    if (!InText(img, img.text.base, img.text.size)) return SigFail(".text outside the image");
    SigResult r = SigPattern(img.text, S.pattern);
    if (r.state != SigState::Ok) return r;
    for (size_t i = 0; i < S.nChecks; ++i) {
        const Check& c = S.checks[i];
        const uint8_t* at = c.off <= static_cast<size_t>(PTRDIFF_MAX)
            ? TextAt(img, r.at, static_cast<ptrdiff_t>(c.off), PatternBytes(c.bytes)) : nullptr;
        if (!at) return SigFail("function check outside .text");
        if (!BytesMatch(at, c.bytes)) return SigFail(c.why);
    }
    if (S.extra)
        if (const char* why = S.extra(img, r.at)) return SigFail(why);
    return r;
}

// The RIP target of an operand in another row; fails when it points outside the image.
template <const RipSpec& S>
SigResult ResolveRip(const Image& img) {
    const uint8_t* from = Sig(S.from);
    if (!from) return SigFail("source row missing");
    if (!img.base || !img.size) return SigFail("image range missing");
    if (S.off > static_cast<size_t>(PTRDIFF_MAX)) return SigFail("RIP source instruction outside .text");
    const uint8_t* insn = TextAt(img, from, static_cast<ptrdiff_t>(S.off), S.size);
    if (!insn) return SigFail("RIP source instruction outside .text");
    if (S.dispOff > S.size || S.size - S.dispOff < sizeof(int32_t)) return SigFail("RIP displacement outside instruction");
    const uint8_t* t = CheckedRip(img, insn, S.dispOff, S.size);
    if (!t) return SigFail("RIP target outside the image");
    return SigOk(t);
}

}  // namespace sco::game::rows
