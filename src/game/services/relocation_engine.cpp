// GPL-3.0-only. Engine adapter for game.relocation 1.0 (Windows, game pack). Ported from sc-offline v0.7.7
// teleport.cpp (GetLocalPlayer, SystemZoneOf, TeleportIntoZone) over the reads the game pack already has
// (sco/game/reads.h): same slots, same params block. Every callback here runs under the provider's fault
// guard (relocation.cpp), and only while the host's fresh offline admission is positive.
#include "relocation.h"
#include "relocation_model.h"
#include "sco/game/host.h"
#include "sco/game/reads.h"
#include "sco/signatures.h"
#include <cstring>

namespace sco::game::services {
namespace {
constexpr size_t kActorTeleportComponentSlot = 0x9D8;  // to_camera+0x368, pinned by teleport.to_camera and teleport.relocation
constexpr size_t kComponentMoveSlot = 0x158;           // to_camera+0x376, pinned by teleport.relocation
constexpr size_t kParamsBytes = 0x80;
constexpr size_t kParamsZoneId = 0x00;       // u64
constexpr size_t kParamsRotation = 0x08;     // double x y z w, in the zone's frame (teleport.relocation: stores at +0x25e..+0x27c)
constexpr size_t kParamsLocalPos = 0x28;     // double x y z, in the zone's frame
constexpr size_t kParamsScale = 0x40;        // double 1.0, as v0.7.7 and the game's own constant at to_camera+0x205
constexpr size_t kParamsWorldPos = 0x48;     // double x y z
constexpr size_t kParamsWorldRotation = 0x60;  // float x y z w, the same orientation in the world frame
constexpr size_t kParamsZoneChange = 0x7D;   // byte, 1 as v0.7.7 set it for every explicit zone target

uintptr_t Rd(uintptr_t p) { return *reinterpret_cast<const uintptr_t*>(p); }

const relocation::ZoneOps kZones{reads::ZoneParent, reads::ZoneName, reads::ZoneId, reads::ZoneFromId};

bool Player(RelocationPlayer* out) {
    uintptr_t actor = 0, entity = 0;
    uint64_t actorId = 0;
    if (!reads::LocalPlayer(actor, entity, actorId)) return false;
    out->id = actorId;
    out->instance = entity;
    out->system = relocation::SystemOfZone(reads::EntityZone(entity), kZones);
    return true;
}

bool Target(uint64_t zoneId, RelocationTarget* out) {
    const uintptr_t zone = reads::ZoneFromId(zoneId);
    out->streamed = zone && reads::ZoneId(zone) == zoneId;
    out->system = out->streamed ? relocation::SystemOfZone(zone, kZones) : 0;
    return true;
}

bool Move(const sc_relocation_request* request) {
    uintptr_t actor = 0, entity = 0;
    if (!reads::LocalPlayer(actor, entity)) return false;
    const uintptr_t zone = reads::ZoneFromId(request->zone_id);
    if (!zone || reads::ZoneId(zone) != request->zone_id) return false;
    reads::ZoneFrame frame{};
    if (reads::ReadZoneChain(zone, &frame, 1) != 1 || frame.id != request->zone_id) return false;
    double worldRotation[4];
    if (!relocation::WorldRotation(frame.axis, request->rot_xyzw, worldRotation)) return false;
    double world[3];
    reads::LocalToWorld(zone, request->pos, world);
    const float worldRotationF[4] = {static_cast<float>(worldRotation[0]), static_cast<float>(worldRotation[1]),
                                     static_cast<float>(worldRotation[2]), static_cast<float>(worldRotation[3])};
    alignas(16) uint8_t params[kParamsBytes] = {};
    std::memcpy(params + kParamsZoneId, &request->zone_id, sizeof(uint64_t));
    std::memcpy(params + kParamsRotation, request->rot_xyzw, 4 * sizeof(double));
    std::memcpy(params + kParamsLocalPos, request->pos, 3 * sizeof(double));
    const double one = 1.0;
    std::memcpy(params + kParamsScale, &one, sizeof(double));
    std::memcpy(params + kParamsWorldPos, world, 3 * sizeof(double));
    std::memcpy(params + kParamsWorldRotation, worldRotationF, sizeof(worldRotationF));
    params[kParamsZoneChange] = 1;
    using Getter = uintptr_t(__fastcall*)(uintptr_t);
    using Teleport = void(__fastcall*)(uintptr_t, void*);
    const uintptr_t component = reinterpret_cast<Getter>(Rd(Rd(actor) + kActorTeleportComponentSlot))(actor);
    if (!component) return false;
    reinterpret_cast<Teleport>(Rd(Rd(component) + kComponentMoveSlot))(component, params);
    return true;
}

bool Pose(uint64_t zoneId, double pos[3], double rot[4]) {
    uintptr_t actor = 0, entity = 0;
    if (!reads::LocalPlayer(actor, entity)) return false;
    const uintptr_t zone = reads::ZoneFromId(zoneId);
    if (!zone || reads::EntityZone(entity) != zone) return false;
    reads::EntityLocalPos(entity, pos);
    return reads::EntityRotation(entity, rot);
}
}

const RelocationBackend* EngineRelocationBackend() {
    static RelocationBackend backend;
    backend = {};
    backend.verified = reads::Ready() && sco::Sig("teleport.relocation") != nullptr;
    backend.offline = &host::FreshAdmissionAllowed;
    backend.player = Player;
    backend.target = Target;
    backend.move = Move;
    backend.pose = Pose;
    return &backend;
}
}
