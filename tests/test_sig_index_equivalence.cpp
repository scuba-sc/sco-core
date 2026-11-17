// SPDX-License-Identifier: GPL-3.0-only
// The signature resolvers used to find each rip-relative `lea` by walking .text byte by byte; they now
// look the target up in the resolve pass's ScanIndex (include/sco/scan.h). This runs, for every
// replaced helper, a verbatim copy of the old loop against the real new helper over randomized
// buffers and asserts identical results in identical order:
//   * the new helpers are the ones in src/game/*_sigs.cpp, compiled into this file (each in its own
//     namespace) so the adapters under test are the shipped ones, not copies;
//   * buffers are dense in REX/opcode/ModRM bytes, with overlapping sites, sites at both ends,
//     displacements aimed before, into, after and exactly at the edges of the buffer;
//   * the index is exercised built-once-and-reused, absent (direct walk) and capped (fallback).
// Then three self-checks: deliberately mutated "new" variants must make the same comparison fail.
// Synthetic bytes only, no game data.
#include "sco/game/actors.h"
#include "sco/game/contracts.h"
#include "sco/game/entities.h"
#include "sco/game/events.h"
#include "sco/game/offline.h"
#include "sco/game/signatures.h"
#include "sco/game/world.h"
#include "sco/scan.h"
#include "sco/signature_registry.h"
#include "sco/signatures.h"
#include "../src/game/actor_stats_map.h"
#include "../src/game/sig_rows.h"
#include <algorithm>
#include <array>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <iterator>
#include <string>
#include <utility>
#include <vector>

// The resolver sources, each compiled into its own namespace sco::game_<unit> (the macro renames
// the `namespace sco::game` they open); `using namespace` keeps the names they take from
// sco::game visible. Unused-function noise from the files' private helpers is expected here.
#if defined(_MSC_VER) && !defined(__clang__)
#pragma warning(push)
#pragma warning(disable : 4505 4068 4100 4189)
#else
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wunused-function"
#pragma GCC diagnostic ignored "-Wunused-const-variable"
#pragma GCC diagnostic ignored "-Wunused-variable"
#pragma GCC diagnostic ignored "-Wunused-parameter"
#endif
#define SIG_UNIT_NS(name) namespace sco::game_##name { using namespace ::sco::game; }
SIG_UNIT_NS(contracts) SIG_UNIT_NS(entities) SIG_UNIT_NS(events) SIG_UNIT_NS(world)
SIG_UNIT_NS(actors) SIG_UNIT_NS(actor_stats) SIG_UNIT_NS(gamerules) SIG_UNIT_NS(offline)
namespace sco::game_entities::entities { using ::sco::game::entities::Capability; }
#define game game_contracts
#include "../src/game/contracts_sigs.cpp"
#undef game
#define game game_entities
#include "../src/game/entities_sigs.cpp"
#undef game
#define game game_events
#include "../src/game/events_sigs.cpp"
#undef game
#define game game_world
#include "../src/game/world_sigs.cpp"
#undef game
#define game game_actors
#include "../src/game/actors_sigs.cpp"
#undef game
#define game game_actor_stats
#include "../src/game/actor_stats_sigs.cpp"
#undef game
#define game game_gamerules
#include "../src/game/gamerules_sigs.cpp"
#undef game
#define game game_offline
#include "../src/game/offline_sigs.cpp"
#undef game
#if defined(_MSC_VER) && !defined(__clang__)
#pragma warning(pop)
#else
#pragma GCC diagnostic pop
#endif

static int pass = 0, fail = 0;
#define CHECK(...) do { if (__VA_ARGS__) ++pass; else { ++fail; std::printf("FAIL %d: %s\n", __LINE__, #__VA_ARGS__); } } while (0)

// ---- the old loops, verbatim from the previous commit, renamed Old* ----------------------------------
// They sit in the namespace of the file they came from, so they use that file's own helpers.

namespace sco::game_contracts {
const uint8_t* OldLeaTo(const Image& img, const uint8_t* target, const uint8_t* from) {
    const Section& text = img.text;
    if (!InImage(img, target, 1) || !Mapped(img, text) || text.size < 7) return nullptr;
    const auto b = reinterpret_cast<uintptr_t>(text.base);
    const auto f = from ? reinterpret_cast<uintptr_t>(from) : b;
    if (f < b || f - b > text.size) return nullptr;
    for (size_t i = f - b; i <= text.size - 7; ++i) {
        const uint8_t* p = TextOffset(img, text.base, i, 7);
        if ((p[0] & 0xFB) == 0x48 && p[1] == 0x8D && (p[2] & 0xC7) == 0x05 &&
            CheckedRip(img, p, 3, 7) == target) return p;
    }
    return nullptr;
}
const uint8_t* OldExactLeaTo(const Image& img, const uint8_t* target) {
    for (const uint8_t* p = OldLeaTo(img, target, nullptr); p; ) {
        if (BytesMatch(p, "4C 8D 05")) return p;
        const auto* next = TextAt(img, p, 1, 0);
        if (!next) break;
        p = OldLeaTo(img, target, next);
    }
    return nullptr;
}
}

namespace sco::game_entities {
int OldRipRefs(const Section& text, uint8_t reg0, uint8_t reg1, uint8_t reg2, const uint8_t* target,
            const uint8_t** out, int max) {
    if (!rows::InRange(text, text.base, text.size) || text.size < 7 || !target) return 0;
    int n = 0;
    for (size_t i = 0; i <= text.size - 7; ++i) {
        const uint8_t* p = text.base + i;
        const uintptr_t addr = reinterpret_cast<uintptr_t>(p);
        if (addr > UINTPTR_MAX - 7) continue;
        uintptr_t dest = addr + 7;
        const int64_t delta = Rel32(p + 3);
        if (delta < 0) { const uintptr_t d = static_cast<uintptr_t>(-delta); if (d > dest) continue; dest -= d; }
        else { const uintptr_t d = static_cast<uintptr_t>(delta); if (d > UINTPTR_MAX - dest) continue; dest += d; }
        if (p[0] == reg0 && p[1] == reg1 && p[2] == reg2 && static_cast<uintptr_t>(dest) == reinterpret_cast<uintptr_t>(target)) {
            if (n < max) out[n] = p;
            ++n;
        }
    }
    return n;
}
}

namespace sco::game_events {
int OldLeaRefs(const Section& text, uint8_t r0, uint8_t r1, uint8_t r2, const uint8_t* target, const uint8_t** out, int max) {
    if (!rows::InRange(text, text.base, text.size) || text.size < 7 || !target) return 0;
    int n = 0;
    for (size_t i = 0; i <= text.size - 7; ++i) {
        const uint8_t* p = text.base + i;
        const uintptr_t addr = reinterpret_cast<uintptr_t>(p);
        if (addr > UINTPTR_MAX - 7) continue;
        uintptr_t dest = addr + 7;
        const int64_t delta = Rel32(p + 3);
        if (delta < 0) { const uintptr_t d = static_cast<uintptr_t>(-delta); if (d > dest) continue; dest -= d; }
        else { const uintptr_t d = static_cast<uintptr_t>(delta); if (d > UINTPTR_MAX - dest) continue; dest += d; }
        if (p[0] == r0 && p[1] == r1 && p[2] == r2 && static_cast<uintptr_t>(dest) == reinterpret_cast<uintptr_t>(target)) {
            if (n < max) out[n] = p;
            ++n;
        }
    }
    return n;
}
}

namespace sco::game_world {
template <class Ok>
int OldRipRefs(const Image& img, uint8_t r1, uint8_t r2, const uint8_t* target, size_t lead, size_t tail,
            uint8_t** out, int max, Ok ok) {
    if (!target || !rows::InImage(img, target, 1) || !rows::InImage(img, img.text.base, img.text.size)) return 0;
    const Section& text = img.text;
    if (!text.base || tail < 7 || lead > text.size || tail > text.size - lead) return 0;
    const size_t last = text.size - tail;
    int n = 0;
    for (size_t off = lead; off <= last; ++off) {
        uint8_t* p = text.base + off;
        if (p[0] != 0x48 || p[1] != r1 || p[2] != r2) continue;
        if (CheckedRip(img, p, 3, 7) != target || !ok(p)) continue;
        if (n < max) out[n] = p;
        ++n;
    }
    return n;
}
}

