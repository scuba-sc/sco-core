// teleport.spatial 1.0 (sc_spatial.h), game.spatial 1.0 (sc_game_spatial.h), and game services' start/stop
// (spawn.entities 1.2 is in spawn.cpp, game.actors 1.1 in actors.cpp, game.vehicles 1.0 in vehicles.cpp,
// game.world 1.0 in world.cpp, the game.* bus events in events.cpp).
// Moved from sc-offline's teleport built-in (src/builtins/teleport_plugin.cpp): a tick
// subscription feeds a sco::engine::ZoneTree with your zone chain, and the conversions read
// through that tree. The tree holds ids and transforms only, never a game pointer, and is rebuilt
// every tick; a zone queried that isn't in it yet is read on the spot.
#include "sco/game/services.h"
#include "spawn.h"
#include "build.h"
#include "relocation.h"
#include "spatial_model.h"
#include "actors.h"
#include "vehicles.h"
#include "entities.h"
#include "creative.h"
#include "asop_service.h"
#include "contracts_service.h"
#include "hangar.h"
#include "inventory_host.h"
#include "container_contents.h"
#include "audio.h"
#include "reputation.h"
#include "loadout.h"
#include "input.h"
#include "gamerules.h"
#include "quantum.h"
#include "cvars.h"
#include "offline_missions.h"
#include "world.h"
#include "events.h"
#include "sco/engine/zone.h"
#include "sco/game/reads.h"
#include "sco/host.h"
#include "sco/log.h"
#include <sc_actors.h>
#include <sc_creative.h>
#include <sc_entities.h>
#include <sc_spatial.h>
#include <sc_spawn.h>
#include <sc_vehicles.h>
#include <sc_world.h>
#include <cmath>
#include <cstring>
#include <windows.h>

