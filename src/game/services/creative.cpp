// game.creative 1.0 (sc_creative.h) from the game pack: god mode, fly (noclip) and its speed, and
// infinite ammo, for the local player. Moved from sc-offline's spawner.cpp (ProcessNoclip,
// ProcessGodMode and the fly speed write) and ammo.cpp (the magazine setter hook, the carried-
// weapon and ship checks, the ship top-up), same offsets and slots; the addresses come from the
// spawn.* rows and ammo.set_ammo (sco/game/actors.h, features.h) and the teleport.* reads
// (reads.h). No scans, no fixed addresses. The commands, hotkeys and menus stay in the product
// (or in a plugin built on this service).
//
// Every toggle has an owner, the plugin that last switched it on, and spawn.cpp's one release
// hook turns it off when that plugin unloads or crashes (ReleaseCreativeOwner). The magazine
// hook is installed with sco::hook the first time an ammo toggle is switched on and its rows are
// OK, and removed at Stop; while both ammo toggles are off the detour only passes calls on.
#include "creative.h"
#include "spawn.h"
#include "sco/caps.h"
#include "sco/game/actors.h"
#include "sco/game/reads.h"
#include "sco/hook.h"
#include "sco/host.h"
#include "sco/log.h"
#include "sco/signatures.h"
#include <sc_creative.h>
#include <cmath>
#include <cstdarg>
#include <cstdio>
#include <cstring>
#include <iterator>
#include <windows.h>