namespace sco::game_actors {
int OldRipRefs(const Image& img, uint8_t reg0, uint8_t reg1, uint8_t reg2, const uint8_t* target,
            const uint8_t** out, int max) {
    const Section& text = img.text;
    if (!text.base || text.size < 7 || text.size > static_cast<size_t>(PTRDIFF_MAX) || !rows::InText(img, text.base, text.size) ||
        !rows::InImage(img, target, 1)) return 0;
    int n = 0;
    // Seven-byte extent includes the final possible instruction.
    const size_t last = text.size - 7;
    for (size_t off = 0; off <= last; ++off) {
        const uint8_t* begin = rows::TextAt(img, text.base, static_cast<ptrdiff_t>(off), last - off + 1);
        const uint8_t* p = static_cast<const uint8_t*>(memchr(begin, reg0, last - off + 1));
        if (!p) break;
        off = static_cast<size_t>(p - text.base);
        // memchr's capped start range guarantees the complete seven-byte instruction.
        if (p[0] == reg0 && p[1] == reg1 && p[2] == reg2 &&
            rows::CheckedRip(img, p, 3, 7) == target) {
            if (n < max) out[n] = p;
            ++n;
        }
    }
    return n;
}
SigResult OldResolveFlightReady(const Image& img, const SigResolveContext&) {
    if (!rows::InImage(img, img.rdata.base, img.rdata.size)) return SigFail("invalid .rdata");
    const Section& text = img.text;
    const uint8_t* header = RdataString(img, "C:\\workspace\\CryEngine\\Code\\CryEngine\\CryCommon\\Events/ISC/Dashboards.h");
    if (!header) return SigFail("the Dashboards.h path isn't in .rdata");
    if (!text.base || text.size < 0x206 || text.size > static_cast<size_t>(PTRDIFF_MAX) || !rows::InText(img, text.base, text.size) ||
        !rows::InImage(img, header, 1)) return Missing();
    const uint8_t* found = nullptr;
    int hits = 0;
    // p denotes the opcode byte after the REX prefix. The last LEA ends at .text's end.
    for (size_t off = 0x200; off <= text.size - 6; ++off) {
        const uint8_t* lea = rows::TextAt(img, text.base, static_cast<ptrdiff_t>(off - 1), 7);
        if (!BytesMatch(lea, "48 8D 15") || rows::CheckedRip(img, lea, 3, 7) != header) continue;
        const uint8_t* start = nullptr;
        for (ptrdiff_t back = 0; back < 0x60; ++back) {
            const uint8_t* q = rows::TextAt(img, lea, -back - 2, 2);
            if (!q) break;
            if (q[0] == 0xCC && q[1] == 0xCC) {
                start = rows::TextAt(img, q, 2, 4); break;
            }
        }
        if (!start) continue;
        uint32_t line = 0;
        const size_t span = static_cast<size_t>(lea - start);
        for (size_t i = 0; i + 6 <= span; ++i) {
            const uint8_t* q = rows::TextAt(img, start, static_cast<ptrdiff_t>(i), 6);
            if (q[0] == 0x41 && q[1] == 0xB8) memcpy(&line, q + 2, 4);
        }
        if (line == 0x49 && BytesMatch(start, "48 89 5C 24") && Contains(img, start, lea, "48 8B FA") && Contains(img, start, lea, "4C 89 44 24")) {
            found = start;
            ++hits;
        }
    }
    if (!hits) return Missing();
    if (hits > 1) return Ambiguous(hits);
    return SigOk(found);
}
}

namespace sco::game_actor_stats {
SigResult OldResolveAdjustStat(const Image& img, const SigResolveContext&) {
    if (!Code(img, img.text.base, img.text.size)) return SigFail("invalid .text range");
    SigResult r = SigPattern(img.text, kAdjustPrologue);
    if (r.state != SigState::Ok) return r;
    if (!InImage(img, img.rdata.base, img.rdata.size)) return SigFail("invalid .rdata range");
    const uint8_t* label = FindCString(img.rdata, kAdjustLabel);
    if (!label) return SigFail("the AdjustStatValue label isn't in .rdata");
    if (!Code(img, img.text.base, img.text.size) || img.text.size < 7) return SigFail("no .text");
    int leas = 0;
    for (size_t off = 0; off <= img.text.size - 7; ++off) {
        const uint8_t* p = rows::TextAt(img, img.text.base, static_cast<ptrdiff_t>(off), 7);
        if ((p[0] != 0x48 && p[0] != 0x4C) || p[1] != 0x8D || (p[2] & 0xC7) != 0x05) continue;
        if (Target(img, p) != label) continue;
        if (FunctionStart(img, p) != r.at) return SigFail("an AdjustStatValue label lea is outside the matched function");
        ++leas;
        off += 6;
    }
    if (!leas) return SigFail("the matched function doesn't load the AdjustStatValue label");
    return r;
}
}

namespace sco::game_gamerules {
SigResult OldResolveParamsSync(const Image& img, const SigResolveContext&) {
    if (!rows::InImage(img, img.rdata.base, img.rdata.size))
        return SigFail("invalid .rdata range");
    const uint8_t* label = FindCString(img.rdata, kParamsLabel);
    if (!label) return SigFail("the GameRules params module label isn't in .rdata");
    if (img.text.size < 7 || !rows::InText(img, img.text.base, img.text.size))
        return SigFail("no .text");
    if (!rows::InImage(img, img.base, img.size) ||
        !rows::InImage(img, img.pdata.base, img.pdata.size)) return SigFail("invalid .pdata/image");
    uint8_t* found = nullptr;
    int hits = 0;
    for (size_t off = 0; off <= img.text.size - 7; ++off) {
        const uint8_t* p = img.text.base + off;
        if (p[1] != 0x8D) continue;
        const bool rex = p[0] == 0x48 || p[0] == 0x4C;
        if (!rex || (p[2] & 0xC7) != 0x05) continue;
        if (rows::CheckedRip(img, p, 3, 7) != label) continue;
        uint8_t* fn = FunctionStart(img, p);
        if (fn && rows::InText(img, fn, rows::PatternBytes(kSyncPrologue)) && BytesMatch(fn, kSyncPrologue)) {
            if (found == fn) continue;   /* same function, multiple label leas */
            found = fn;
            ++hits;
        }
        off += 6;
    }
    if (hits == 0) return SigFail("no params-sync function references the module label");
    if (hits > 1) return SigFail("more than one function matches the sync prologue and label");
    return SigOk(found);
}
}

namespace sco::game_offline {
const uint8_t* OldFindLeaFrom(const Section& text, const uint8_t* target, const uint8_t* from) {
    if (!target || !rows::InRange(text, text.base, text.size) || text.size < 7 ||
        !rows::InRange(text, from, 7)) return nullptr;
    const uint8_t* const end = text.base + text.size - 6;
    for (const uint8_t* opcode = from + 1; opcode <= end; ++opcode) {
        opcode = static_cast<const uint8_t*>(memchr(opcode, 0x8D, static_cast<size_t>(end - opcode) + 1));
        if (!opcode) break;
        const uint8_t* p = opcode - 1;
        if ((p[0] != 0x48 && p[0] != 0x4C) || (p[2] & 0xC7) != 0x05) continue;
        const uint8_t* next = rows::RangeAt(text, p, 7, 0);
        const auto address = reinterpret_cast<uintptr_t>(next);
        const int32_t disp = Rel32(p + 3);
        const uintptr_t magnitude = disp < 0 ? uintptr_t{0} - static_cast<uintptr_t>(disp) : static_cast<uintptr_t>(disp);
        if (!(disp < 0 ? magnitude > address : magnitude > UINTPTR_MAX - address) &&
            (disp < 0 ? address - magnitude : address + magnitude) == reinterpret_cast<uintptr_t>(target)) return p;
    }
    return nullptr;
}
}

