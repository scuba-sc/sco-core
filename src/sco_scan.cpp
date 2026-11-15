#include "sco/scan.h"
#include <array>
#include <cstdlib>
#include <cstring>

namespace sco {

namespace {
// How common each byte value is in x64 code, roughly (higher: more common). FindPattern scans for the
// pattern byte with the lowest cost: memchr then skips almost everything, where anchoring on a
// common first byte (REX prefixes, 0x8B, zero padding) stops at every few bytes. Only a speed hint:
// any anchor byte finds exactly the same matches.
constexpr std::array<uint8_t, 256> MakeByteCost() {
    std::array<uint8_t, 256> cost{};
    for (auto& c : cost) c = 1;
    const struct { uint8_t value, weight; } common[] = {
        {0x00, 255}, {0xFF, 200}, {0x48, 190}, {0x8B, 170}, {0x89, 160}, {0xCC, 150}, {0x0F, 140}, {0x24, 130},
        {0x04, 120}, {0x01, 110}, {0xE8, 105}, {0x40, 100}, {0x41, 100}, {0x4C, 100}, {0x83, 95}, {0x08, 95},
        {0xC0, 90}, {0x45, 90}, {0x85, 85}, {0x74, 85}, {0x10, 80}, {0x20, 80}, {0x8D, 80}, {0x05, 75},
        {0x03, 75}, {0x02, 70}, {0x18, 70}, {0x28, 65}, {0x30, 65}, {0x38, 65}, {0x50, 60}, {0x80, 60},
        {0xC7, 60}, {0xC3, 60}, {0xEB, 55}, {0x75, 55}, {0x49, 55}, {0x44, 50}, {0x66, 50}, {0x90, 50},
        {0x5B, 45}, {0x5F, 45}, {0x57, 45}, {0x56, 45}, {0x53, 45}, {0x7E, 40}, {0x0D, 40},
    };
    for (const auto& c : common) cost[c.value] = c.weight;
    return cost;
}
constexpr std::array<uint8_t, 256> kByteCost = MakeByteCost();
}  // namespace

const uint8_t* FindCString(const Section& s, const char* str) {
    const size_t n = strlen(str) + 1;
    if (!s.base || s.size < n) return nullptr;
    uint8_t* const last = s.base + s.size - n;
    for (uint8_t* p = s.base; p <= last; ++p) {
        p = static_cast<uint8_t*>(memchr(p, str[0], static_cast<size_t>(last - p) + 1));
        if (!p) break;
        if ((p == s.base || p[-1] == 0) && memcmp(p, str, n) == 0) return p;
    }
    return nullptr;
}

int32_t Rel32(const uint8_t* p) { int32_t v; memcpy(&v, p, sizeof(v)); return v; }

bool BytesMatch(const uint8_t* p, const char* pattern) {
    for (const char* c = pattern; *c; ) {
        if (*c == ' ') { ++c; continue; }
        if (c[0] == '?') { ++p; c += (c[1] == '?') ? 2 : 1; continue; }
        char hex[3] = { c[0], c[1], 0 };
        if (*p++ != static_cast<uint8_t>(strtoul(hex, nullptr, 16))) return false;
        c += 2;
    }
    return true;
}

uint8_t* FindRipLea(const Section& text, uint8_t reg0, uint8_t reg1, uint8_t reg2, const uint8_t* target) {
    if (!text.base || text.size < 7) return nullptr;
    uint8_t* const end = text.base + text.size - 7;
    for (uint8_t* p = text.base; p <= end; ++p) {
        p = static_cast<uint8_t*>(memchr(p, reg0, static_cast<size_t>(end - p) + 1));
        if (!p) break;
        const auto next = reinterpret_cast<uintptr_t>(p) + 7;
        const int32_t disp = Rel32(p + 3);
        const auto magnitude = disp < 0 ? uintptr_t{0} - static_cast<uintptr_t>(disp) : static_cast<uintptr_t>(disp);
        if (disp < 0 ? magnitude > next : magnitude > UINTPTR_MAX - next) continue;
        const auto address = disp < 0 ? next - magnitude : next + magnitude;
        if (p[1] == reg1 && p[2] == reg2 && address == reinterpret_cast<uintptr_t>(target)) return p;
    }
    return nullptr;
}

int FindPattern(const Section& text, const char* pattern, uint8_t** out, int max) {
    uint8_t bytes[kMaxPatternBytes]; bool wild[kMaxPatternBytes]; size_t n = 0;
    for (const char* c = pattern; *c; ) {
        if (*c == ' ') { ++c; continue; }
        if (n == kMaxPatternBytes) return 0;
        if (c[0] == '?') { wild[n] = true; bytes[n++] = 0; c += (c[1] == '?') ? 2 : 1; continue; }
        char hex[3] = { c[0], c[1], 0 };
        bytes[n] = static_cast<uint8_t>(strtoul(hex, nullptr, 16)); wild[n++] = false; c += 2;
    }
    if (n == 0 || !text.base || text.size < n) return 0;
    int matches = 0;
    uint8_t* const end = text.base + text.size - n;
    // The anchor: the rarest fixed byte of the pattern, and its offset in it.
    size_t anchor = n;
    for (size_t i = 0; i < n; ++i)
        if (!wild[i] && (anchor == n || kByteCost[bytes[i]] < kByteCost[bytes[anchor]])) anchor = i;
    if (anchor == n) {   // every byte is a wildcard: every position matches
        for (uint8_t* p = text.base; p <= end; ++p) { if (matches < max) out[matches] = p; ++matches; }
        return matches;
    }
    // Candidate matches start at an anchor byte, in order, so matches are found in address order.
    uint8_t* const last = end + anchor;   // the last position where the anchor byte can sit
    for (uint8_t* a = text.base + anchor; a <= last; ++a) {
        a = static_cast<uint8_t*>(memchr(a, bytes[anchor], static_cast<size_t>(last - a) + 1));
        if (!a) break;
        uint8_t* const p = a - anchor;
        size_t i = 0;
        while (i < n && (wild[i] || p[i] == bytes[i])) ++i;
        if (i == n) { if (matches < max) out[matches] = p; ++matches; }
    }
    return matches;
}

uint8_t* FunctionStart(const Image& img, const uint8_t* at) {
    const auto base = reinterpret_cast<uintptr_t>(img.base);
    const auto address = reinterpret_cast<uintptr_t>(at);
    const auto directory = reinterpret_cast<uintptr_t>(img.pdata.base);
    if (!base || !img.size || img.size > UINTPTR_MAX - base || !at || address < base ||
        address - base >= img.size || !directory || directory < base ||
        directory - base > img.size || img.pdata.size > img.size - (directory - base)) return nullptr;
    const uint32_t rva = static_cast<uint32_t>(address - base);
    struct Entry { uint32_t begin, end, unwind; };
    const auto entry = [&](size_t off, Entry& e) {   // false when the 12 bytes at off aren't inside the image
        if (off > img.size || img.size - off < sizeof(Entry)) return false;
        memcpy(&e, img.base + off, sizeof(Entry));
        return true;
    };
    const size_t pdata = static_cast<size_t>(directory - base);
    // The entries are sorted by begin and don't overlap: binary search, as RtlLookupFunctionEntry does.
    size_t lo = 0, hi = img.pdata.size / sizeof(Entry);
    bool found = false;
    Entry e{};
    while (lo < hi) {
        const size_t mid = lo + (hi - lo) / 2;
        if (!entry(pdata + mid * sizeof(Entry), e)) return nullptr;
        if (rva < e.begin) hi = mid;
        else if (rva >= e.end) lo = mid + 1;
        else { found = true; break; }
    }
    if (!found) return nullptr;
    // A chained entry's unwind info ends with the parent RUNTIME_FUNCTION (after the 2-byte unwind
    // codes, padded to an even count): follow it to the primary function.
    constexpr uint8_t kChainInfo = 4;   // UNW_FLAG_CHAININFO
    for (int i = 0; i <= 8; ++i) {
        if (e.unwind >= img.size || img.size - e.unwind < 4) return nullptr;
        const uint8_t* info = img.base + e.unwind;
        if (!((info[0] >> 3) & kChainInfo))
            return e.begin < img.size ? img.base + e.begin : nullptr;
        const size_t tail = 4 + ((info[2] + 1u) & ~1u) * 2;
        if (i == 8 || tail > img.size - e.unwind || !entry(size_t{e.unwind} + tail, e)) return nullptr;
    }
    return nullptr;
}

uint8_t* FindUniquePattern(const Section& text, const char* pattern, int& matches) {
    uint8_t* hit = nullptr;
    matches = FindPattern(text, pattern, &hit, 1);
    return matches == 1 ? hit : nullptr;
}

}  // namespace sco
