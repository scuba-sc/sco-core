// SPDX-License-Identifier: GPL-3.0-only
// FindPattern anchors on the pattern's rarest byte. This checks it returns exactly what the plain
// first-byte scan it replaced returned (count, order and the first `max` addresses) over randomized
// buffers and patterns with wildcards, including boundary and degenerate cases. Synthetic bytes only.
#include "sco/scan.h"
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <random>
#include <string>
#include <vector>

static int pass = 0, fail = 0;
#define CHECK(...) do { if (__VA_ARGS__) ++pass; else { ++fail; std::printf("FAIL %d: %s\n", __LINE__, #__VA_ARGS__); } } while (0)

// The previous implementation, unchanged: memchr on the first byte unless it is a wildcard.
static int ReferenceFindPattern(const sco::Section& text, const char* pattern, uint8_t** out, int max) {
    uint8_t bytes[sco::kMaxPatternBytes]; bool wild[sco::kMaxPatternBytes]; size_t n = 0;
    for (const char* c = pattern; *c; ) {
        if (*c == ' ') { ++c; continue; }
        if (n == sco::kMaxPatternBytes) return 0;
        if (c[0] == '?') { wild[n] = true; bytes[n++] = 0; c += (c[1] == '?') ? 2 : 1; continue; }
        char hex[3] = { c[0], c[1], 0 };
        bytes[n] = static_cast<uint8_t>(strtoul(hex, nullptr, 16)); wild[n++] = false; c += 2;
    }
    if (n == 0 || !text.base || text.size < n) return 0;
    int matches = 0;
    uint8_t* const end = text.base + text.size - n;
    for (uint8_t* p = text.base; p <= end; ++p) {
        if (!wild[0]) p = static_cast<uint8_t*>(memchr(p, bytes[0], static_cast<size_t>(end - p) + 1));
        if (!p) break;
        size_t i = 1;
        while (i < n && (wild[i] || p[i] == bytes[i])) ++i;
        if (i == n) { if (matches < max) out[matches] = p; ++matches; }
    }
    return matches;
}

static bool Same(const sco::Section& text, const std::string& pattern, int max) {
    std::vector<uint8_t*> a(static_cast<size_t>(max) + 1, nullptr), b(static_cast<size_t>(max) + 1, nullptr);
    const int ra = ReferenceFindPattern(text, pattern.c_str(), a.data(), max);
    const int rb = sco::FindPattern(text, pattern.c_str(), b.data(), max);
    if (ra != rb) return false;
    for (int i = 0; i < ra && i < max; ++i) if (a[static_cast<size_t>(i)] != b[static_cast<size_t>(i)]) return false;
    // FindUniquePattern is defined by FindPattern: same verdict as the reference.
    int matches = 0;
    uint8_t* unique = sco::FindUniquePattern(text, pattern.c_str(), matches);
    if (matches != ra) return false;
    uint8_t* one = nullptr;
    ReferenceFindPattern(text, pattern.c_str(), &one, 1);
    return unique == (ra == 1 ? one : nullptr);
}

static std::string RandomPattern(std::mt19937& rng, const std::vector<uint8_t>& alphabet, size_t length, int wildPercent) {
    std::string out;
    for (size_t i = 0; i < length; ++i) {
        if (!out.empty() && rng() % 4) out += ' ';
        if (static_cast<int>(rng() % 100) < wildPercent) out += rng() % 2 ? "??" : "?";
        else {
            // Lowercase or uppercase digits, same value.
            char text[3];
            std::snprintf(text, sizeof(text), rng() % 2 ? "%02x" : "%02X", alphabet[rng() % alphabet.size()]);
            out += text;
        }
    }
    return out;
}