// A deliberately wrong copy of the new actor.adjust_stat scan (it forgets to skip a lea that starts
// inside the last one counted), for the self-check at the end.
namespace sco::game_actor_stats {
SigResult MutantResolveAdjustStat(const Image& img, const SigResolveContext&) {
    if (!Code(img, img.text.base, img.text.size)) return SigFail("invalid .text range");
    SigResult r = SigPattern(img.text, kAdjustPrologue);
    if (r.state != SigState::Ok) return r;
    if (!InImage(img, img.rdata.base, img.rdata.size)) return SigFail("invalid .rdata range");
    const uint8_t* label = FindCString(img.rdata, kAdjustLabel);
    if (!label) return SigFail("the AdjustStatValue label isn't in .rdata");
    if (!Code(img, img.text.base, img.text.size) || img.text.size < 7) return SigFail("no .text");
    int leas = 0;
    bool outside = false;
    ScanIndexRef index(img);
    index->ForEachRip(img.text, RipForm::AnyLea(), label, 0, ~size_t{0}, [&](size_t off) {
        if (FunctionStart(img, img.text.base + off) != r.at) { outside = true; return false; }
        ++leas;
        return true;
    });
    if (outside) return SigFail("an AdjustStatValue label lea is outside the matched function");
    if (!leas) return SigFail("the matched function doesn't load the AdjustStatValue label");
    return r;
}
}  // namespace sco::game_actor_stats

// ---- random buffers ----------------------------------------------------------------------------------