namespace sco::game::services {

namespace {

using engine::Quatd;
using engine::Transform;
using engine::Vector3d;
using reads::ZoneFrame;

bool             g_started = false;
engine::ZoneTree g_metadataZones; // strict, complete chains only, <= 256 observed IDs
spatial::Snapshot g_metadata;
engine::ZoneTree g_zones;          // cleared and refilled every tick; ids and transforms only
constexpr int    kChainMax = 16;   // zones per chain; the game's run about 6 deep

// Game reads under SEH, into plain structs. No C++ object with a destructor lives in these
// frames (MSVC C2712); the tree is fed outside them.
int ChainOfZoneId(uint64_t zoneId, ZoneFrame* out) {
    __try {
        const uintptr_t zone = reads::ZoneFromId(zoneId);
        return zone ? reads::ReadZoneChain(zone, out, kChainMax) : 0;
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        return 0;
    }
}

int ChainOfPlayer(ZoneFrame* out) {
    __try {
        uintptr_t actor, entity;
        if (!reads::LocalPlayer(actor, entity)) return 0;
        const uintptr_t zone = reads::EntityZone(entity);
        return zone ? reads::ReadZoneChain(zone, out, kChainMax) : 0;
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        return 0;
    }
}

struct Pose { double pos[3]; double rot[4]; uint64_t zone; };

bool ReadPlayerPose(Pose& p) {
    __try {
        uintptr_t actor, entity;
        if (!reads::LocalPlayer(actor, entity)) return false;
        const uintptr_t zone = reads::EntityZone(entity);
        if (!zone) return false;
        p.zone = reads::ZoneId(zone);
        if (!p.zone || reads::ZoneFromId(p.zone) != zone) return false;
        reads::EntityLocalPos(entity, p.pos);
        return reads::EntityRotation(entity, p.rot);
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        return false;
    }
}

uint64_t ZoneIdOfEntity(uint64_t entityId) {
    __try {
        const uintptr_t ent = reads::EntityFromId(entityId);
        const uintptr_t zone = ent ? reads::EntityZone(ent) : 0;
        if (!zone) return 0;
        const uint64_t id = reads::ZoneId(zone);
        return id && reads::ZoneFromId(id) == zone ? id : 0;
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        return 0;
    }
}

// The rotation whose matrix has columns a[0], a[1], a[2] (the world directions of a zone's axes).
Quatd FromAxes(const double a[3][3]) {
    const double m00 = a[0][0], m11 = a[1][1], m22 = a[2][2];
    const double m01 = a[1][0], m10 = a[0][1], m02 = a[2][0], m20 = a[0][2], m12 = a[2][1], m21 = a[1][2];
    const double t = m00 + m11 + m22;
    Quatd q;
    if (t > 0) {
        const double s = std::sqrt(t + 1.0) * 2.0;
        q = { 0.25 * s, (m21 - m12) / s, (m02 - m20) / s, (m10 - m01) / s };
    } else if (m00 > m11 && m00 > m22) {
        const double s = std::sqrt(1.0 + m00 - m11 - m22) * 2.0;
        q = { (m21 - m12) / s, 0.25 * s, (m01 + m10) / s, (m02 + m20) / s };
    } else if (m11 > m22) {
        const double s = std::sqrt(1.0 + m11 - m00 - m22) * 2.0;
        q = { (m02 - m20) / s, (m01 + m10) / s, 0.25 * s, (m12 + m21) / s };
    } else {
        const double s = std::sqrt(1.0 + m22 - m00 - m11) * 2.0;
        q = { (m10 - m01) / s, (m02 + m20) / s, (m12 + m21) / s, 0.25 * s };
    }
    return q.Normalized();
}

bool RightHanded(const double a[3][3]) {
    const Vector3d x{ a[0][0], a[0][1], a[0][2] }, y{ a[1][0], a[1][1], a[1][2] }, z{ a[2][0], a[2][1], a[2][2] };
    return x.Cross(y).Dot(z) > 0;
}

Transform WorldFrame(const ZoneFrame& f) {
    return { { f.origin[0], f.origin[1], f.origin[2] }, FromAxes(f.axis), 1.0 };
}

// Puts a chain (innermost first) into the tree, each zone relative to the next one up; the
// outermost is a root zone with its world frame. A mirrored frame (no rotation can express it)
// cuts the chain below it: the zones inside it still go in, the outermost of them as a root.
void Feed(const ZoneFrame* f, int n) {
    for (int i = 0; i < n; ++i)
        if (!RightHanded(f[i].axis)) { n = i; break; }
    for (int i = 0; i < n; ++i) {
        const Transform world = WorldFrame(f[i]);
        if (i + 1 < n) {
            const Transform parent = WorldFrame(f[i + 1]);
            const Transform local = { parent.InverseTransformPoint(world.position),
                                      (parent.rotation.Conjugate() * world.rotation).Normalized(), 1.0 };
            g_zones.Set(f[i].id, f[i + 1].id, f[i].name, local);
        } else {
            g_zones.Set(f[i].id, 0, f[i].name, world);
        }
    }
}

// A skipped/truncated/mirrored chain cannot establish direct ancestry. Verify every
// original parent against the copied chain; do not infer systems from name strings.
bool Ready();

int ExactChain(uint64_t id, bool player, ZoneFrame* out) {
    __try {
        uintptr_t zone = 0;
        if (player) {
            uintptr_t actor, entity;
            if (!reads::LocalPlayer(actor, entity)) return 0;
            zone = reads::EntityZone(entity);
        } else {
            zone = reads::ZoneFromId(id);
        }
        if (!zone) return 0;
        const int n = reads::ReadZoneChain(zone, out, kChainMax);
        if (!n || (!player && out[0].id != id)) return 0;
        for (int i = 0; i < n; ++i) {
            if (!zone || reads::ZoneId(zone) != out[i].id ||
                reads::ZoneFromId(out[i].id) != zone || !RightHanded(out[i].axis)) return 0;
            for (int k = 0; k < 3; ++k) {
                if (!std::isfinite(out[i].origin[k])) return 0;
                for (int j = 0; j < 3; ++j) if (!std::isfinite(out[i].axis[k][j])) return 0;
            }
            zone = reads::ZoneParent(zone);
        }
        return zone ? 0 : n;
    } __except (EXCEPTION_EXECUTE_HANDLER) { return 0; }
}

bool ObserveChain(const ZoneFrame* f, int n) {
    if (n <= 0) return false;
    engine::ZoneTree chain;
    for (int i = 0; i < n; ++i) {
        const Transform world = WorldFrame(f[i]);
        Transform local = world;
        if (i + 1 < n) {
            const Transform parent = WorldFrame(f[i + 1]);
            local = {parent.InverseTransformPoint(world.position),
                     (parent.rotation.Conjugate() * world.rotation).Normalized(), 1.0};
        }
        chain.Set(f[i].id, i + 1 < n ? f[i + 1].id : 0, f[i].name, local);
    }
    if (!g_metadata.Observe(chain, f[0].id)) return false;
    // Snapshot can drop all descendants after ancestry/name changes. Rebuild only
    // accepted bounded records, preserving existing frame data for unchanged IDs.
    for (int i = 0; i < n; ++i) {
        engine::Zone z;
        if (chain.Find(f[i].id, &z)) g_metadataZones.Set(z.id, z.parentId, z.name, (&z)->local);
    }
    // Clear stale frame entries; they must never enable conversion after invalidation.
    // The source is bounded; rebuilding copies no game pointers.
    engine::ZoneTree accepted;
    for (uint32_t i = 0; i < g_metadata.Count(); ++i) {
        engine::Zone z;
        if (g_metadataZones.Find(g_metadata.Id(i), &z)) accepted.Set(z.id, z.parentId, z.name, (&z)->local);
    }
    g_metadataZones.Clear();
    for (uint32_t i = 0; i < g_metadata.Count(); ++i) {
        engine::Zone z;
        if (accepted.Find(g_metadata.Id(i), &z)) g_metadataZones.Set(z.id, z.parentId, z.name, (&z)->local);
    }
    return true;
}

bool ObserveId(uint64_t id) {
    if (!id) return true;
    ZoneFrame chain[kChainMax];
    return ObserveChain(chain, ExactChain(id, false, chain));
}

// Refuse mid-tick streaming/reparenting instead of returning a cached ID as live.
bool MetadataLive() {
    __try {
        for (uint32_t i = 0; i < g_metadata.Count(); ++i) {
            const uint64_t id = g_metadata.Id(i);
            const uintptr_t zone = reads::ZoneFromId(id);
            sc_game_zone record = {}; record.size = sizeof(record);
            if (!zone || reads::ZoneId(zone) != id || !g_metadata.Get(id, &record)) return false;
            const uintptr_t parent = reads::ZoneParent(zone);
            const uint64_t parentId = parent ? reads::ZoneId(parent) : 0;
            if (!spatial::ParentMatches(record.parent_id, parent != 0, parentId,
                                        parent && parentId && reads::ZoneFromId(parentId) == parent)) return false;
        }
        return true;
    } __except (EXCEPTION_EXECUTE_HANDLER) { return false; }
}

bool MetadataReady() {
    if (!Ready()) return false;
    if (MetadataLive()) return true;
    g_metadata.Reset(); g_metadataZones.Clear();
    return false;
}

void RefreshZones() {
    g_zones.Clear();
    g_metadata.Reset();
    g_metadataZones.Clear();
    if (!reads::Ready()) return;
    ZoneFrame chain[kChainMax];
    Feed(chain, ChainOfPlayer(chain));
    ObserveChain(chain, ExactChain(0, true, chain));
}

bool Ready() { return g_started && reads::Ready() && OnGameThread(); }

// The zone is in the tree, read this tick: the player's chain from the tick, or its own chain
// read now. 0, the world, always is.
bool EnsureZone(uint64_t zoneId) {
    if (!zoneId || g_zones.Has(zoneId)) return true;
    ZoneFrame chain[kChainMax];
    Feed(chain, ChainOfZoneId(zoneId, chain));
    return g_zones.Has(zoneId);
}

Vector3d V(const double p[3]) { return { p[0], p[1], p[2] }; }
void     Out(const Vector3d& v, double p[3]) { p[0] = v.x; p[1] = v.y; p[2] = v.z; }

int SvcPlayerPose(double pos[3], double rot[4], uint64_t* zoneId) {
    if (!Ready() || !pos || !rot || !zoneId) return 0;
    Pose p = {};
    if (!ReadPlayerPose(p)) return 0;
    memcpy(pos, p.pos, sizeof(p.pos));
    memcpy(rot, p.rot, sizeof(p.rot));
    *zoneId = p.zone;
    return 1;
}

int SvcZoneOfEntity(uint64_t entityId, uint64_t* zoneId) {
    if (!Ready() || !zoneId) return 0;
    const uint64_t id = ZoneIdOfEntity(entityId);
    if (!id || !EnsureZone(id)) return 0;
    *zoneId = id;
    return 1;
}

int SvcLocalToWorld(uint64_t zoneId, const double local[3], double world[3]) {
    if (!Ready() || !local || !world || !EnsureZone(zoneId)) return 0;
    Vector3d w;
    if (!g_zones.LocalToWorld(zoneId, V(local), &w)) return 0;
    Out(w, world);
    return 1;
}

int SvcWorldToLocal(uint64_t zoneId, const double world[3], double local[3]) {
    if (!Ready() || !world || !local || !EnsureZone(zoneId)) return 0;
    Vector3d l;
    if (!g_zones.WorldToLocal(zoneId, V(world), &l)) return 0;
    Out(l, local);
    return 1;
}

int SvcZoneToZone(uint64_t from, uint64_t to, const double in[3], double out[3]) {
    if (!Ready() || !in || !out || !EnsureZone(from) || !EnsureZone(to)) return 0;
    Vector3d p;
    if (!g_zones.Transform(from, to, V(in), &p)) return 0;
    Out(p, out);
    return 1;
}

int SvcZoneName(uint64_t zoneId, char* out, uint32_t cap) {
    if (!Ready() || !out || !cap || !zoneId || !EnsureZone(zoneId)) return 0;
    engine::Zone z;
    if (!g_zones.Find(zoneId, &z)) return 0;
    const size_t n = z.name.size() < cap - 1 ? z.name.size() : cap - 1;
    memcpy(out, z.name.data(), n);
    out[n] = 0;
    return 1;
}

int SvcPoseToZone(uint64_t from, uint64_t to, const double pos[3], const double rot[4],
                  double outPos[3], double outRot[4]) {
    if (!MetadataReady() || !ObserveId(from) || !ObserveId(to)) return 0;
    // Observing the second frame may invalidate an earlier reparented frame.
    return spatial::Convert(g_metadataZones, from, to, pos, rot, outPos, outRot) ? 1 : 0;
}

int SvcZoneRecord(uint64_t id, sc_game_zone* out) {
    if (!MetadataReady() || !id || !out || out->size < sizeof(*out) || !ObserveId(id)) return 0;
    return g_metadata.Get(id, out) ? 1 : 0;
}

int SvcFindZone(const char* name, sc_game_zone* out) {
    return MetadataReady() && g_metadata.Find(name, out) ? 1 : 0;
}

int SvcListZones(uint64_t epoch, uint32_t offset, sc_game_zone* out, uint32_t stride,
                 uint32_t cap, uint32_t* count, uint32_t* more, uint64_t* outEpoch) {
    return MetadataReady() && g_metadata.List(epoch, offset, out, stride, cap, count, more, outEpoch) ? 1 : 0;
}

const sc_game_spatial_v1 kGameSpatial = {
    sizeof(sc_game_spatial_v1), SvcPoseToZone, SvcZoneRecord, SvcFindZone, SvcListZones,
};

void WithdrawSpatial() {
    StopRelocation();
    host::WithdrawGameService(SC_GAME_SPATIAL_NAME);
    host::WithdrawGameService(SC_SPATIAL_SERVICE_NAME);
}

const sc_spatial_v1 kSpatial = {
    sizeof(sc_spatial_v1), SvcPlayerPose, SvcZoneOfEntity, SvcLocalToWorld, SvcWorldToLocal, SvcZoneToZone, SvcZoneName,
};

void OnTick(const char*, const void*, void*) {
    RefreshZones();
    TickRelocation();
}

}  // namespace

Result Start() {
    if (g_started) return Result::Ok;
    if (!reads::Init()) Log("[game] teleport.* rows not OK: teleport.spatial answers 0 (see the [core] lines)");
    if (StartGameBuild() != Result::Ok) Log("[game] game.build did not publish (image header unreadable)");
    Result r = host::ProvideGameService(SC_SPATIAL_SERVICE_NAME, SC_SPATIAL_SERVICE_VERSION, &kSpatial);
    if (r != Result::Ok) return r;
    r = host::ProvideGameService(SC_GAME_SPATIAL_NAME, SC_GAME_SPATIAL_VERSION, &kGameSpatial);
    if (r != Result::Ok) {
        host::WithdrawGameService(SC_SPATIAL_SERVICE_NAME);
        return r;
    }
    r = StartRelocation(EngineRelocationBackend());
    if (r != Result::Ok) { WithdrawSpatial(); return r; }
    r = StartSpawn();
    if (r != Result::Ok) {
        WithdrawSpatial();
        return r;
    }
    r = StartActors();
    if (r != Result::Ok) {
        StopSpawn();
        WithdrawSpatial();
        return r;
    }
    r = StartVehicles();
    if (r != Result::Ok) {
        StopActors();
        StopSpawn();
        WithdrawSpatial();
        return r;
    }
    r = StartWorld();
    if (r != Result::Ok) {
        StopVehicles();
        StopActors();
        StopSpawn();
        WithdrawSpatial();
        return r;
    }
    r = StartEvents();
    if (r != Result::Ok) {
        StopWorld();
        StopVehicles();
        StopActors();
        StopSpawn();
        WithdrawSpatial();
        return r;
    }
    r = StartEntities();
    if (r != Result::Ok) {
        StopEvents();
        StopWorld();
        StopVehicles();
        StopActors();
        StopSpawn();
        WithdrawSpatial();
        return r;
    }
    r = StartCreative();
    if (r != Result::Ok) {
        StopEntities();
        StopEvents();
        StopWorld();
        StopVehicles();
        StopActors();
        StopSpawn();
        WithdrawSpatial();
        return r;
    }
    r = StartLoadout(nullptr, ProductionLoadoutBackend()); // synchronous 1.1 stays unavailable
    if (r != Result::Ok) {
        StopCreative(); StopEntities(); StopEvents(); StopWorld();
        StopVehicles(); StopActors(); StopSpawn();
        host::WithdrawGameService(SC_SPATIAL_SERVICE_NAME);
        return r;
    }
    (void)ConfigureDefaultProductionQuantum(); // unresolved rows or unknown admission keep the empty, OFF configuration
    r = StartProductionQuantum(SpawnReleaseHooked()); // approved G8; absent adapter still fails closed
    if (r != Result::Ok) {
        StopLoadout(); StopCreative(); StopEntities(); StopEvents(); StopWorld();
        StopVehicles(); StopActors(); StopSpawn();
        host::WithdrawGameService(SC_SPATIAL_SERVICE_NAME);
        return r;
    }
    r = StartCvars(); // default cvar port remains unavailable
    if (r != Result::Ok) {
        StopQuantum(); StopLoadout(); StopCreative(); StopEntities(); StopEvents();
        StopWorld(); StopVehicles(); StopActors(); StopSpawn();
        host::WithdrawGameService(SC_SPATIAL_SERVICE_NAME);
        return r;
    }
    r = StartHangar(); // both capabilities held OFF pending an in-game run
    if (r != Result::Ok) {
        StopCvars(); StopQuantum(); StopLoadout(); StopCreative(); StopEntities(); StopEvents();
        StopWorld(); StopVehicles(); StopActors(); StopSpawn();
        host::WithdrawGameService(SC_SPATIAL_SERVICE_NAME);
        return r;
    }
    r = StartReputation(); // game.reputation.checks held OFF pending an in-game run
    if (r != Result::Ok) {
        StopHangar(); StopCvars(); StopQuantum(); StopLoadout(); StopCreative(); StopEntities(); StopEvents();
        StopWorld(); StopVehicles(); StopActors(); StopSpawn();
        host::WithdrawGameService(SC_SPATIAL_SERVICE_NAME);
        return r;
    }
    r = StartAsop(); // game.asop.claim_timeout and game.asop.shard_persisted held OFF pending an in-game run
    if (r != Result::Ok) {
        StopReputation(); StopHangar(); StopCvars(); StopQuantum(); StopLoadout(); StopCreative(); StopEntities(); StopEvents();
        StopWorld(); StopVehicles(); StopActors(); StopSpawn();
        host::WithdrawGameService(SC_SPATIAL_SERVICE_NAME);
        return r;
    }
    r = StartContracts(); // game.contracts.stream_radius held OFF pending an in-game run
    if (r != Result::Ok) {
        StopAsop(); StopReputation(); StopHangar(); StopCvars(); StopQuantum(); StopLoadout(); StopCreative(); StopEntities();
        StopEvents(); StopWorld(); StopVehicles(); StopActors(); StopSpawn();
        host::WithdrawGameService(SC_SPATIAL_SERVICE_NAME);
        return r;
    }
    r = Subscribe(host::GameOwner(), "tick", OnTick, nullptr);
    if (r != Result::Ok) {
        StopContracts(); StopAsop(); StopReputation(); StopHangar(); StopCvars(); StopQuantum(); StopLoadout();
        StopCreative();
        StopEntities();
        StopEvents();
        StopWorld();
        StopVehicles();
        StopActors();
        StopSpawn();
        WithdrawSpatial();
        return r;
    }
    r = offline::Start(); // published unavailable until a separately validated adapter exists
    if (r != Result::Ok) {
        Unsubscribe(host::GameOwner(), "tick", OnTick);
        StopContracts(); StopAsop(); StopReputation(); StopHangar(); StopCreative(); StopEntities(); StopEvents(); StopWorld();
        StopVehicles(); StopActors(); StopSpawn();
        host::WithdrawGameService(SC_SPATIAL_SERVICE_NAME);
        return r;
    }
    (void)StartInventory(); // game.inventory.read held OFF; a failed publish leaves the pack up
    g_started = true;
    r = StartInput();
    if (r != Result::Ok) { Stop(); return r; }
    r = StartProductionContainerContents(); // empty until a grounded host-owned provider is configured
    if (r != Result::Ok) { Stop(); return r; }
    r = StartAudio(); // publishes unavailable; no native audio backend is installed
    if (r != Result::Ok) { Stop(); return r; }
    r = StartGameRules(); // metadata available; live reads still require a verified backend
    if (r != Result::Ok) { Stop(); return r; }
    Log("[game] game.spatial 1.0 published: observed chains only; system identity unknown");
    Log("[game] game services published: %s 1.0, %s 1.2, %s 1.1, %s 1.0, %s 1.0, %s 1.0, %s 1.0 (+ the game.* events)", SC_SPATIAL_SERVICE_NAME,
        SC_SPAWN_SERVICE_NAME, SC_ACTORS_NAME, SC_VEHICLES_SERVICE_NAME, SC_WORLD_NAME, SC_ENTITIES_NAME,
        SC_CREATIVE_SERVICE_NAME);
    return Result::Ok;
}

void Stop() {
    StopGameBuild();
    if (!g_started) return;
    StopInventory();
    StopContainerContents();
    (void)StopAudio();
    Unsubscribe(host::GameOwner(), "tick", OnTick);
    StopGameRules();
    StopInput();
    StopContracts();
    StopAsop();
    StopReputation();
    StopHangar();
    StopCvars();
    StopQuantum();
    StopLoadout();
    offline::Stop();
    StopCreative();
    StopEntities();
    StopEvents();
    StopWorld();
    StopVehicles();
    StopActors();
    StopSpawn();
    WithdrawSpatial();
    g_metadata.Reset();
    g_metadataZones.Clear();
    g_zones.Clear();
    g_started = false;
}

bool Started() { return g_started; }

uint64_t ZoneOfEntity(uint64_t entityId) {
    if (!Ready()) return 0;
    const uint64_t id = ZoneIdOfEntity(entityId);
    return id && EnsureZone(id) ? id : 0;
}

bool PoseToZone(uint64_t from, uint64_t to, const double pos[3], const double rot[4], double outPos[3],
                double outRot[4]) {
    if (!Ready() || !EnsureZone(from) || !EnsureZone(to)) return false;
    return spatial::Convert(g_zones, from, to, pos, rot, outPos, outRot);
}

}  // namespace sco::game::services
