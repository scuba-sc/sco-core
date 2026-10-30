// Signature row for natural mining: CBiomeBuilder::BuildLargeScaleEcoSystem. sc-offline's mining
// module (an earlier build, fixed RVA) hooked this function and ORed 0x8 into the rule flags of
// cells that had only built their physics; the row finds the same function in any build.
//
// The function is found from its profiler label, the "CBiomeBuilder::BuildLargeScaleEcoSystem"
// string the first block loads (lea r8, [label] at +0x9B), then walked back to the start with
// .pdata. The checks pin the ABI and every offset the hook relies on (docs/game/mining.md).
#include "sco/game/mining.h"
#include "sco/scan.h"
#include "sco/signatures.h"
#include "sig_rows.h"
#include <cstring>
#include <iterator>

namespace sco::game {

namespace {

using namespace mining;
using rows::InText;

SigResult Missing()        { return { SigState::Missing, nullptr, 0, nullptr }; }
SigResult Ambiguous(int n) { return { SigState::Ambiguous, nullptr, n, nullptr }; }

constexpr size_t kLabelLea = 0x9B;

constexpr rows::Check kCellChecks[] = {
    { 0x000, "44 89 4C 24 20 4C 89 44 24 18 48 89 54 24 10 48 89 4C 24 08", "arguments aren't saved to their home slots at +0x000" },
    { 0x014, "55 53 56 57 41 54 41 55 41 56 41 57 48 8D AC 24 28 FE FF FF 48 81 EC A8 03 00 00", "frame layout changed at +0x014" },      // rbp = rsp - 0x1d8 after 8 pushes: [rbp+0x240] is the 5th argument
    { 0x03E, "45 8B E1", "the requested LOD (r9d) isn't kept in r12d at +0x03e" },
    { 0x052, "4C 8B EA", "the cell (rdx) isn't kept in r13 at +0x052" },
    { 0x05C, "48 8B F1", "the builder (rcx) isn't kept in rsi at +0x05c" },
    { 0x0FD, "44 8B B5 40 02 00 00", "the flags aren't read from the fifth argument at +0x0fd" },
    { 0x127, "41 8B CE 83 E0 04 83 E1 08 41 83 E6 02", "the flag bits aren't split at +0x127" },                                       // flags & 4, & 8, & 2
    { 0x147, "8B 46 08 83 F8 01", "the builder type isn't read at +8 at +0x147" },
    { 0x441, "84 DB 0F 84 ?? ?? ?? ?? 80 7E 29 00 0F 85", "the builder spawn gate isn't at +0x29 at +0x441" },
    { 0x4BF, "83 7D F8 00 74 1D 41 83 BD A8 16 00 00 01 7E 13 41 83 FC 01 7F 0D C7 45 A8 01 00 00 00 C6 45 86 01",
             "the harvestable LOD / spawn flag branch changed at +0x4bf" },                                                         // flags & 8, cell+0x16a8 > 1, lod <= 1 -> spawn mode 1
    { 0x929, "48 8D 85 40 02 00 00", "the flags aren't passed on by address at +0x929" },
};
static_assert(kCellBuilderType == 8 && kCellSpawnGate == 0x29 && kCellHarvestLod == 0x16A8 && kRuleSpawnBit == 8,
              "kCellChecks pin these offsets");

SigResult ResolveCell(const Image& img, const SigResolveContext&) {
    const uint8_t* label = FindCString(img.rdata, "CBiomeBuilder::BuildLargeScaleEcoSystem");
    if (!label) return SigFail("the BuildLargeScaleEcoSystem label isn't in .rdata");
    // lea r8, [label] is 4C 8D 05: count every one, a second use of the label would be ambiguous.
    const uint8_t* const end = img.text.base + img.text.size - 7;
    const uint8_t* site = nullptr;
    int n = 0;
    for (const uint8_t* p = img.text.base; p < end; ++p) {
        p = static_cast<const uint8_t*>(memchr(p, 0x4C, static_cast<size_t>(end - p)));
        if (!p) break;
        if (p[1] != 0x8D || p[2] != 0x05 || RipTarget(p, 3, 7) != label) continue;
        if (n++ == 0) site = p;
    }
    if (n == 0) return Missing();
    if (n > 1) return Ambiguous(n);
    const uint8_t* f = FunctionStart(img, site);
    if (!f) return SigFail("no .pdata entry holds the label's function");
    if (site != f + kLabelLea) return SigFail("the label isn't loaded at +0x09b");
    for (const auto& c : kCellChecks)
        if (!InText(img, f + c.off, rows::PatternBytes(c.bytes)) || !BytesMatch(f + c.off, c.bytes)) return SigFail(c.why);
    return SigOk(f);
}

// ---- capabilities ----------------------------------------------------------------------------

constexpr const char* kMiningCell[] = { "mining.cell" };

constexpr Capability kCaps[] = {
    { "game.mining", kMiningCell, std::size(kMiningCell) },
};

}  // namespace

namespace mining {

const Capability* Capabilities(size_t& count) {
    count = std::size(kCaps);
    return kCaps;
}

}  // namespace mining

extern const SigDef kMiningSignatures[] = {
    { "mining.cell", nullptr, 0, 0, ResolveCell, {} },
};
extern const size_t kMiningSignatureCount = std::size(kMiningSignatures);

}  // namespace sco::game