namespace {

struct Rng {
    uint64_t s;
    uint64_t next() {   // splitmix64
        uint64_t z = (s += 0x9E3779B97F4A7C15ull);
        z = (z ^ (z >> 30)) * 0xBF58476D1CE4E5B9ull;
        z = (z ^ (z >> 27)) * 0x94D049BB133111EBull;
        return z ^ (z >> 31);
    }
    size_t below(size_t n) { return n ? static_cast<size_t>(next() % n) : 0; }
    bool chance(int percent) { return static_cast<int>(below(100)) < percent; }
};

uint64_t g_cases = 0;   // randomized comparisons run, all helpers
uint64_t g_hits = 0;    // of them, those where the old helper found something (a non-null site, a non-zero count, state Ok)

const uint8_t kHot[] = { 0x48, 0x4C, 0x8D, 0x05, 0x0D, 0x15, 0x1D, 0x25, 0x2D, 0x35, 0x3D, 0x45, 0x85, 0x49, 0x40,
                         0x00, 0xFF, 0x8B, 0xE8 };
const uint8_t kModrm[] = { 0x05, 0x0D, 0x15, 0x1D, 0x25, 0x2D, 0x35, 0x3D };

const uint8_t* PtrAt(const uint8_t* base, int64_t off) {   // base + off; may point outside the buffer
    return reinterpret_cast<const uint8_t*>(reinterpret_cast<uintptr_t>(base) + static_cast<uintptr_t>(off));
}

// An image buffer with a .text range inside it, dense in lea-shaped byte runs.
struct Low {
    std::vector<uint8_t> mem;
    sco::Image img;
    std::vector<int64_t> pool;                      // target offsets worth asking for, from the image base
    std::vector<int64_t> planted;                   // the targets the planted sites were aimed at
    std::vector<std::array<uint8_t, 3>> forms;      // the three leading bytes of planted sites
    const uint8_t* Target(Rng& r) const {
        if (r.chance(6)) return PtrAt(img.base, static_cast<int64_t>(r.next() % 4096) - 2048);
        if (!planted.empty() && r.chance(65)) return PtrAt(img.base, planted[r.below(planted.size())]);
        return PtrAt(img.base, pool[r.below(pool.size())]);
    }
    const uint8_t* InText(Rng& r, int64_t slack) const {   // a pointer in or just around .text
        const int64_t tb = img.text.base - img.base;
        return PtrAt(img.base, tb - slack + static_cast<int64_t>(r.below(img.text.size + static_cast<size_t>(2 * slack) + 1)));
    }
};

void MakeLow(Rng& r, Low& L) {
    size_t S;
    switch (r.below(20)) {
        case 0: S = 1 + r.below(24); break;
        case 1: S = 400 + r.below(1200); break;
        default: S = 25 + r.below(376); break;
    }
    L.mem.assign(S, 0);
    for (auto& b : L.mem) b = r.chance(40) ? kHot[r.below(sizeof(kHot))] : static_cast<uint8_t>(r.next());
    L.img = sco::Image{};
    L.img.base = L.mem.data();
    L.img.size = static_cast<uint32_t>(S);
    size_t tb = r.chance(55) ? 0 : r.below(S);
    size_t ts = r.chance(65) ? S - tb : r.below(S - tb + 1);
    if (r.chance(5)) ts = std::min<size_t>(ts, 6);
    L.img.text = { L.mem.data() + tb, ts };
    // Target pool: random spots plus the edges of the image and of .text.
    const int64_t s64 = static_cast<int64_t>(S), tb64 = static_cast<int64_t>(tb), te64 = static_cast<int64_t>(tb + ts);
    L.pool = { -1, 0, s64 - 1, s64, s64 + 1, tb64, tb64 - 1, te64 - 1, te64, tb64 + 7, te64 - 7 };
    for (int i = 0, n = 2 + static_cast<int>(r.below(6)); i < n; ++i) L.pool.push_back(static_cast<int64_t>(r.below(S + 8)) - 4);
    // Plant sites: any position, both ends, overlapping the previous one.
    L.forms.clear();
    L.planted.clear();
    if (S >= 7 && !r.chance(15)) {
        int64_t prev = -1;
        const int64_t hot = L.pool[r.below(L.pool.size())];
        for (int i = 0, n = 2 + static_cast<int>(r.below(12)); i < n; ++i) {
            int64_t pos;
            const size_t pick = r.below(10);
            if (pick == 0) pos = 0;
            else if (pick == 1) pos = s64 - 7;
            else if (pick < 5 && prev >= 0) pos = prev + 1 + static_cast<int64_t>(r.below(6));   // starts inside the previous site
            else if (pick < 7) pos = tb64 + static_cast<int64_t>(r.below(ts + 1)) - 3;
            else pos = static_cast<int64_t>(r.below(S - 6));
            if (pos < 0 || pos + 7 > s64) continue;
            prev = pos;
            uint8_t* p = L.mem.data() + pos;
            p[0] = r.chance(85) ? (r.chance(60) ? 0x48 : 0x4C) : static_cast<uint8_t>(0x40 + r.below(16));
            p[1] = r.chance(92) ? 0x8D : (r.chance(50) ? 0x8B : static_cast<uint8_t>(r.next()));
            p[2] = r.chance(88) ? kModrm[r.below(sizeof(kModrm))] : static_cast<uint8_t>(r.next());
            const int64_t target = r.chance(50) ? hot : L.pool[r.below(L.pool.size())];   // half of them aim at one spot: several hits, in order
            int32_t disp = static_cast<int32_t>(target - (pos + 7));
            if (r.chance(15)) disp = static_cast<int32_t>(static_cast<int64_t>(r.below(2 * S + 16)) - static_cast<int64_t>(S) - 8);
            if (r.chance(2)) disp = static_cast<int32_t>(r.next());   // anywhere in 4 GiB
            std::memcpy(p + 3, &disp, 4);
            L.forms.push_back({ p[0], p[1], p[2] });
            L.planted.push_back(pos + 7 + disp);
        }
    }
}

// The three leading bytes of a query: usually those of a planted site, sometimes any lea shape, sometimes noise.
std::array<uint8_t, 3> PickForm(Rng& r, const Low& L) {
    const size_t pick = r.below(10);
    if (pick < 5 && !L.forms.empty()) return L.forms[r.below(L.forms.size())];
    if (pick < 8) return { r.chance(60) ? uint8_t{0x48} : uint8_t{0x4C}, 0x8D, kModrm[r.below(sizeof(kModrm))] };
    if (pick == 8) return { 0x48, 0x8B, 0x05 };
    return { static_cast<uint8_t>(r.next()), static_cast<uint8_t>(r.next()), static_cast<uint8_t>(r.next()) };
}

// The index situations a helper can run under: built on first use and reused by every query of the
// case, absent (direct walk), or capped below the .text size (fallback walk).
struct Modes {
    sco::ScanIndex indexed;
    sco::ScanIndex capped;
    sco::ScanIndex* Pick(Rng& r, sco::Image& img) {
        switch (r.below(8)) {
            case 0: img.index = nullptr; break;
            case 1: img.index = &capped; break;
            default: img.index = &indexed; break;
        }
        return img.index;
    }
    explicit Modes(size_t cap) : capped(cap) {}
};

// ---- the helpers under test ---------------------------------------------------------------------------
// Each Run* returns the number of disagreements between the old loop and the helper. The function
// pointers default to the shipped helpers; the self-checks pass mutants.

using LeaToFn = const uint8_t* (*)(const sco::Image&, const uint8_t*, const uint8_t*);
using RipRefsFn = int (*)(const sco::Image&, const sco::Section&, uint8_t, uint8_t, uint8_t, const uint8_t*, const uint8_t**, int);

const uint8_t* NewLeaTo(const sco::Image& img, const uint8_t* t, const uint8_t* f) { return sco::game_contracts::LeaTo(img, t, f); }
int NewEntitiesRipRefs(const sco::Image& img, const sco::Section& text, uint8_t a, uint8_t b, uint8_t c, const uint8_t* t,
                       const uint8_t** out, int max) { return sco::game_entities::RipRefs(img, text, a, b, c, t, out, max); }
int NewEventsLeaRefs(const sco::Image& img, const sco::Section& text, uint8_t a, uint8_t b, uint8_t c, const uint8_t* t,
                     const uint8_t** out, int max) { return sco::game_events::LeaRefs(img, text, a, b, c, t, out, max); }
int NewActorsRipRefs(const sco::Image& img, const sco::Section&, uint8_t a, uint8_t b, uint8_t c, const uint8_t* t,
                     const uint8_t** out, int max) { return sco::game_actors::RipRefs(img, a, b, c, t, out, max); }

// Mutants: LeaTo steps over a site that starts exactly at `from`; RipRefs forgets the REX byte filter.
const uint8_t* MutantLeaTo(const sco::Image& img, const uint8_t* t, const uint8_t* f) {
    const uint8_t* r = sco::game_contracts::LeaTo(img, t, f);
    if (r && r == f) return sco::game_contracts::LeaTo(img, t, f + 1);
    return r;
}
int MutantEntitiesRipRefs(const sco::Image& img, const sco::Section& text, uint8_t, uint8_t, uint8_t c, const uint8_t* t,
                          const uint8_t** out, int max) {
    return sco::game_entities::RipRefs(img, text, 0x48, 0x8D, c, t, out, max);
}

// LeaTo over random images: single lookups, and the resolvers' walk (each next lookup starts at the
// previous site + 2 or + 7); returns the number of disagreements.
uint64_t RunLeaTo(Rng& r, uint64_t cases, LeaToFn fn) {
    uint64_t bad = 0;
    for (uint64_t c = 0; c < cases; ++c) {
        Low L; MakeLow(r, L);
        Modes m(L.img.text.size / 2);
        m.Pick(r, L.img);
        for (int q = 0, n = 1 + static_cast<int>(r.below(3)); q < n; ++q) {
            const uint8_t* target = L.Target(r);
            const uint8_t* from = r.chance(25) ? nullptr : L.InText(r, 3);
            ++g_cases;
            const uint8_t* single = sco::game_contracts::OldLeaTo(L.img, target, from);
            if (single) ++g_hits;
            if (single != fn(L.img, target, from)) ++bad;
            const uint8_t* a = sco::game_contracts::OldLeaTo(L.img, target, nullptr);
            const uint8_t* b = fn(L.img, target, nullptr);
            for (int step = 0; step < 40 && (a || b); ++step) {
                ++g_cases;
                if (a != b) { ++bad; break; }
                const ptrdiff_t adv = (step & 1) ? 7 : 2;
                a = sco::game_contracts::OldLeaTo(L.img, target, sco::game::rows::TextAt(L.img, a, adv, 0));
                b = fn(L.img, target, sco::game::rows::TextAt(L.img, b, adv, 0));
            }
        }
    }
    return bad;
}

uint64_t RunExactLeaTo(Rng& r, uint64_t cases) {
    uint64_t bad = 0;
    for (uint64_t c = 0; c < cases; ++c) {
        Low L; MakeLow(r, L);
        Modes m(L.img.text.size / 2);
        m.Pick(r, L.img);
        const uint8_t* target = L.Target(r);
        ++g_cases;
        const uint8_t* old = sco::game_contracts::OldExactLeaTo(L.img, target);
        if (old) ++g_hits;
        if (old != sco::game_contracts::ExactLeaTo(L.img, target)) ++bad;
    }
    return bad;
}

uint64_t RunRipRefs(Rng& r, uint64_t cases, RipRefsFn fn, int which /*0 entities, 1 events, 2 actors*/) {
    uint64_t bad = 0;
    for (uint64_t c = 0; c < cases; ++c) {
        Low L; MakeLow(r, L);
        Modes m(L.img.text.size / 2);
        m.Pick(r, L.img);
        for (int q = 0, n = 1 + static_cast<int>(r.below(3)); q < n; ++q) {
            const auto f = PickForm(r, L);
            const uint8_t* target = L.Target(r);
            const int max = static_cast<int>(r.below(5));   // 0..4 kept, the total still counted
            const uint8_t* a[8] = {}; const uint8_t* b[8] = {};
            int na = 0;
            const sco::Section text = L.img.text;
            if (which == 0) na = sco::game_entities::OldRipRefs(text, f[0], f[1], f[2], target, a, max);
            else if (which == 1) na = sco::game_events::OldLeaRefs(text, f[0], f[1], f[2], target, a, max);
            else na = sco::game_actors::OldRipRefs(L.img, f[0], f[1], f[2], target, a, max);
            const int nb = fn(L.img, text, f[0], f[1], f[2], target, b, max);
            ++g_cases;
            if (na) ++g_hits;
            if (na != nb) { ++bad; continue; }
            for (int i = 0; i < na && i < max; ++i) if (a[i] != b[i]) { ++bad; break; }
        }
    }
    return bad;
}

// world's RipRefs takes lead/tail window bounds and an `ok` predicate.
uint64_t RunWorldRipRefs(Rng& r, uint64_t cases) {
    uint64_t bad = 0;
    for (uint64_t c = 0; c < cases; ++c) {
        Low L; MakeLow(r, L);
        Modes m(L.img.text.size / 2);
        m.Pick(r, L.img);
        for (int q = 0, n = 1 + static_cast<int>(r.below(3)); q < n; ++q) {
            auto f = PickForm(r, L);
            const uint8_t* target = L.Target(r);
            const size_t ts = L.img.text.size;
            const size_t leads[] = { 0, 0xF, 0x13, 7, 0x20, 0x4B, r.below(ts + 4) };
            const size_t tails[] = { 7, 0x20, 0x30, 8, 6, 7 + r.below(ts + 4) };
            const size_t lead = r.chance(50) ? 0 : leads[r.below(std::size(leads))], tail = r.chance(50) ? 7 : tails[r.below(std::size(tails))];
            const int max = static_cast<int>(r.below(5));
            const unsigned okMode = static_cast<unsigned>(r.below(3));
            auto ok = [okMode](const uint8_t* p) { return okMode == 0 ? true : okMode == 1 ? (p[4] & 1) != 0 : (p[5] & 3) == 0; };
            uint8_t* a[8] = {}; uint8_t* b[8] = {};
            const int na = sco::game_world::OldRipRefs(L.img, f[1], f[2], target, lead, tail, a, max, ok);
            const int nb = sco::game_world::RipRefs(L.img, f[1], f[2], target, lead, tail, b, max, ok);
            ++g_cases;
            if (na) ++g_hits;
            if (na != nb) { ++bad; continue; }
            for (int i = 0; i < na && i < max; ++i) if (a[i] != b[i]) { ++bad; break; }
        }
    }
    return bad;
}

uint64_t RunFindLeaFrom(Rng& r, uint64_t cases) {
    uint64_t bad = 0;
    for (uint64_t c = 0; c < cases; ++c) {
        Low L; MakeLow(r, L);
        Modes m(L.img.text.size / 2);
        m.Pick(r, L.img);
        const uint8_t* target = r.chance(3) ? nullptr : L.Target(r);
        const uint8_t* from = r.chance(20) ? L.img.text.base : L.InText(r, 3);
        const sco::Section text = L.img.text;
        ++g_cases;
        const uint8_t* single = sco::game_offline::OldFindLeaFrom(text, target, from);
        if (single) ++g_hits;
        if (single != sco::game_offline::FindLeaFrom(L.img, text, target, from)) ++bad;
        // The resolvers' own walk: every reference, next = site + 7.
        const uint8_t* a = sco::game_offline::OldFindLeaFrom(text, target, text.base);
        const uint8_t* b = sco::game_offline::FindLeaFrom(L.img, text, target, text.base);
        for (int step = 0; step < 40 && (a || b); ++step) {
            ++g_cases;
            if (a != b) { ++bad; break; }
            a = sco::game_offline::OldFindLeaFrom(text, target, sco::game::rows::TextAt(L.img, a, 7, 0));
            b = sco::game_offline::FindLeaFrom(L.img, text, target, sco::game::rows::TextAt(L.img, b, 7, 0));
        }
    }
    return bad;
}

// The index itself against a plain scan of every form and window.
uint64_t RunIndexPrimitive(Rng& r, uint64_t cases) {
    uint64_t bad = 0;
    for (uint64_t c = 0; c < cases; ++c) {
        Low L; MakeLow(r, L);
        Modes m(L.img.text.size / 2);
        sco::ScanIndex* ix = m.Pick(r, L.img);
        sco::ScanIndex direct(0);
        if (!ix) ix = &direct;
        const sco::Section text = L.img.text;
        for (int q = 0, n = 1 + static_cast<int>(r.below(4)); q < n; ++q) {
            sco::RipForm form;
            if (r.chance(50)) form = sco::RipForm::AnyLea();
            else { const auto f = PickForm(r, L); form = sco::RipForm::Exact(f[0], f[1], f[2]); }
            const uint8_t* target = L.Target(r);
            const size_t first = r.chance(50) ? 0 : r.below(text.size + 3);
            const size_t last = r.chance(50) ? ~size_t{0} : r.below(text.size + 3);
            std::vector<size_t> want, got;
            if (text.base && text.size >= 7) {
                const size_t hi = std::min(last, text.size - 7);
                for (size_t off = first; off <= hi; ++off) {
                    const uint8_t* p = text.base + off;
                    if ((p[0] & form.mask0) != form.b0 || p[1] != form.b1 || (p[2] & form.mask2) != form.b2) continue;
                    // The plain scan, in integers: no pointer wraps in a small buffer.
                    const int64_t dest = static_cast<int64_t>(reinterpret_cast<uintptr_t>(p)) + 7 + sco::Rel32(p + 3);
                    if (dest == static_cast<int64_t>(reinterpret_cast<uintptr_t>(target))) want.push_back(off);
                }
            }
            ix->ForEachRip(text, form, target, first, last, [&](size_t off) { got.push_back(off); return true; });
            ++g_cases;
            if (want != got) ++bad;
            if (!want.empty()) ++g_hits;
            if (last == ~size_t{0}) {   // FindRip is the first of them
                const size_t one = ix->FindRip(text, form, target, first);
                if (one != (want.empty() ? sco::ScanIndex::kNone : want.front())) ++bad;
            }
        }
    }
    return bad;
}

}  // namespace

