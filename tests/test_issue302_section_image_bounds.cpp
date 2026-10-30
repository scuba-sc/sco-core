#include "sco/scan.h"
#include "sco/signatures.h"
#include <cassert>
#include <cstdio>
#include <cstring>
#include <vector>

// Exercise actual scanners, recording every whole-section scan requested by a
// direct resolver. Malformed metadata must be rejected before reaching them.
#define FindCString SectionTestFindCString
#define FindPattern SectionTestFindPattern
#define FindRipLea SectionTestFindRipLea
#include "../src/sco_scan.cpp"
#undef FindCString
#undef FindPattern
#undef FindRipLea
#include "../src/game/sig_rows.h"

namespace sco {
const Image* currentImage;
size_t rejectedScans;
bool Allowed(const Section& s) {
    if (game::rows::InImage(*currentImage, s.base, s.size)) return true;
    ++rejectedScans;
    return false;
}
const uint8_t* FindCString(const Section& s, const char* text) {
    return Allowed(s) ? SectionTestFindCString(s, text) : nullptr;
}
int FindPattern(const Section& s, const char* pattern, uint8_t** out, int max) {
    return Allowed(s) ? SectionTestFindPattern(s, pattern, out, max) : 0;
}
uint8_t* FindRipLea(const Section& s, uint8_t a, uint8_t b, uint8_t c, const uint8_t* target) {
    return Allowed(s) ? SectionTestFindRipLea(s, a, b, c, target) : nullptr;
}
uint8_t* Sig(const char*) { return nullptr; }
SigResult SigPattern(const Section& s, const char* p) {
    uint8_t* at = nullptr;
    const int n = FindPattern(s, p, &at, 1);
    return n == 1 ? SigOk(at) : SigResult{SigState::Missing, nullptr, n, nullptr};
}
}
#define TABLES(X) X(Actors) X(World) X(Offline) X(GameRules) X(Mining) X(Entities) X(Events) X(System) X(Pak) X(Hangar) X(Asop) X(Atc) X(Contracts) X(DataCoreLive) X(ActorStats) X(Inventory) X(Feature) X(Teleport)
namespace sco::game {
#define DECLARE(n) extern const SigDef k##n##Signatures[]; extern const size_t k##n##SignatureCount;
TABLES(DECLARE)
#undef DECLARE
}
int main() {
    struct Table { const sco::SigDef* rows; size_t count; };
#define ENTRY(n) {sco::game::k##n##Signatures, sco::game::k##n##SignatureCount},
    const Table tables[] = {TABLES(ENTRY)};
#undef ENTRY
    std::vector<uint8_t> bytes(8192), external(8192);
    size_t rows = 0;
    for (const auto& table : tables) {
        rows += table.count;
        for (size_t i = 0; i < table.count; ++i) {
            const auto& row = table.rows[i];
            if (!row.resolve) continue;
            for (int section = 0; section < 3; ++section) {
              for (int shape = 0; shape < 3; ++shape) {
                sco::Image img{};
                img.base = bytes.data(); img.size = bytes.size();
                img.text = {bytes.data(), 4096};
                img.rdata = {bytes.data() + 4096, 2048};
                img.pdata = {bytes.data() + 6144, 2048};
                auto* bad = section == 0 ? &img.text : section == 1 ? &img.rdata : &img.pdata;
                *bad = shape == 0 ? sco::Section{external.data(), external.size()}
                    : shape == 1 ? sco::Section{bytes.data() + bytes.size() - 4, 8}
                    : sco::Section{reinterpret_cast<uint8_t*>(UINTPTR_MAX - 3), 8};
                sco::currentImage = &img;
                sco::rejectedScans = 0;
                row.resolve(img);
                if (sco::rejectedScans) {
                    std::fprintf(stderr, "%s scanned section %d outside image\n", row.id, section);
                    return 1;
                }
              }
            }
        }
    }
    const auto find = [&](const char* id) -> const sco::SigDef& {
        for (const auto& table : tables)
            for (size_t i = 0; i < table.count; ++i)
                if (std::strcmp(table.rows[i].id, id) == 0) return table.rows[i];
        assert(false);
        return tables[0].rows[0];
    };
    sco::Image valid{};
    valid.base = bytes.data(); valid.size = bytes.size();
    valid.text = {bytes.data(), 4096}; valid.rdata = {bytes.data() + 4096, 2048};
    sco::currentImage = &valid;
    const auto missing = find("spawn.landing_helper").resolve(valid);
    assert(missing.why && std::strcmp(missing.why, "the Landing Area message isn't in .rdata") == 0);
    valid.rdata = {external.data(), external.size()};
    const auto invalid = find("spawn.landing_helper").resolve(valid);
    assert(invalid.state == sco::SigState::Failed && invalid.why && std::strstr(invalid.why, ".rdata"));
    // A text-only resolver must not start requiring unrelated data sections.
    const uint8_t store[] = {0x44,0x89,0x4C,0x24,0x20,0x48,0x89,0x4C,0x24,0x08,
        0x55,0x53,0x56,0x57,0x41,0x54,0x41,0x55,0x41,0x56,0x41,0x57,0x48,0x8D,0xAC,0x24,0xA8,0xFC,0xFF,0xFF};
    std::memcpy(bytes.data(), store, sizeof(store));
    valid.rdata = {}; valid.pdata = {};
    assert(find("atc.store_vehicle").resolve(valid).state == sco::SigState::Ok);
    std::printf("%zu rows: direct resolver scans remain inside image; diagnostics and text-only control passed\n", rows);
}
