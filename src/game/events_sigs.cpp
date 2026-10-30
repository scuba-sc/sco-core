// Signature rows for game.actors 1.1's health and state and the game.* bus events, from the spikes
// in docs/design/game-world-spikes.md (B4, B5, B6, B8), each locator and byte check as written
// there, proven unique in the .text of 4.10.196.36804 (sigcheck output in the PR).
#include "sco/game/events.h"
#include "sco/scan.h"
#include "sco/signatures.h"
#include "sig_rows.h"
#include <cstring>
#include <iterator>

namespace sco::game {

namespace {

using namespace events;
using rows::Check;
using rows::FnSpec;
using rows::ResolveFn;
using rows::ResolveRip;
using rows::RipSpec;

// Guard conversion of table offsets before using shared range accessors.
const uint8_t* LocalTextAt(const Image& img, const uint8_t* p, size_t off, size_t n) {
    return off <= static_cast<size_t>(PTRDIFF_MAX)
        ? rows::TextAt(img, p, static_cast<ptrdiff_t>(off), n) : nullptr;
}
const uint8_t* LocalRip(const Image& img, const uint8_t* p, size_t off, size_t disp, size_t len) {
    return rows::CheckedRip(img, LocalTextAt(img, p, off, len), disp, len);
}

// ---- actor status: health and state (B4) -------------------------------------------------------

// mov rax,[rcx+208h]; add rax,74A0h; ret: actor -> CSCActorStatus.
constexpr const char* kAccessor = "48 8B 81 08 02 00 00 48 05 A0 74 00 00 C3";
static_assert(kActorStatusObject == 0x208 && kActorStatus == 0x74A0, "kAccessor pins these offsets");
static_assert(kStatHealthPool == 0x0B && kStatStun == 6, "the health site's mov edx, imm pins these");
static_assert(kStatusPredicateSlot == 0x60, "the state site's mov rdx,[rcx+60h] pins this");

bool CallTargetsEqual(const Image& img, const uint8_t* at, size_t a, size_t b) {
    const uint8_t* x = LocalRip(img, at, a, 1, 5);
    const uint8_t* y = LocalRip(img, at, b, 1, 5);
    return x && y && x == y;
}

const char* HealthExtra(const Image& img, const uint8_t* at) {
    if (!LocalTextAt(img, at, 0, 5)) return "status accessor call outside .text";
    const uint8_t* accessor = LocalRip(img, at, 0, 1, 5);
    if (!LocalTextAt(img, accessor, 0, 14)) return "status accessor outside .text";
    if (!BytesMatch(accessor, kAccessor)) return "the status accessor changed (actor+0x208, +0x74a0)";
    if (!LocalTextAt(img, at, 0x1D, 5)) return "second status accessor call outside .text";
    if (!LocalRip(img, at, 0x1D, 1, 5)) return "second status accessor target outside the image";
    if (!CallTargetsEqual(img, at, 0, 0x1D)) return "the two status accessor calls reach different functions";
    if (!LocalTextAt(img, at, 0x0D, 5) || !LocalTextAt(img, at, 0x2A, 5)) return "GetStat call outside .text";
    if (!LocalRip(img, at, 0x0D, 1, 5) || !LocalRip(img, at, 0x2A, 1, 5)) return "GetStat target outside the image";
    if (!CallTargetsEqual(img, at, 0x0D, 0x2A)) return "the two GetStat calls reach different functions";
    return nullptr;
}

constexpr rows::FnSpec kHealthSite = {
    "E8 ?? ?? ?? ?? BA 06 00 00 00 48 8B C8 E8 ?? ?? ?? ?? 48 8B 8E F8 00 00 00 C5 F8 28 F0 E8 ?? ?? ?? ?? BA 0B 00 00 00 48 8B C8 E8",
    nullptr, 0, HealthExtra,
};
constexpr RipSpec kStatusAccessor{ "actor.health_site", 0x00, 1, 5 };   // call the accessor
constexpr RipSpec kGetStat{ "actor.health_site", 0x0D, 1, 5 };          // call GetStat

constexpr Check kStateChecks[] = {
    { 0x17, "48 8B 51 60", "the status vtable slot 0x60 predicate changed (+0x17)" },
};

const char* StateExtra(const Image& img, const uint8_t* at) {
    if (!LocalTextAt(img, at, 0, 5)) return "status accessor call outside .text";
    const uint8_t* accessor = LocalRip(img, at, 0, 1, 5);
    if (!LocalTextAt(img, accessor, 0, 14)) return "status accessor outside .text";
    if (!BytesMatch(accessor, kAccessor)) return "the status accessor changed (actor+0x208, +0x74a0)";
    return nullptr;
}

constexpr rows::FnSpec kStateSite = {
    "E8 ?? ?? ?? ?? 48 8B C8 48 8B D8 E8 ?? ?? ?? ?? 84 C0 75 14 48 8B 0B 48 8B 51 60 48 8B CB FF D2 84 C0 "
    "0F 84 ?? ?? ?? ?? 48 8D 44 24 70",
    kStateChecks, std::size(kStateChecks), StateExtra,
};

// Counts the `r0 r1 r2 rel32` instructions in .text whose RIP target is `target` (up to max kept).
int LeaRefs(const Section& text, uint8_t r0, uint8_t r1, uint8_t r2, const uint8_t* target, const uint8_t** out, int max) {
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

// The function whose label string is `label`: the one lea r0 r1 r2 [label] in .text, and the .pdata
// function that contains it. lea receives the lea.
SigResult LabelFunction(const Image& img, const char* label, uint8_t r0, uint8_t r1, uint8_t r2, const uint8_t** lea) {
    if (!LocalTextAt(img, img.text.base, 0, img.text.size)) return SigFail("invalid .text");
    if (!rows::InImage(img, img.rdata.base, img.rdata.size)) return SigFail("invalid .rdata");
    const uint8_t* str = FindCString(img.rdata, label);
    if (!str) return SigFail("the label string isn't in .rdata");
    const uint8_t* refs[2] = {};
    const int n = LeaRefs(img.text, r0, r1, r2, str, refs, 2);
    if (n == 0) return { SigState::Missing, nullptr, 0, nullptr };
    if (n > 1) return { SigState::Ambiguous, nullptr, n, nullptr };
    if (!rows::ImageAt(img, img.pdata.base, 0, img.pdata.size)) return SigFail("invalid .pdata");
    const uint8_t* f = FunctionStart(img, refs[0]);
    if (!f) return SigFail("the label isn't inside a .pdata function");
    if (!rows::TextAt(img, f, 0, 1)) return SigFail("function start outside .text");
    if (lea) *lea = refs[0];
    return SigOk(f);
}

SigResult ResolveIsDeadConfirmed(const Image& img) {
    const uint8_t* lea = nullptr;
    SigResult r = LabelFunction(img, "CSCActorStatus::IsDeadConfirmed", 0x48, 0x8D, 0x05, &lea);
    if (r.state != SigState::Ok) return r;
    const uint8_t* site = Sig("actor.state_site");
    if (!site) return SigFail("actor.state_site missing");
    const uint8_t* call = LocalTextAt(img, site, 0x0B, 5);
    const uint8_t* p2 = LocalRip(img, site, 0x0B, 1, 5);
    if (!call) return SigFail("state test call outside .text");
    if (!p2) return SigFail("state test target outside the image");
    if (*call != 0xE8 || p2 != r.at) return SigFail("the state test doesn't call CSCActorStatus::IsDeadConfirmed (+0x0b)");
    return r;
}

// ---- player spawned (B5) -----------------------------------------------------------------------

constexpr const char* kSpawnLabel = "void __cdecl SCigEventDispatcher::QueueEvent<struct SPI_Player_OnSpawn>(const struct SPI_Player_OnSpawn &)";

const char* SpawnExtra(const Image& img, const uint8_t* at) {
    if (!rows::InImage(img, img.rdata.base, img.rdata.size)) return "invalid .rdata";
    const uint8_t* label = FindCString(img.rdata, kSpawnLabel);
    if (!label || LocalRip(img, at, 0x60, 3, 7) != label) return "the function doesn't load the SPI_Player_OnSpawn label at +0x60";
    if (!rows::ImageAt(img, img.pdata.base, 0, img.pdata.size)) return "invalid .pdata";
    const uint8_t* f = FunctionStart(img, LocalTextAt(img, at, 0x60, 7));
    if (f != at) return "the SPI_Player_OnSpawn label isn't inside the function the pattern found";
    return nullptr;
}

constexpr Check kSpawnChecks[] = {
    { 0x2C, "FF 90 A8 07 00 00", "entity slot 0x7a8 call changed (+0x2c)" },
    { 0x60, "48 8D 05",          "the label load changed (+0x60)" },
};
constexpr rows::FnSpec kPlayerSpawn = {
    "48 89 5C 24 10 48 89 74 24 18 57 48 83 EC 40 48 8B F9 48 B8 FF FF FF FF FF FF 00 00 48 8B 49 08 49 8B D8 48 23 C8",
    kSpawnChecks, std::size(kSpawnChecks), SpawnExtra,
};

// ---- player died (B6) --------------------------------------------------------------------------

SigResult ResolvePlayerDeath(const Image& img) {
    const uint8_t* lea = nullptr;
    SigResult r = LabelFunction(img, "void __cdecl SCigEventDispatcher::QueueEvent<struct SPI_Player_OnDeath>(const struct SPI_Player_OnDeath &)",
                                0x48, 0x8D, 0x05, &lea);
    if (r.state != SigState::Ok) return r;
    const uint8_t* f = r.at;
    if (lea != LocalTextAt(img, f, 0x414, 7)) return SigFail("the label isn't loaded at +0x414");
    constexpr Check checks[] = {
        {0, "4C 89 44 24 18 48 89 54 24 10 55 53 57 41 54 41 55 41 57", "prologue changed at +0x000"},
        {0x22, "48 8B DA 4C 8B F9", "argument saves changed at +0x022"},
        {0x69, "4D 8B 47 08", "the actor's entity handle load changed at +0x069"},
    };
    for (const auto& c : checks) {
        const uint8_t* p = LocalTextAt(img, f, c.off, rows::PatternBytes(c.bytes));
        if (!p) return SigFail("death function check outside .text");
        if (!BytesMatch(p, c.bytes)) return SigFail(c.why);
    }
    return r;
}

// ---- seat transitions (B8) ---------------------------------------------------------------------

SigResult ResolveSeatEnter(const Image& img) {
    SigResult r = LabelFunction(img, "CSCActorResultStateLinked::Enter", 0x4C, 0x8D, 0x05, nullptr);
    if (r.state != SigState::Ok) return r;
    if (!LocalTextAt(img, r.at, 0, 21)) return SigFail("seat prologue outside .text");
    if (!BytesMatch(r.at, "48 89 5C 24 10 4C 89 44 24 18 55 56 57 41 54 41 55 41 56 41 57"))
        return SigFail("prologue changed at +0x000");
    return r;
}

SigResult ResolveSeatExit(const Image& img) {
    SigResult r = LabelFunction(img, "CSCActorResultStateLinked::Exit", 0x4C, 0x8D, 0x05, nullptr);
    if (r.state != SigState::Ok) return r;
    if (!LocalTextAt(img, r.at, 0, 19)) return SigFail("seat prologue outside .text");
    if (!BytesMatch(r.at, "4C 89 44 24 18 48 89 54 24 10 48 89 4C 24 08 55 53 56 57"))
        return SigFail("prologue changed at +0x000");
    return r;
}

// ---- capabilities ------------------------------------------------------------------------------

constexpr const char* kHealth[] = { "actor.health_site", "actor.status_accessor", "actor.get_stat" };
constexpr const char* kState[] = { "actor.state_site", "actor.status_accessor", "actor.is_dead_confirmed" };
constexpr const char* kSpawned[] = { "event.player_spawn" };
constexpr const char* kDied[] = { "event.player_death" };
// The same death row backs both views; StartEvents installs its detour once.
constexpr const char* kActorDied[] = { "event.player_death" };
constexpr const char* kSeat[] = { "event.seat_enter", "event.seat_exit" };

constexpr Capability kCaps[] = {
    { "game.actors.health",              kHealth,    std::size(kHealth) },
    { "game.actors.state",               kState,     std::size(kState) },
    { "game.events.player_spawned",      kSpawned,   std::size(kSpawned) },
    { "game.events.player_died",         kDied,      std::size(kDied) },
    { "game.events.actor_died",          kActorDied, std::size(kActorDied) },
    { "game.events.vehicle_seat",        kSeat,      std::size(kSeat) },
};

}  // namespace

namespace events {

const Capability* Capabilities(size_t& count) {
    count = std::size(kCaps);
    return kCaps;
}

}  // namespace events

extern const SigDef kEventsSignatures[] = {
    { "actor.health_site",        nullptr, 0, 0, ResolveFn<kHealthSite>,        {} },
    { "actor.status_accessor",    nullptr, 0, 0, ResolveRip<kStatusAccessor>,   { "actor.health_site" } },
    { "actor.get_stat",           nullptr, 0, 0, ResolveRip<kGetStat>,          { "actor.health_site" } },
    { "actor.state_site",         nullptr, 0, 0, ResolveFn<kStateSite>,         {} },
    { "actor.is_dead_confirmed",  nullptr, 0, 0, ResolveIsDeadConfirmed,        { "actor.state_site" } },
    { "event.player_spawn",       nullptr, 0, 0, ResolveFn<kPlayerSpawn>,       {} },
    { "event.player_death",       nullptr, 0, 0, ResolvePlayerDeath,            {} },
    { "event.seat_enter",         nullptr, 0, 0, ResolveSeatEnter,              {} },
    { "event.seat_exit",          nullptr, 0, 0, ResolveSeatExit,               {} },
};
extern const size_t kEventsSignatureCount = std::size(kEventsSignatures);

}  // namespace sco::game