// ---- whole resolvers on structured images -------------------------------------------------------------
// Resolvers take a SigResolveContext only a registry can make, so a one-row registry calls a driver
// that runs old and new resolvers inside the pass (with the pass's index). The image is sparse: one
// 96 MiB buffer reused by every case, so a lea can reach a label ~93 MB away, which is what lets
// two label references overlap (the second starts four bytes into the first).

namespace {

constexpr size_t kBig = 0x06000000;
constexpr size_t kTextRva = 0x1000, kRdataRva = 0x10000, kPdataRva = 0x20000, kUnwindRva = 0x30000;

struct Job {
    void (*run)(const sco::Image&, const sco::SigResolveContext&, void*) = nullptr;
    void* arg = nullptr;
};
Job g_job;

sco::SigResult Driver(const sco::Image& img, const sco::SigResolveContext& ctx) {
    g_job.run(img, ctx, g_job.arg);
    return sco::SigOk(img.base);
}

struct Harness {
    sco::SignatureRegistry registry;
    sco::SigDef row{ "driver", nullptr, 0, 0, Driver, {} };
    sco::SignatureTable table{ &row, 1 };
    Harness() {
        CHECK(registry.RegisterTables(&table, 1).status == sco::SigRegistryStatus::Ok);
        CHECK(registry.Freeze() == sco::SigRegistryStatus::Ok);
    }
    template <class F>
    void Run(const sco::Image& img, F&& f) {
        g_job.run = [](const sco::Image& i, const sco::SigResolveContext& c, void* a) { (*static_cast<F*>(a))(i, c); };
        g_job.arg = &f;
        CHECK(registry.Resolve(img) == sco::SigRegistryStatus::Ok);
    }
};

bool SameResult(const sco::SigResult& a, const sco::SigResult& b) {
    return a.state == b.state && a.at == b.at && a.matches == b.matches &&
           ((!a.why && !b.why) || (a.why && b.why && std::strcmp(a.why, b.why) == 0));
}

struct Big {
    std::vector<uint8_t> mem = std::vector<uint8_t>(kBig, 0);
    std::vector<std::pair<size_t, size_t>> dirty;
    void Put(size_t rva, const void* data, size_t n) {
        std::memcpy(mem.data() + rva, data, n);
        dirty.push_back({ rva, n });
    }
    void Clear() {
        for (const auto& d : dirty) std::memset(mem.data() + d.first, 0, d.second);
        dirty.clear();
    }
};

// Pattern text ("48 8B ?? 05") as concrete bytes, wildcards random.
std::vector<uint8_t> Concrete(Rng& r, const char* pattern) {
    std::vector<uint8_t> out;
    for (const char* c = pattern; *c; ) {
        if (*c == ' ') { ++c; continue; }
        if (c[0] == '?') { out.push_back(static_cast<uint8_t>(r.next())); c += (c[1] == '?') ? 2 : 1; continue; }
        char hex[3] = { c[0], c[1], 0 };
        out.push_back(static_cast<uint8_t>(std::strtoul(hex, nullptr, 16)));
        c += 2;
    }
    return out;
}

// .text of noise cut into functions with a .pdata entry each (one in ten has none).
struct Funcs {
    std::vector<uint8_t> tx;
    std::vector<size_t> starts;
    size_t T = 0, nPdata = 0;
    size_t FuncOf(size_t pos) const { size_t f = 0; while (f + 1 < starts.size() && starts[f + 1] <= pos) ++f; return f; }
    size_t End(size_t f) const { return f + 1 < starts.size() ? starts[f + 1] : T; }
};

void MakeFuncs(Rng& r, Big& big, Funcs& F, size_t minT, size_t maxT) {
    F.T = minT + r.below(maxT - minT + 1);
    F.tx.resize(F.T);
    for (auto& b : F.tx) b = r.chance(35) ? kHot[r.below(sizeof(kHot))] : static_cast<uint8_t>(r.next());
    F.starts = { 0 };
    for (size_t at = 64 + r.below(300); at + 64 < F.T; at += 64 + r.below(400)) F.starts.push_back(at);
    std::vector<uint32_t> pdata;
    for (size_t f = 0; f < F.starts.size(); ++f) {
        if (r.chance(10)) continue;
        pdata.push_back(static_cast<uint32_t>(kTextRva + F.starts[f]));
        pdata.push_back(static_cast<uint32_t>(kTextRva + F.End(f)));
        pdata.push_back(static_cast<uint32_t>(kUnwindRva));
    }
    F.nPdata = pdata.size() / 3;
    if (!pdata.empty()) big.Put(kPdataRva, pdata.data(), pdata.size() * 4);
    const uint8_t unwind[4] = { 0x01, 0, 0, 0 };   // version 1, no flags, no codes: not chained
    big.Put(kUnwindRva, unwind, sizeof(unwind));
}

void PlantLea(Rng& r, Funcs& F, size_t pos, size_t targetRva, uint8_t modrm) {
    if (pos + 7 > F.T) return;
    uint8_t* p = F.tx.data() + pos;
    p[0] = r.chance(55) ? 0x48 : (r.chance(80) ? 0x4C : static_cast<uint8_t>(0x40 + r.below(16)));
    p[1] = r.chance(95) ? 0x8D : 0x8B;
    p[2] = modrm;
    const int32_t disp = static_cast<int32_t>(static_cast<int64_t>(targetRva) - static_cast<int64_t>(kTextRva + pos + 7));
    std::memcpy(p + 3, &disp, 4);
}

sco::Image CommitImage(Big& big, const Funcs& F, size_t rdataBytes) {
    big.Put(kTextRva, F.tx.data(), F.T);
    sco::Image img;
    img.base = big.mem.data();
    img.size = static_cast<uint32_t>(kBig);
    img.timestamp = 1;
    img.text = { big.mem.data() + kTextRva, F.T };
    img.rdata = { big.mem.data() + kRdataRva, rdataBytes };
    img.pdata = { big.mem.data() + kPdataRva, 12 * F.nPdata };   // the entries MakeFuncs wrote
    return img;
}

// Label references for the lea scans: where the label lives (normal: in a small .rdata; overlap:
// ~93 MB past the first reference so a second reference can start four bytes into it).
struct LabelPlan {
    size_t rva = 0;
    size_t rdataBytes = 0x1000;
    size_t overlapAt = ~size_t{0};   // text offset of the first of two overlapping references
};

LabelPlan PlanLabel(Rng& r, const Funcs& F, size_t len, bool overlap, size_t boundary) {
    LabelPlan plan;
    if (!overlap || F.T < 64) {
        plan.rva = kRdataRva + r.below(0x300);
        return plan;
    }
    plan.overlapAt = 8 + r.below(F.T - 24);
    // Often put a function boundary between the two references, so the second one is in another function.
    if (boundary != ~size_t{0} && boundary > 12 && boundary + 16 < F.T && r.chance(70)) plan.overlapAt = boundary - 1 - r.below(4);
    const uint32_t d0 = static_cast<uint32_t>(r.below(256));
    const int64_t disp = 0x058D4800 + d0;   // bytes: d0 48 8D 05 = a second `48 8D 05` at +4
    plan.rva = kTextRva + plan.overlapAt + 7 + static_cast<size_t>(disp);
    plan.rdataBytes = plan.rva + len + 16 - kRdataRva;
    return plan;
}

void PlantOverlap(Rng& r, Funcs& F, const LabelPlan& plan) {
    if (plan.overlapAt == ~size_t{0} || plan.overlapAt + 12 > F.T) return;
    uint8_t* p = F.tx.data() + plan.overlapAt;
    const int64_t disp = static_cast<int64_t>(plan.rva) - static_cast<int64_t>(kTextRva + plan.overlapAt + 7);
    p[0] = r.chance(50) ? 0x48 : 0x4C; p[1] = 0x8D; p[2] = kModrm[r.below(sizeof(kModrm))];
    const int32_t d = static_cast<int32_t>(disp);
    std::memcpy(p + 3, &d, 4);                       // d0 48 8D 05: the second reference begins at +4
    const int32_t d2 = static_cast<int32_t>(static_cast<int64_t>(plan.rva) - static_cast<int64_t>(kTextRva + plan.overlapAt + 4 + 7));
    p[7] = 0; p[8] = 0; p[9] = 0; p[10] = 0;
    std::memcpy(p + 7, &d2, 4);
    if (r.chance(40) && plan.overlapAt + 16 <= F.T) PlantLea(r, F, plan.overlapAt + 4 + 3 + 5, plan.rva, 0x05);   // and a third one
}

// ---- actor.adjust_stat and gamerules.params_sync: lea scans with the +7 skip -------------------------

enum class Skip { Adjust, Params };

using ResolveFn = sco::SigResult (*)(const sco::Image&, const sco::SigResolveContext&);

uint64_t RunSkipScans(Rng& r, Big& big, Harness& h, uint64_t cases, Skip which, ResolveFn newFn) {
    uint64_t bad = 0;
    for (uint64_t c = 0; c < cases; ++c) {
        const char* prologue = which == Skip::Adjust ? sco::game_actor_stats::kAdjustPrologue : sco::game_gamerules::kSyncPrologue;
        const char* label = which == Skip::Adjust ? sco::game_actor_stats::kAdjustLabel : sco::game_gamerules::kParamsLabel;
        const size_t len = std::strlen(label) + 1;
        Funcs F;
        MakeFuncs(r, big, F, 0x300, 0x1800);
        const size_t home = r.below(F.starts.size());
        const bool overlap = r.chance(8);
        const LabelPlan plan = PlanLabel(r, F, len, overlap, home + 1 < F.starts.size() && r.chance(70) ? F.starts[home + 1] : F.starts[r.below(F.starts.size())]);
        big.Put(plan.rva, label, len);
        if (!overlap && r.chance(10)) big.Put(kRdataRva + 0x500, label, len);   // a second label copy later in .rdata
        // Prologue at the start of one or two functions (adjust_stat needs it unique in .text).
        const size_t copies = r.chance(75) ? 1 : r.chance(40) ? 0 : 2;
        // The references: some anywhere, some all inside the prologue's function.
        const bool inside = r.chance(60);
        for (size_t i = 0, n = r.below(6); i < n; ++i) {
            const size_t lo = inside ? F.starts[home] : 0, hi = inside ? F.End(home) : F.T;
            if (hi < lo + 8) continue;
            const size_t pos = (r.chance(10) ? 0 : r.chance(10) ? hi - 7 : lo + r.below(hi - lo - 7 + 1));
            PlantLea(r, F, pos, plan.rva, kModrm[r.below(sizeof(kModrm))]);
        }
        PlantOverlap(r, F, plan);
        std::vector<size_t> at;
        for (size_t k = 0; k < copies; ++k) at.push_back(k == 0 ? home : r.below(F.starts.size()));
        for (size_t f : at) {
            const auto bytes = Concrete(r, prologue);
            if (F.starts[f] + bytes.size() <= F.T) std::memcpy(F.tx.data() + F.starts[f], bytes.data(), bytes.size());
        }
        const sco::Image img = CommitImage(big, F, plan.rdataBytes);
        sco::SigResult oldR, newR, plainR;
        h.Run(img, [&](const sco::Image& inPass, const sco::SigResolveContext& ctx) {
            sco::Image plain = inPass; plain.index = nullptr;
            if (which == Skip::Adjust) {
                oldR = sco::game_actor_stats::OldResolveAdjustStat(plain, ctx);
                newR = newFn(inPass, ctx);
                plainR = newFn(plain, ctx);
            } else {
                oldR = sco::game_gamerules::OldResolveParamsSync(plain, ctx);
                newR = newFn(inPass, ctx);
                plainR = newFn(plain, ctx);
            }
        });
        ++g_cases;
        if (oldR.state == sco::SigState::Ok) ++g_hits;
        if (!SameResult(oldR, newR) || !SameResult(oldR, plainR)) ++bad;
        big.Clear();
    }
    return bad;
}

// ---- spawn.toggle_flight_ready ------------------------------------------------------------------------

uint64_t RunFlightReady(Rng& r, Big& big, Harness& h, uint64_t cases) {
    static const char kHeader[] = "C:\\workspace\\CryEngine\\Code\\CryEngine\\CryCommon\\Events/ISC/Dashboards.h";
    uint64_t bad = 0;
    for (uint64_t c = 0; c < cases; ++c) {
        Funcs F;
        MakeFuncs(r, big, F, 0x1F0 + r.below(0x40), 0x1800);
        const size_t headerRva = kRdataRva + r.below(0x300);
        big.Put(headerRva, kHeader, sizeof(kHeader));
        // Sender blocks: CC CC, prologue, the register moves, the line number, then the header lea.
        for (size_t b = 0, n = r.below(4); b < n; ++b) {
            std::vector<uint8_t> blk = { 0xCC, 0xCC, 0x48, 0x89, 0x5C, 0x24, 0x08 };
            const auto add = [&](std::initializer_list<uint8_t> bytes) { blk.insert(blk.end(), bytes); };
            for (size_t i = r.below(6); i > 0; --i) blk.push_back(static_cast<uint8_t>(r.next()));
            if (r.chance(90)) add({ 0x48, 0x8B, 0xFA });
            for (size_t i = r.below(5); i > 0; --i) blk.push_back(static_cast<uint8_t>(r.next()));
            if (r.chance(90)) add({ 0x4C, 0x89, 0x44, 0x24, 0x20 });
            const uint32_t line = r.chance(85) ? 0x49 : static_cast<uint32_t>(r.below(0x60));
            if (r.chance(95)) { add({ 0x41, 0xB8 }); for (int k = 0; k < 4; ++k) blk.push_back(static_cast<uint8_t>(line >> (8 * k))); }
            for (size_t i = r.below(8); i > 0; --i) blk.push_back(static_cast<uint8_t>(r.next()));
            const size_t leaAt = blk.size();
            blk.resize(leaAt + 7);
            if (F.T < blk.size() + 4) continue;
            // Anywhere, with the lea's opcode byte at or around the old scan's first position, 0x200.
            size_t pos = r.below(F.T - blk.size() + 1);
            if (r.chance(35) && leaAt <= 0x1FE) pos = 0x1FE + r.below(4) - leaAt;   // the lea at 0x1FE..0x201: the old scan began at 0x200
            if (pos + blk.size() > F.T) pos = F.T - blk.size();
            std::memcpy(F.tx.data() + pos, blk.data(), leaAt);
            PlantLea(r, F, pos + leaAt, headerRva, 0x15);
            F.tx[pos + leaAt] = 0x48;
            if (r.chance(8)) F.tx[pos + leaAt + 2] = 0x0D;   // another register: not a Dashboards.h reference
        }
        const sco::Image img = CommitImage(big, F, 0x1000);
        sco::SigResult oldR, newR, plainR;
        h.Run(img, [&](const sco::Image& inPass, const sco::SigResolveContext& ctx) {
            sco::Image plain = inPass; plain.index = nullptr;
            oldR = sco::game_actors::OldResolveFlightReady(plain, ctx);
            newR = sco::game_actors::ResolveFlightReady(inPass, ctx);
            plainR = sco::game_actors::ResolveFlightReady(plain, ctx);
        });
        ++g_cases;
        if (oldR.state == sco::SigState::Ok) ++g_hits;
        if (!SameResult(oldR, newR) || !SameResult(oldR, plainR)) ++bad;
        big.Clear();
    }
    return bad;
}

// ---- the K-row families: one scan per pass, same answer for every K ----------------------------------

template <int... Ks>
void SocialRows(const sco::Image& img, const sco::SigResolveContext& ctx, sco::SigResult* out, std::integer_sequence<int, Ks...>) {
    ((out[Ks - 1] = sco::game_offline::ResolveSocialGroup<Ks>(img, ctx)), ...);
}
template <int... Ks>
void HandshakeRows(const sco::Image& img, const sco::SigResolveContext& ctx, sco::SigResult* out, std::integer_sequence<int, Ks...>) {
    ((out[Ks - 1] = sco::game_offline::ResolveHandshake<Ks>(img, ctx)), ...);
}
template <int... Ks>
void OfflineDbRows(const sco::Image& img, const sco::SigResolveContext& ctx, sco::SigResult* out, std::integer_sequence<int, Ks...>) {
    ((out[Ks - 1] = sco::game_offline::ResolveOfflineDb<Ks>(img, ctx)), ...);
}
template <int N> constexpr auto Seq1() {   // 1..N
    return []<int... I>(std::integer_sequence<int, I...>) { return std::integer_sequence<int, (I + 1)...>{}; }(std::make_integer_sequence<int, N>{});
}

uint64_t RunFamilies(Rng& r, Big& big, Harness& h, uint64_t cases) {
    using namespace sco::game::offline;
    static const char kGuid[] = "sessionGuid", kDb[] = "Libs/OfflineDB";
    uint64_t bad = 0;
    for (uint64_t c = 0; c < cases; ++c) {
        Funcs F;
        MakeFuncs(r, big, F, 0x2800, 0x4000);
        const size_t guidRva = kRdataRva + 0x10 + r.below(0x100), dbRva = kRdataRva + 0x200 + r.below(0x100);
        if (r.chance(92)) big.Put(guidRva, kGuid, sizeof(kGuid));
        if (r.chance(92)) big.Put(dbRva, kDb, sizeof(kDb));
        const size_t globalA = kRdataRva + 0x600, globalB = kRdataRva + 0x640, flagA = kRdataRva + 0x700, flagB = kRdataRva + 0x740;
        const auto put = [&](size_t pos, const std::vector<uint8_t>& bytes) {
            if (pos + bytes.size() <= F.T) std::memcpy(F.tx.data() + pos, bytes.data(), bytes.size());
        };
        const auto rel = [&](std::vector<uint8_t>& v, size_t at, size_t pos, size_t targetRva) {   // disp32 of the 7-byte insn at pos
            const int32_t d = static_cast<int32_t>(static_cast<int64_t>(targetRva) - static_cast<int64_t>(kTextRva + pos + at + 7));
            std::memcpy(v.data() + at + 3, &d, 4);
        };
        size_t cursor = 8;
        const auto slot = [&](size_t span) { const size_t p = cursor + r.below(24); cursor = p + span; return p; };
        // Social-group blocks.
        const size_t social = r.chance(60) ? kSocialGroupSites : r.chance(50) ? kSocialGroupSites - 1 : r.below(kSocialGroupSites + 3);
        for (size_t i = 0; i < social; ++i) {
            std::vector<uint8_t> v = { 0x48, 0x8B, 0x0D, 0, 0, 0, 0 };
            const size_t pos = slot(0xA0);
            rel(v, 0, pos, r.chance(97) || i == 0 ? globalA : globalB);
            for (size_t k = r.below(0x20); k > 0; --k) v.push_back(static_cast<uint8_t>(r.next()));
            if (r.chance(94)) v.insert(v.end(), { 0x48, 0x8B, 0x01, 0xFF, 0x50, 0x18 }); else v.insert(v.end(), 6, 0x90);
            for (size_t k = r.below(0x10); k > 0; --k) v.push_back(0x90);
            if (r.chance(50)) v.insert(v.end(), { static_cast<uint8_t>(r.chance(50) ? 0x48 : 0x4C), 0x8B, static_cast<uint8_t>(0x40 + r.below(0x40)), 0x70 });
            else v.insert(v.end(), { 0xFF, static_cast<uint8_t>(0x50 + r.below(8)), 0x70 });
            for (size_t k = r.below(0x20); k > 0; --k) v.push_back(0x90);
            if (r.chance(50)) v.insert(v.end(), { static_cast<uint8_t>(r.chance(50) ? 0x48 : 0x4C), 0x8B, static_cast<uint8_t>(0x80 + r.below(0x40)), 0xB0, 0, 0, 0 });
            else v.insert(v.end(), { 0xFF, static_cast<uint8_t>(0x90 + r.below(8)), 0xB0, 0, 0, 0 });
            if (r.chance(6)) v[v.size() - 3] = 1;   // not slot 0xB0
            put(pos, v);
        }
        // Handshake gates.
        const size_t gates = r.chance(65) ? kHandshakeSites : r.below(kHandshakeSites + 3);
        for (size_t i = 0; i < gates; ++i) {
            std::vector<uint8_t> v = { 0x44, 0x38, kModrm[r.below(sizeof(kModrm))], 0, 0, 0, 0, 0x0F, 0x84, 1, 2, 3, 4 };
            const size_t pos = slot(0x40);
            rel(v, 0, pos, r.chance(96) ? flagA : flagB);
            for (size_t k = r.below(12); k > 0; --k) v.push_back(static_cast<uint8_t>(r.chance(50) ? 0x90 : r.next()));
            const size_t lea = v.size();
            v.insert(v.end(), { 0x48, 0x8D, 0x15, 0, 0, 0, 0 });
            rel(v, lea, pos, guidRva);
            if (r.chance(5)) v[lea + 1] = 0x8B;
            put(pos, v);
        }
        // Libs/OfflineDB references.
        const size_t dbs = r.chance(65) ? kOfflineDbSites : r.below(kOfflineDbSites + 3);
        for (size_t i = 0; i < dbs; ++i) {
            std::vector<uint8_t> v = { 0x4C, 0x8D, 0x05, 0, 0, 0, 0 };
            const size_t pos = slot(8);
            rel(v, 0, pos, dbRva);
            put(pos, v);
        }
        const sco::Image img = CommitImage(big, F, 0x1000);
        sco::SigResult withIdx[kSocialGroupSites] = {}, noIdx[kSocialGroupSites] = {};
        h.Run(img, [&](const sco::Image& inPass, const sco::SigResolveContext& ctx) {
            sco::Image plain = inPass; plain.index = nullptr;
            SocialRows(inPass, ctx, withIdx, Seq1<kSocialGroupSites>());
            SocialRows(plain, ctx, noIdx, Seq1<kSocialGroupSites>());
        });
        for (int k = 0; k < kSocialGroupSites; ++k) { ++g_cases; if (withIdx[k].state == sco::SigState::Ok) ++g_hits; if (!SameResult(withIdx[k], noIdx[k])) ++bad; }
        sco::SigResult hw[4] = {}, hn[4] = {}, dw[2] = {}, dn[2] = {};
        h.Run(img, [&](const sco::Image& inPass, const sco::SigResolveContext& ctx) {
            sco::Image plain = inPass; plain.index = nullptr;
            HandshakeRows(inPass, ctx, hw, Seq1<kHandshakeSites>());
            HandshakeRows(plain, ctx, hn, Seq1<kHandshakeSites>());
            OfflineDbRows(inPass, ctx, dw, Seq1<kOfflineDbSites>());
            OfflineDbRows(plain, ctx, dn, Seq1<kOfflineDbSites>());
        });
        for (int k = 0; k < kHandshakeSites; ++k) { ++g_cases; if (!SameResult(hw[k], hn[k])) ++bad; }
        for (int k = 0; k < kOfflineDbSites; ++k) { ++g_cases; if (!SameResult(dw[k], dn[k])) ++bad; }
        big.Clear();
    }
    return bad;
}

}  // namespace