int main() {
    std::mt19937 rng(0x5C0DEu);
    // Small alphabets give many overlapping matches, large ones few; some bytes common in code.
    const std::vector<std::vector<uint8_t>> alphabets = {
        {0x00}, {0x00, 0x48}, {0x00, 0x48, 0x8B}, {0x48, 0x8B, 0x05, 0xE8, 0xCC, 0x00, 0xFF},
        {0x01, 0x02, 0x03, 0x04, 0x05, 0x06, 0x07, 0x08},
    };
    int iterations = 0;
    for (int round = 0; round < 6000; ++round) {
        const auto& alphabet = alphabets[rng() % alphabets.size()];
        const size_t size = rng() % 6 == 0 ? rng() % 8 : rng() % 700;
        std::vector<uint8_t> buffer(size + 16, 0xEE);   // guard bytes after the section, never matched
        for (size_t i = 0; i < size; ++i) buffer[i] = alphabet[rng() % alphabet.size()];
        sco::Section section{buffer.data(), size};
        for (int k = 0; k < 20; ++k) {
            const size_t length = 1 + rng() % 14;
            const int wild = static_cast<int>(rng() % 4) * 20;
            std::string pattern = RandomPattern(rng, alphabet, length, wild);
            // Half of the time take the pattern from the buffer itself so it does occur, wildcards on top.
            if (size >= length && rng() % 2) {
                const size_t at = rng() % (size - length + 1);
                pattern.clear();
                for (size_t i = 0; i < length; ++i) {
                    if (!pattern.empty()) pattern += ' ';
                    if (static_cast<int>(rng() % 100) < wild) { pattern += "??"; continue; }
                    char text[3];
                    std::snprintf(text, sizeof(text), "%02X", buffer[at + i]);
                    pattern += text;
                }
            }
            for (int max : {0, 1, 2, 5, 1000}) {
                ++iterations;
                if (!Same(section, pattern, max)) {
                    ++fail;
                    std::printf("MISMATCH size %zu max %d pattern '%s'\n", size, max, pattern.c_str());
                    if (fail > 20) return 1;
                }
            }
        }
    }
    CHECK(iterations > 100000);

    // Hand-written boundaries.
    const uint8_t code[] = {0x48, 0x8B, 0x05, 0x10, 0x20, 0x30, 0x40, 0x48, 0x8B, 0x05, 0x10, 0x20, 0x30, 0x41, 0x00};
    const sco::Section section{const_cast<uint8_t*>(code), sizeof(code)};
    for (const char* pattern : {"48", "48 8B 05", "48 8B 05 ?? ?? ?? ??", "?? 8B", "8B ??", "?? ??", "??", "?",
                                "41 00", "00", "?? 00", "48 8B 05 10 20 30 40 48 8B 05 10 20 30 41 00",
                                "48 8B 05 10 20 30 40 48 8B 05 10 20 30 41 00 00", "FF", "", "  ", "48  8b  05"})
        for (int max : {0, 1, 3, 100}) CHECK(Same(section, pattern, max));
    // A pattern too long for the table matches nothing, as before.
    std::string tooLong;
    for (size_t i = 0; i < sco::kMaxPatternBytes + 1; ++i) tooLong += "00 ";
    CHECK(Same(section, tooLong, 4));
    std::string exact;
    for (size_t i = 0; i < sco::kMaxPatternBytes; ++i) exact += "00 ";
    std::vector<uint8_t> zeros(sco::kMaxPatternBytes + 5, 0);
    CHECK(Same(sco::Section{zeros.data(), zeros.size()}, exact, 10));
    // Empty and null sections.
    CHECK(Same(sco::Section{nullptr, 0}, "48", 1));
    CHECK(Same(sco::Section{zeros.data(), 0}, "00", 1));
    // Large zero-filled section with one rare byte: the anchor must find the one hit at both ends.
    std::vector<uint8_t> big(1 << 20, 0);
    big.front() = 0x7A; big.back() = 0x7B; big[777777] = 0x7A;
    const sco::Section bigSection{big.data(), big.size()};
    for (const char* pattern : {"7A 00 00", "00 00 7B", "7A", "7B", "7A ?? ?? ?? 00", "00 ?? 7A ?? 00"})
        CHECK(Same(bigSection, pattern, 4));
    std::printf("scan equivalence: %d passed, %d failed\n", pass, fail);
    return fail ? 1 : 0;
}
