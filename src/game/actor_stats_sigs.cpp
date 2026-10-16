// Signature rows for game.actor_stats (F-4.10.197.2878-006): the guard that keeps a named stat on
// the game's stat index, and the record array GetStat reads.
//
// actor.stat_names: the game's stat-type name function, index -> name, a bounded jump table:
//   cmp edx,1Ch; ja <undefined>; movsxd rax,edx; lea rdx,[image base]; mov ecx,[rdx+rax*4+table];
//   add rcx,rdx; jmp rcx, every case `lea rax,[name]; ret`. The pattern is unique in .text. The row
//   then follows the table for every entry in actor_stats_map.h and checks that the case still
//   loads the expected name, so an enum the game reorders fails here instead of in a plugin.
// actor.stat_array: actor.get_stat's body, checked to be
//   movsxd rax,edx; mov rdx,[rcx+rax*8+disp32]; test rdx,rdx; je; vmovss xmm0,[rdx]; ret.
//   The row's address is the disp32 (the record array's offset in CSCActorStatus), so the service
//   can tell "no record of this stat" from a real 0.0 without a hard-coded offset.
#include "actor_stats_map.h"
#include "sig_rows.h"
#include "sco/scan.h"
#include "sco/signatures.h"
#include <cstring>
#include <iterator>

namespace sco::game {
namespace {

using rows::InText;

constexpr const char* kNameSwitch =
    "83 FA 1C 0F 87 ?? ?? ?? ?? 48 63 C2 48 8D 15 ?? ?? ?? ?? 8B 8C 82 ?? ?? ?? ?? 48 03 CA FF E1";
constexpr size_t kBaseLea = 0x0C;     // lea rdx,[rip+disp32] -> the image base
constexpr size_t kTableDisp = 0x16;   // mov ecx,[rdx+rax*4+disp32]: the table's RVA
constexpr const char* kNameCase = "48 8D 05 ?? ?? ?? ?? C3";   // lea rax,[name]; ret
static_assert(actor_stats::kNameCount == 0x1D, "kNameSwitch's cmp edx,1Ch pins the bound");

constexpr const char* kGetStatBody = "48 63 C2 48 8B 94 C1 ?? ?? ?? ?? 48 85 D2 74 ?? C5 FA 10 02 C3";
constexpr size_t kArrayDisp = 7;

bool InImage(const Image& img, const uint8_t* p, size_t n) {
    return img.base && img.size && p >= img.base && p <= img.base + img.size &&
           static_cast<size_t>(img.base + img.size - p) >= n;
}

bool NameIs(const Image& img, const uint8_t* s, const char* want) {
    const size_t n = strlen(want) + 1;
    return InImage(img, s, n) && memcmp(s, want, n) == 0;
}

SigResult ResolveStatNames(const Image& img) {
    SigResult r = SigPattern(img.text, kNameSwitch);
    if (r.state != SigState::Ok) return r;
    if (!img.base || !img.size) return SigFail("no image base to follow the name table from");
    if (RipTarget(r.at + kBaseLea, 3, 7) != img.base) return SigFail("the name function's base lea isn't the image base");
    uint32_t tableRva = 0;
    memcpy(&tableRva, r.at + kTableDisp, sizeof(tableRva));
    const uint8_t* table = img.base + tableRva;
    if (!InImage(img, table, 4u * actor_stats::kNameCount)) return SigFail("the name table is outside the image");
    for (const actor_stats::Entry& e : actor_stats::kEntries) {
        uint32_t caseRva = 0;
        memcpy(&caseRva, table + 4u * static_cast<unsigned>(e.index), sizeof(caseRva));
        const uint8_t* c = img.base + caseRva;
        if (!InText(img, c, 8) || !BytesMatch(c, kNameCase)) return SigFail("a stat name case changed shape");
        if (!NameIs(img, RipTarget(c, 3, 7), e.name)) return SigFail("a stat index no longer has its expected name");
    }
    return r;
}

SigResult ResolveStatArray(const Image& img) {
    const uint8_t* fn = Sig("actor.get_stat");
    if (!fn) return SigFail("actor.get_stat missing");
    if (!InText(img, fn, 21) || !BytesMatch(fn, kGetStatBody)) return SigFail("GetStat's body changed (record array read)");
    return SigOk(fn + kArrayDisp);
}

}  // namespace

extern const SigDef kActorStatsSignatures[] = {
    { "actor.stat_names", nullptr, 0, 0, ResolveStatNames, {} },
    { "actor.stat_array", nullptr, 0, 0, ResolveStatArray, { "actor.get_stat" } },
};
extern const size_t kActorStatsSignatureCount = std::size(kActorStatsSignatures);

}  // namespace sco::game