// ---- main ---------------------------------------------------------------------------------------------

int main() {
    Rng r{ 0x5C0DE2410001ull };
    uint64_t hitsBefore = 0;
    const auto report = [&](const char* name, uint64_t images, uint64_t before, uint64_t bad) {
        std::printf("%-34s %9llu images %10llu comparisons %9llu non-empty  mismatches %llu\n", name,
                    static_cast<unsigned long long>(images), static_cast<unsigned long long>(g_cases - before),
                    static_cast<unsigned long long>(g_hits - hitsBefore), static_cast<unsigned long long>(bad));
        hitsBefore = g_hits;
        CHECK(bad == 0);
    };
    uint64_t before;

    before = g_cases; report("contracts LeaTo", 90000, before, RunLeaTo(r, 90000, NewLeaTo));
    before = g_cases; report("contracts ExactLeaTo", 60000, before, RunExactLeaTo(r, 60000));
    before = g_cases; report("entities RipRefs", 150000, before, RunRipRefs(r, 150000, NewEntitiesRipRefs, 0));
    before = g_cases; report("events LeaRefs", 100000, before, RunRipRefs(r, 100000, NewEventsLeaRefs, 1));
    before = g_cases; report("actors RipRefs", 150000, before, RunRipRefs(r, 150000, NewActorsRipRefs, 2));
    before = g_cases; report("world RipRefs (lead/tail/ok)", 150000, before, RunWorldRipRefs(r, 150000));
    before = g_cases; report("offline FindLeaFrom", 90000, before, RunFindLeaFrom(r, 90000));
    before = g_cases; report("ScanIndex::ForEachRip/FindRip", 150000, before, RunIndexPrimitive(r, 150000));

    {
        Big big;
        Harness h;
        before = g_cases; report("actor.adjust_stat scan", 1500, before,
                                 RunSkipScans(r, big, h, 1500, Skip::Adjust, sco::game_actor_stats::ResolveAdjustStat));
        before = g_cases; report("gamerules.params_sync scan", 1500, before,
                                 RunSkipScans(r, big, h, 1500, Skip::Params, sco::game_gamerules::ResolveParamsSync));
        before = g_cases; report("spawn.toggle_flight_ready", 6000, before, RunFlightReady(r, big, h, 6000));
        before = g_cases; report("social/handshake/offline_db families", 1200, before, RunFamilies(r, big, h, 1200));

        // The registry hands resolvers an image that carries the pass's index.
        bool sawIndex = false;
        sco::Image probe;
        probe.base = big.mem.data(); probe.size = static_cast<uint32_t>(kBig);
        h.Run(probe, [&](const sco::Image& inPass, const sco::SigResolveContext&) { sawIndex = inPass.index != nullptr; });
        CHECK(sawIndex);
        CHECK(probe.index == nullptr);   // the caller's image is untouched

        // ---- self-check: mutated "new" variants must be caught by the same comparisons ----------------
        Rng m{ 0x5C0DE2410002ull };
        const uint64_t badLea = RunLeaTo(m, 20000, MutantLeaTo);
        const uint64_t badRip = RunRipRefs(m, 20000, MutantEntitiesRipRefs, 0);
        const uint64_t badAdj = RunSkipScans(m, big, h, 1500, Skip::Adjust, sco::game_actor_stats::MutantResolveAdjustStat);
        std::printf("mutation self-check: LeaTo mutant -> %llu mismatches, RipRefs mutant -> %llu, adjust_stat mutant -> %llu\n",
                    static_cast<unsigned long long>(badLea), static_cast<unsigned long long>(badRip), static_cast<unsigned long long>(badAdj));
        CHECK(badLea > 0);
        CHECK(badRip > 0);
        CHECK(badAdj > 0);
    }

    // The memo and the index follow the image they were filled for.
    {
        sco::ScanIndex ix;
        sco::Image a{}, b{};
        a.base = reinterpret_cast<uint8_t*>(0x10000); a.size = 0x1000;
        b = a; b.size = 0x2000;
        sco::SiteMemo& ma = ix.Memo(a, sco::MemoKey::SocialGroup);
        ma.done = true; ma.n = 5;
        CHECK(ix.Memo(a, sco::MemoKey::SocialGroup).done);
        CHECK(!ix.Memo(a, sco::MemoKey::Handshake).done);
        CHECK(!ix.Memo(b, sco::MemoKey::SocialGroup).done);   // another image: reset
        CHECK(!ix.Memo(a, sco::MemoKey::SocialGroup).done);
    }
    {
        // One index, two different .text ranges in turn: each answer is the plain scan's.
        Rng q{ 0x5C0DE2410003ull };
        sco::ScanIndex ix;
        uint64_t bad = 0;
        for (int i = 0; i < 2000; ++i) {
            Low a, b; MakeLow(q, a); MakeLow(q, b);
            for (Low* L : { &a, &b, &a }) {
                const uint8_t* t = L->Target(q);
                size_t want = sco::ScanIndex::kNone;
                if (L->img.text.size >= 7)
                    for (size_t off = 0; off + 7 <= L->img.text.size; ++off) {
                        const uint8_t* p = L->img.text.base + off;
                        if ((p[0] & 0xFB) == 0x48 && p[1] == 0x8D && (p[2] & 0xC7) == 0x05 &&
                            static_cast<int64_t>(reinterpret_cast<uintptr_t>(p)) + 7 + sco::Rel32(p + 3) ==
                                static_cast<int64_t>(reinterpret_cast<uintptr_t>(t))) { want = off; break; }
                    }
                ++g_cases;
                if (ix.FindRip(L->img.text, sco::RipForm::AnyLea(), t, 0) != want) ++bad;
            }
        }
        CHECK(bad == 0);
    }

    std::printf("%llu comparisons in total\n", static_cast<unsigned long long>(g_cases));
    CHECK(g_cases >= 1000000);
    std::printf("%d passed, %d failed\n", pass, fail);
    return fail ? 1 : 0;
}