namespace sco::game::services {

namespace {

using RequestFlyFn = uint16_t(__fastcall*)(uintptr_t actionHandler, int mode);
using ActorLinkFn  = uintptr_t(__fastcall*)(uintptr_t actor);
using SetGodFn     = void(__fastcall*)(uintptr_t godModeState, uint8_t state);
using HandleToIdFn = uint64_t*(__fastcall*)(const void* handleField, uint64_t* entityId);
using SetAmmoFn    = void(__fastcall*)(uintptr_t container, int count, uint8_t notify);

constexpr uint64_t kPtrMask = 0xFFFFFFFFFFFFull;
constexpr size_t   kStolenAmmo = 14;   // ammo.set_ammo's prologue, pinned by its row's checks

// The rows each feature reads; each list is its capability (caps::SetFromSignatures).
constexpr const char* kGodRows[]   = { "teleport.entity_system", "spawn.landing_helper", "spawn.god_mode_call", "spawn.set_god_mode" };
constexpr const char* kFlyRows[]   = { "teleport.entity_system", "spawn.landing_helper", "spawn.request_fly_mode", "spawn.actor_link" };
constexpr const char* kSpeedRows[] = { "spawn.game_cvars", "spawn.fly_speed_scaler" };   // the actors table's spawn.fly_speed
constexpr const char* kAmmoRows[]  = { "teleport.entity_system", "spawn.handle_to_id", "ammo.set_ammo" };
constexpr const char* kShipRows[]  = { "teleport.entity_system", "spawn.landing_helper", "spawn.handle_to_id", "ammo.set_ammo" };
constexpr const char kCapGod[]  = "game.creative.god_mode";
constexpr const char kCapFly[]  = "game.creative.fly";
constexpr const char kCapAmmo[] = "game.creative.ammo";
constexpr const char kCapShip[] = "game.creative.ship_ammo";

struct Creative {
    bool           god = false, fly = false, speed = false, ammo = false, ship = false;
    uintptr_t*     entitySystem = nullptr;   // teleport.entity_system
    uintptr_t*     components = nullptr;     // the global 8 bytes past it (spawn.landing_helper checks)
    RequestFlyFn   requestFly = nullptr;
    ActorLinkFn    actorLink = nullptr;
    SetGodFn       setGod = nullptr;
    uint32_t       godByte = 0;
    uintptr_t*     gameCVars = nullptr;
    uint32_t       flySpeedOffset = 0;
    HandleToIdFn   handleToId = nullptr;
};

Creative g_c;
bool     g_started = false;

// A switch and who turned it on.
struct Toggle { bool on = false; const void* owner = nullptr; };
Toggle g_god, g_fly;
Toggle g_speed;                       // on: the fly speed was changed from the game's; owner restores it
float  g_speedDefault = 0;
// The ammo toggles are read by the magazine setter's detour, which may run on any thread.
Toggle        g_ammo, g_ship;
volatile LONG g_ammoOn = 0, g_shipOn = 0;
volatile LONG g_lastNotify = 1;
volatile uint64_t g_you = 0, g_myShip = 0;
SetAmmoFn     g_setAmmoOrig = nullptr;
bool          g_hooked = false;
void*         g_hookTarget = nullptr;

template <typename T> T Rd(uintptr_t p) { return *reinterpret_cast<const T*>(p); }

template <typename R, typename... A> R VCall(uintptr_t obj, size_t off, A... a) {
    using Fn = R(__fastcall*)(uintptr_t, A...);
    return reinterpret_cast<Fn>(Rd<uintptr_t>(Rd<uintptr_t>(obj) + off))(obj, a...);
}

template <typename T> T Row(const char* id) { return reinterpret_cast<T>(Sig(id)); }

int32_t Rel32(const uint8_t* p) { return *reinterpret_cast<const int32_t*>(p); }

bool RowsOk(const char* const* ids, size_t n) {
    for (size_t i = 0; i < n; ++i) {
        const SigResult* s = SigLookup(ids[i]);
        if (!s || s->state != SigState::Ok) return false;
    }
    return true;
}

bool SetCap(const char* name, const char* const* rows, size_t n, bool readsOk) {
    caps::SetFromSignatures(name, rows, n);
    if (!readsOk) caps::Set(name, false, "teleport.* rows not OK");
    return readsOk && RowsOk(rows, n);
}

void Resolve() {
    g_c = {};
    const bool readsOk = reads::Ready();
    g_c.god   = SetCap(kCapGod, kGodRows, std::size(kGodRows), readsOk);
    g_c.fly   = SetCap(kCapFly, kFlyRows, std::size(kFlyRows), readsOk);
    g_c.ammo  = SetCap(kCapAmmo, kAmmoRows, std::size(kAmmoRows), readsOk);
    g_c.ship  = SetCap(kCapShip, kShipRows, std::size(kShipRows), readsOk);
    g_c.speed = g_c.fly && RowsOk(kSpeedRows, std::size(kSpeedRows));
    if (!g_c.god && !g_c.fly && !g_c.ammo && !g_c.ship) return;
    uint8_t* es = Sig("teleport.entity_system");
    g_c.entitySystem = reinterpret_cast<uintptr_t*>(es);
    g_c.components   = reinterpret_cast<uintptr_t*>(es + 8);
    if (g_c.god) {
        g_c.setGod  = Row<SetGodFn>("spawn.set_god_mode");
        g_c.godByte = static_cast<uint32_t>(Rel32(Sig("spawn.set_god_mode") + actors::kGodModeByteDisp));
    }
    if (g_c.fly) {
        g_c.requestFly = Row<RequestFlyFn>("spawn.request_fly_mode");
        g_c.actorLink  = Row<ActorLinkFn>("spawn.actor_link");
    }
    if (g_c.speed) {
        g_c.gameCVars      = Row<uintptr_t*>("spawn.game_cvars");
        g_c.flySpeedOffset = static_cast<uint32_t>(Rel32(Sig("spawn.fly_speed_scaler") + actors::kFlySpeedDisp));
    }
    if (g_c.ammo || g_c.ship) g_c.handleToId = Row<HandleToIdFn>("spawn.handle_to_id");
}

// ---- last_error ------------------------------------------------------------------------------
//
// Game thread: one slot per plugin; other threads: the refusal on that thread.

constexpr size_t kErrMax   = 256;
constexpr size_t kErrSlots = 64;   // plugin handles are never reused; past it the oldest slot goes
struct ErrSlot { const void* owner; uint64_t seq; char msg[kErrMax]; };
ErrSlot  g_errs[kErrSlots];
ErrSlot  g_queryErr;               // calls with no valid self
uint64_t g_errSeq = 0;
thread_local char t_threadErr[kErrMax];

const ErrSlot* FindErr(const void* owner) {
    for (const ErrSlot& e : g_errs)
        if (e.owner == owner) return &e;
    return nullptr;
}

ErrSlot& ErrFor(const void* owner) {
    if (!owner) return g_queryErr;
    ErrSlot* oldest = &g_errs[0];
    for (ErrSlot& e : g_errs) {
        if (e.owner == owner) return e;
        if (e.seq < oldest->seq) oldest = &e;
    }
    *oldest = {};
    oldest->owner = owner;
    return *oldest;
}

void ForgetErr(const void* owner) {
    for (ErrSlot& e : g_errs)
        if (e.owner == owner) e = {};
}

const char* PluginName(const sco_plugin* self) {
    const char* id = self ? host::PluginId(self) : nullptr;
    return id ? id : "?";
}

// Records why a call failed (the reason for last_error), logs it with the plugin's id and returns r.
sco_result Fail(const sco_plugin* owner, const char* fn, sco_result r, const char* fmt, ...) {
    ErrSlot& e = ErrFor(owner);
    va_list ap;
    va_start(ap, fmt);
    vsnprintf(e.msg, sizeof(e.msg), fmt, ap);
    va_end(ap);
    e.seq = ++g_errSeq;
    if (owner) Log("[game] creative: %s: %s -> %s: %s", PluginName(owner), fn, ResultName(static_cast<Result>(r)), e.msg);
    return r;
}

sco_result WrongThread() {
    snprintf(t_threadErr, sizeof(t_threadErr), "game thread only");
    return SCO_WRONG_THREAD;
}

// ---- game reads and calls, under SEH -----------------------------------------------------------
//
// No C++ object with a destructor lives in these frames (MSVC C2712).

uintptr_t EntityComponent(uintptr_t entity, const char* type) {
    uint8_t tmp[16] = {};
    const uint16_t* id = VCall<const uint16_t*>(*g_c.components, actors::kComponentsTypeId, tmp, type);
    if (!id) return 0;
    uint16_t typeId = *id;
    uint8_t out[16] = {};
    const uint64_t* h = VCall<const uint64_t*>(entity, actors::kEntityComponent, out, &typeId);
    return h ? (*h & kPtrMask) : 0;
}

uintptr_t SafeComponent(uintptr_t entity, const char* type) {
    __try { return entity ? EntityComponent(entity, type) : 0; } __except (EXCEPTION_EXECUTE_HANDLER) { return 0; }
}

uintptr_t EntityById(uint64_t id) {
    __try { return id ? reads::EntityFromId(id) : 0; } __except (EXCEPTION_EXECUTE_HANDLER) { return 0; }
}

uint64_t ParentId(uintptr_t entity) {
    __try {
        uint64_t port = 0;
        VCall<void>(entity, actors::kEntityParentPort, &port, 0ull);
        if (!(port & kPtrMask)) return 0;
        uint64_t id = 0;
        const uint64_t* owner = VCall<const uint64_t*>(port & kPtrMask, actors::kPortOwnerId, &id);
        return owner ? *owner : 0;
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        return 0;
    }
}

uint64_t LocalPlayerId() {
    __try {
        uintptr_t actor, entity;
        if (!reads::LocalPlayer(actor, entity)) return 0;
        uint64_t id = 0;
        g_c.handleToId(reinterpret_cast<const void*>(actor + 8), &id);
        return id;
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        return 0;
    }
}

// The ship you're aboard: your zone's entity, when it has item ports; 0 = in no ship.
uint64_t PlayerShip() {
    __try {
        uintptr_t actor, entity;
        if (!reads::LocalPlayer(actor, entity)) return 0;
        const uintptr_t zone = reads::EntityZone(entity);
        if (!zone) return 0;
        const uint64_t id = reads::ZoneId(zone);
        const uintptr_t ship = reads::EntityFromId(id);
        return ship && EntityComponent(ship, "IItemPortContainer") ? id : 0;
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        return 0;
    }
}

// ---- god mode ----------------------------------------------------------------------------------

enum class Apply { Changed, Already, NotSpawned, NoData, Fault };

// Puts your actor's god mode state byte at on (2) or off (0), through the game's own setter.
Apply ApplyGod(bool on) {
    __try {
        uintptr_t actor, entity;
        if (!reads::LocalPlayer(actor, entity)) return Apply::NotSpawned;
        const uintptr_t comp = EntityComponent(entity, "Actor");
        const uintptr_t data = comp ? Rd<uintptr_t>(comp + actors::kGodModeData) : 0;
        if (!data) return Apply::NoData;
        const uintptr_t state = data + actors::kGodModeState;
        const uint8_t want = on ? 2 : 0;
        if (Rd<uint8_t>(state + g_c.godByte) == want) return Apply::Already;
        g_c.setGod(state, want);
        return Apply::Changed;
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        return Apply::Fault;
    }
}

// ---- fly mode and its speed --------------------------------------------------------------------

// ESCActorFlyMode: 2 = fly (noclip), 0 = off, as sc-offline sent them.
Apply RequestFly(bool on) {
    __try {
        uintptr_t actor, entity;
        if (!reads::LocalPlayer(actor, entity)) return Apply::NotSpawned;
        const uintptr_t comp = EntityComponent(entity, "Actor");
        if (!comp) return Apply::NoData;
        g_c.requestFly(g_c.actorLink(comp), on ? 2 : 0);
        return Apply::Changed;
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        return Apply::Fault;
    }
}

// Reads (save != nullptr) and writes the game's fly speed field; false when the settings aren't
// up yet or the access faulted.
bool WriteFlySpeed(float mps, float* previous) {
    __try {
        const uintptr_t cvars = *g_c.gameCVars;
        if (!cvars) return false;
        float* field = reinterpret_cast<float*>(cvars + g_c.flySpeedOffset);
        if (previous) *previous = *field;
        *field = mps;
        return true;
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        return false;
    }
}

// ---- infinite ammo: the magazine setter's detour ------------------------------------------------

// True if the container's owner has the entity `a` as an ancestor within `levels` steps.
bool HasAncestor(uintptr_t container, int levels, uint64_t a) {
    uintptr_t entity = Rd<uint64_t>(container + 8) & kPtrMask;
    for (int up = 0; a && entity && up < levels; ++up) {
        const uint64_t parent = ParentId(entity);
        if (!parent) return false;
        if (parent == a) return true;
        entity = EntityById(parent);
    }
    return false;
}

void __fastcall SetAmmoDetour(uintptr_t container, int count, uint8_t notify) {
    g_lastNotify = notify;
    if (g_ammoOn || g_shipOn) {
        __try {
            const int32_t key = Rd<int32_t>(container + actors::kAmmoKey);
            const int32_t now = key ? Rd<int32_t>(container + actors::kAmmoCount) ^ key : 0;
            if (count < now) {
                // A hand-held weapon is a few steps from you; a ship weapon sits deeper (weapon ->
                // mount -> turret -> ship), so walk further up.
                if (g_ammoOn && HasAncestor(container, 4, g_you)) count = Rd<int32_t>(container + actors::kAmmoMax);
                else if (g_shipOn && HasAncestor(container, 8, g_myShip)) count = Rd<int32_t>(container + actors::kAmmoMax);
            }
        } __except (EXCEPTION_EXECUTE_HANDLER) {}
    }
    g_setAmmoOrig(container, count, notify);
}

// Energy weapons can drain their magazine without going through the setter, so with ship ammo on
// every ammo container on your ship is also topped up directly, twice a second. A container is
// only touched if it reads like a magazine (key set, count between 0 and its maximum).
bool ReadMagazine(uintptr_t c, int32_t& count, int32_t& max) {
    __try {
        const int32_t key = Rd<int32_t>(c + actors::kAmmoKey);
        max = Rd<int32_t>(c + actors::kAmmoMax);
        if (!key || max <= 0 || max > 1000000) return false;
        count = Rd<int32_t>(c + actors::kAmmoCount) ^ key;
        return count >= 0 && count <= max;
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        return false;
    }
}

void Refill(uintptr_t c, int32_t max) {
    __try { g_setAmmoOrig(c, max, static_cast<uint8_t>(g_lastNotify)); } __except (EXCEPTION_EXECUTE_HANDLER) {}
}

// The ship's parts that hold ammo, found like sc-offline's CollectShipItems: a spawned ship's parts
// have consecutive entity ids after its own, and the ones whose chain of parents leads back to
// the ship are kept. Cached for 3 s.
constexpr uint32_t kMaxParts = 96;
uint64_t   g_parts[kMaxParts];
uint32_t   g_partCount = 0;
uint64_t   g_partsOf = 0;
ULONGLONG  g_partsAt = 0;

void CollectParts(uint64_t ship, ULONGLONG now) {
    if (g_partsOf == ship && now - g_partsAt < 3000) return;
    g_partCount = 0;
    g_partsOf = ship;
    g_partsAt = now;
    int misses = 0;
    for (uint64_t id = ship + 1; id < ship + 8000 && misses < 400 && g_partCount < kMaxParts; ++id) {
        const uintptr_t e = EntityById(id);
        if (!e) { ++misses; continue; }
        misses = 0;
        uint64_t up = ParentId(e);
        for (int depth = 1; up && up != ship && depth < 12; ++depth) {
            const uintptr_t next = EntityById(up);
            up = next ? ParentId(next) : 0;
        }
        if (up != ship) continue;   // not part of this ship (crew, other ships, loose items)
        if (SafeComponent(e, "AmmoContainerComponent")) g_parts[g_partCount++] = id;
    }
}

void TopUpShip(uint64_t ship, ULONGLONG now) {
    CollectParts(ship, now);
    for (uint32_t i = 0; i < g_partCount; ++i) {
        const uintptr_t comp = SafeComponent(EntityById(g_parts[i]), "AmmoContainerComponent");
        int32_t count = 0, max = 0;
        if (comp && ReadMagazine(comp, count, max) && count < max) Refill(comp, max);
    }
}

// Hooks the magazine setter once; false (and the reason in why) when it can't be.
bool EnsureAmmoHook(const char** why) {
    if (g_hooked) return true;
    void* target = Sig("ammo.set_ammo");
    void* orig = nullptr;
    const hook::Error e = hook::InstallDetour(target, kStolenAmmo, reinterpret_cast<void*>(&SetAmmoDetour), &orig);
    if (e != hook::Error::None) {
        *why = hook::ErrorName(e);
        return false;
    }
    g_setAmmoOrig = reinterpret_cast<SetAmmoFn>(orig);
    g_hookTarget = target;
    g_hooked = true;
    Log("[game] creative: the magazine setter is hooked");
    return true;
}

void RemoveAmmoHook() {
    if (!g_hooked) return;
    const hook::Error e = hook::RemoveDetour(g_hookTarget);
    if (e != hook::Error::None) Log("[game] warning: creative: removing the magazine hook failed: %s", hook::ErrorName(e));
    g_hooked = false;   // the trampoline stays callable (g_setAmmoOrig is kept) for a thread still inside the detour
    g_hookTarget = nullptr;
}

// ---- the tick ----------------------------------------------------------------------------------

ULONGLONG g_lastGod = 0, g_lastShip = 0;

void OnTick(const char*, const void*, void*) {
    if (!g_started) return;
    const ULONGLONG now = GetTickCount64();
    if (g_ammoOn) g_you = LocalPlayerId();
    if (g_shipOn) {
        g_myShip = PlayerShip();
        if (g_hooked && g_myShip && now - g_lastShip >= 500) {
            g_lastShip = now;
            TopUpShip(g_myShip, now);
        }
    }
    // The game resets the state on a respawn, so god mode is held, not set once.
    if (g_god.on && now - g_lastGod >= 500) {
        g_lastGod = now;
        if (ApplyGod(true) == Apply::Changed) Log("[game] creative: god mode applied (no damage)");
    }
}

// ---- switching things off ----------------------------------------------------------------------

void GodOff() {
    g_god = {};
    const Apply r = ApplyGod(false);
    if (r == Apply::Changed) Log("[game] creative: god mode off");
}

void FlyOff() {
    g_fly = {};
    const Apply r = RequestFly(false);
    if (r == Apply::Changed) Log("[game] creative: fly off");
}

void SpeedRestore() {
    if (!g_speed.on) return;
    g_speed = {};
    if (WriteFlySpeed(g_speedDefault, nullptr)) Log("[game] creative: fly speed back to %.0f", g_speedDefault);
}

void AmmoOff() {
    g_ammo = {};
    InterlockedExchange(&g_ammoOn, 0);
}

void ShipOff() {
    g_ship = {};
    InterlockedExchange(&g_shipOn, 0);
    g_partCount = 0;
    g_partsOf = 0;
}

// ---- game.creative (sc_creative.h) -------------------------------------------------------------

// The checks every function starts with. SCO_OK to go on.
sco_result Enter(sco_plugin* self, const char* fn, bool ready, const char* capability) {
    if (!OnGameThread()) return WrongThread();
    if (!ready) return Fail(self, fn, SCO_UNAVAILABLE, "%s isn't ready on this game build", capability);
    if (!self || !host::PluginId(self)) return Fail(nullptr, fn, SCO_BAD_ARG, "self isn't a plugin handle");
    return SCO_OK;
}

// Without the release hook a reloaded plugin could inherit an unloaded one's toggles, so nothing
// is switched on then.
sco_result NeedRelease(sco_plugin* self, const char* fn) {
    if (SpawnReleaseHooked()) return SCO_OK;
    return Fail(self, fn, SCO_FAILED, "the host has no release hook left, so it couldn't switch this off when your plugin unloads");
}

sco_result SvcSetGodMode(sco_plugin* self, int on) {
    if (const sco_result r = Enter(self, "set_god_mode", g_c.god, kCapGod); r != SCO_OK) return r;
    if (!on) {
        if (g_god.on) {
            Log("[game] creative: %s: god mode off", PluginName(self));
            GodOff();
        }
        return SCO_OK;
    }
    if (const sco_result r = NeedRelease(self, "set_god_mode"); r != SCO_OK) return r;
    if (ApplyGod(true) == Apply::Fault) return Fail(self, "set_god_mode", SCO_FAILED, "fault while setting god mode");
    g_god = { true, self };
    g_lastGod = GetTickCount64();
    Log("[game] creative: %s: god mode on", PluginName(self));
    return SCO_OK;
}

sco_result SvcSetFlyMode(sco_plugin* self, int on) {
    if (const sco_result r = Enter(self, "set_fly_mode", g_c.fly, kCapFly); r != SCO_OK) return r;
    if (!on) {
        if (g_fly.on) {
            Log("[game] creative: %s: fly off", PluginName(self));
            FlyOff();
        }
        return SCO_OK;
    }
    if (const sco_result r = NeedRelease(self, "set_fly_mode"); r != SCO_OK) return r;
    switch (RequestFly(true)) {
    case Apply::NotSpawned: return Fail(self, "set_fly_mode", SCO_FAILED, "you're not spawned yet");
    case Apply::NoData:     return Fail(self, "set_fly_mode", SCO_FAILED, "your actor has no Actor component (no fly handler)");
    case Apply::Fault:      return Fail(self, "set_fly_mode", SCO_FAILED, "fault while requesting fly mode");
    default: break;
    }
    g_fly = { true, self };
    Log("[game] creative: %s: fly on", PluginName(self));
    return SCO_OK;
}

sco_result SvcSetFlySpeed(sco_plugin* self, float mps) {
    if (const sco_result r = Enter(self, "set_fly_speed", g_c.fly, kCapFly); r != SCO_OK) return r;
    if (!g_c.speed)
        return Fail(self, "set_fly_speed", SCO_UNAVAILABLE, "the game's fly speed field wasn't found on this game build (spawn.fly_speed rows)");
    if (!std::isfinite(mps) || mps <= 0 || mps > static_cast<float>(SC_CREATIVE_FLY_SPEED_MAX))
        return Fail(self, "set_fly_speed", SCO_BAD_ARG, "speed must be above 0 and at most %d m/s", SC_CREATIVE_FLY_SPEED_MAX);
    if (const sco_result r = NeedRelease(self, "set_fly_speed"); r != SCO_OK) return r;
    float previous = 0;
    if (!WriteFlySpeed(mps, g_speed.on ? nullptr : &previous))
        return Fail(self, "set_fly_speed", SCO_FAILED, "the game's settings aren't up yet");
    if (!g_speed.on) g_speedDefault = previous;
    g_speed = { true, self };
    Log("[game] creative: %s: fly speed %.0f m/s", PluginName(self), mps);
    return SCO_OK;
}

sco_result SvcSetInfiniteAmmo(sco_plugin* self, int on) {
    if (const sco_result r = Enter(self, "set_infinite_ammo", g_c.ammo, kCapAmmo); r != SCO_OK) return r;
    if (!on) {
        if (g_ammo.on) {
            Log("[game] creative: %s: infinite ammo off", PluginName(self));
            AmmoOff();
        }
        return SCO_OK;
    }
    if (const sco_result r = NeedRelease(self, "set_infinite_ammo"); r != SCO_OK) return r;
    const char* why = nullptr;
    if (!EnsureAmmoHook(&why)) return Fail(self, "set_infinite_ammo", SCO_FAILED, "couldn't hook the magazine setter: %s", why);
    g_you = LocalPlayerId();
    g_ammo = { true, self };
    InterlockedExchange(&g_ammoOn, 1);
    Log("[game] creative: %s: infinite ammo on", PluginName(self));
    return SCO_OK;
}

sco_result SvcSetInfiniteShipAmmo(sco_plugin* self, int on) {
    if (const sco_result r = Enter(self, "set_infinite_ship_ammo", g_c.ship, kCapShip); r != SCO_OK) return r;
    if (!on) {
        if (g_ship.on) {
            Log("[game] creative: %s: infinite ship ammo off", PluginName(self));
            ShipOff();
        }
        return SCO_OK;
    }
    if (const sco_result r = NeedRelease(self, "set_infinite_ship_ammo"); r != SCO_OK) return r;
    const char* why = nullptr;
    if (!EnsureAmmoHook(&why)) return Fail(self, "set_infinite_ship_ammo", SCO_FAILED, "couldn't hook the magazine setter: %s", why);
    g_myShip = PlayerShip();
    g_ship = { true, self };
    g_lastShip = 0;
    InterlockedExchange(&g_shipOn, 1);
    Log("[game] creative: %s: infinite ship ammo on", PluginName(self));
    return SCO_OK;
}

sco_result SvcState(sco_plugin* self, uint32_t* outFlags) {
    if (outFlags) *outFlags = 0;
    if (!OnGameThread()) return WrongThread();
    if (!g_c.god && !g_c.fly && !g_c.ammo && !g_c.ship)
        return Fail(nullptr, "state", SCO_UNAVAILABLE, "no game.creative capability is ready on this game build");
    if (!self || !host::PluginId(self)) return Fail(nullptr, "state", SCO_BAD_ARG, "self isn't a plugin handle");
    if (!outFlags) return Fail(nullptr, "state", SCO_BAD_ARG, "out_flags is NULL");
    *outFlags = (g_god.on ? SC_CREATIVE_GOD_MODE : 0u) | (g_fly.on ? SC_CREATIVE_FLY : 0u) |
                (g_ammo.on ? SC_CREATIVE_INFINITE_AMMO : 0u) | (g_ship.on ? SC_CREATIVE_INFINITE_SHIP_AMMO : 0u);
    return SCO_OK;
}

sco_result SvcLastError(sco_plugin* self, char* out, uint32_t* io) {
    if (!io) return SCO_BAD_ARG;
    const uint32_t capacity = *io;
    *io = 0;
    if (!out && capacity) return SCO_BAD_ARG;
    const char* msg = t_threadErr;
    if (OnGameThread()) {
        const ErrSlot* mine = self ? FindErr(self) : nullptr;
        msg = mine && mine->seq > g_queryErr.seq ? mine->msg : g_queryErr.msg;
    }
    const size_t need = strlen(msg) + 1;
    if (capacity < need) {
        *io = static_cast<uint32_t>(need);
        return SCO_TOO_MANY;
    }
    memcpy(out, msg, need);
    *io = static_cast<uint32_t>(need);
    return SCO_OK;
}

const sc_creative_v1 kService = {
    sizeof(sc_creative_v1), 0, SvcSetGodMode, SvcSetFlyMode, SvcSetFlySpeed, SvcSetInfiniteAmmo, SvcSetInfiniteShipAmmo,
    SvcState, SvcLastError,
};

// Everything off and forgotten: Stop, and a failed Start.
void Reset() {
    if (g_god.on) GodOff();
    if (g_fly.on) FlyOff();
    SpeedRestore();
    AmmoOff();
    ShipOff();
    RemoveAmmoHook();
    g_god = g_fly = g_speed = {};
    g_c = {};
    for (ErrSlot& e : g_errs) e = {};
    g_queryErr = {};
}

}  // namespace

Result StartCreative() {
    if (g_started) return Result::Ok;
    Resolve();
    if (!g_c.god && !g_c.fly && !g_c.ammo && !g_c.ship)
        Log("[game] game.creative.* not ready: game.creative answers SCO_UNAVAILABLE (see the [core] lines)");
    Result r = host::ProvideGameService(SC_CREATIVE_SERVICE_NAME, SC_CREATIVE_SERVICE_VERSION, &kService);
    if (r != Result::Ok) {
        g_c = {};
        return r;
    }
    r = Subscribe(host::GameOwner(), "tick", OnTick, nullptr);
    if (r != Result::Ok) {
        host::WithdrawGameService(SC_CREATIVE_SERVICE_NAME);
        g_c = {};
        return r;
    }
    g_started = true;
    return Result::Ok;
}

void StopCreative() {
    if (!g_started) return;
    Unsubscribe(host::GameOwner(), "tick", OnTick);
    g_started = false;
    host::WithdrawGameService(SC_CREATIVE_SERVICE_NAME);
    Reset();
}

void ReleaseCreativeOwner(const void* owner) {
    if (!g_started || !owner) return;
    ForgetErr(owner);
    if (g_god.on && g_god.owner == owner) { Log("[game] creative: owner unloaded: god mode off"); GodOff(); }
    if (g_fly.on && g_fly.owner == owner) { Log("[game] creative: owner unloaded: fly off"); FlyOff(); }
    if (g_speed.on && g_speed.owner == owner) SpeedRestore();
    if (g_ammo.on && g_ammo.owner == owner) { Log("[game] creative: owner unloaded: infinite ammo off"); AmmoOff(); }
    if (g_ship.on && g_ship.owner == owner) { Log("[game] creative: owner unloaded: infinite ship ammo off"); ShipOff(); }
}

}  // namespace sco::game::services
