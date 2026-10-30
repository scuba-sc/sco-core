// Signature rows for game.gamerules (F-4.10.196.36804-008): the parameter-sync site of the
// GameRules params module, label-anchored like features_sigs. The label string is unique in
// .rdata; the .text lea sites to it are counted and filtered by the sync function's prologue,
// so a patch that moves, adds or loses a site shows up as a failed row, not a silent pick.
// Anchored on findings F-4.10.196.36804-007 and F-4.10.196.36804-008,
// cross-build verified at 4.10.196.36804 and the 4.10.197.2878 hotfix.
#include "sco/game/signatures.h"
#include "sco/scan.h"
#include "sco/signatures.h"
#include <iterator>

namespace sco::game {
namespace {

constexpr const char* kParamsLabel = "IGameRulesParamsModule";
constexpr const char* kSyncPrologue = "40 55 53 57 41 54 41 55 41 56 41 57 48 8B EC 48";

SigResult ResolveParamsLabel(const Image& img, const SigResolveContext&) {
    const uint8_t* label = FindCString(img.rdata, kParamsLabel);
    if (!label) return SigFail("the GameRules params module label isn't in .rdata");
    if (FindCString({const_cast<uint8_t*>(label) + 1,
                     static_cast<size_t>(img.rdata.base + img.rdata.size - (label + 1))},
                    kParamsLabel))
        return SigFail("the params module label is not unique in .rdata");
    return SigOk(label);
}

SigResult ResolveParamsSync(const Image& img, const SigResolveContext&) {
    const uint8_t* label = FindCString(img.rdata, kParamsLabel);
    if (!label) return SigFail("the GameRules params module label isn't in .rdata");
    if (!img.text.base || img.text.size < 8) return SigFail("no .text");
    uint8_t* found = nullptr;
    int hits = 0;
    const uint8_t* p = img.text.base;
    const uint8_t* end = img.text.base + img.text.size - 7;
    for (; p < end; ++p) {
        if (p[1] != 0x8D) continue;
        const bool rex = p[0] == 0x48 || p[0] == 0x4C;
        if (!rex || (p[2] & 0xC7) != 0x05) continue;
        if (p + 7 + Rel32(p + 3) != label) continue;
        uint8_t* fn = FunctionStart(img, p);
        if (fn && BytesMatch(fn, kSyncPrologue)) {
            if (found == fn) continue;   /* same function, multiple label leas */
            found = fn;
            ++hits;
        }
        p += 6;
    }
    if (hits == 0) return SigFail("no params-sync function references the module label");
    if (hits > 1) return SigFail("more than one function matches the sync prologue and label");
    return SigOk(found);
}

}  // namespace

extern const SigDef kGameRulesSignatures[] = {
    {"gamerules.params_label", nullptr, 0, 0, ResolveParamsLabel, {nullptr}},
    {"gamerules.params_sync", nullptr, 0, 0, ResolveParamsSync, {"gamerules.params_label", nullptr}},
};
extern const size_t kGameRulesSignatureCount = std::size(kGameRulesSignatures);

}  // namespace sco::game
