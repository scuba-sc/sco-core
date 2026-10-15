#pragma once
// GPL-3.0-only. Portable state machinery used by the game pack provider.
#include <sc_relocation.h>
#include <array>
#include <cmath>
#include <cstring>
namespace sco::game::services::relocation {
struct Evidence {
    bool offline, transport, player;
    uint64_t session, playerId, currentSystem, targetSystem;
    bool streamed, arrived, applied, fault;
};
inline bool Arrived(const sc_relocation_request& r, const double p[3], const double q[4]) {
    if (!p || !q) return false;
    double distance=0, norm=0, targetNorm=0, dot=0;
    for (int i=0;i<3;++i) {
        if (!std::isfinite(p[i]) || !std::isfinite(r.pos[i])) return false;
        distance=std::hypot(distance,p[i]-r.pos[i]);
    }
    for (int i=0;i<4;++i) {
        if (!std::isfinite(q[i]) || !std::isfinite(r.rot_xyzw[i])) return false;
        norm=std::hypot(norm,q[i]); targetNorm=std::hypot(targetNorm,r.rot_xyzw[i]);
    }
    if (!std::isfinite(norm) || !std::isfinite(targetNorm) || norm<1e-12 || targetNorm<1e-12) return false;
    for (int i=0;i<4;++i) dot+=(q[i]/norm)*(r.rot_xyzw[i]/targetNorm);
    return distance<=0.05 && std::abs(dot)>=0.999999;
}
// The game's zone accessors, passed in so the system classifier below is portable and testable.
struct ZoneOps {
    uintptr_t (*parent)(uintptr_t);
    const char* (*name)(uintptr_t);
    uint64_t (*id)(uintptr_t);
    uintptr_t (*fromId)(uint64_t);
};
// The id of the "SolarSystem*" zone a zone belongs to, or 0 when unknown: no such ancestor within 64
// parents (the walk is bounded against a parent cycle), or an id that does not lead back to the zone.
// Names follow the v0.7.7 classifier (SystemZoneOf: case-insensitive "SolarSystem" prefix).
// Unlike v0.7.7 an unknown current system is never treated as "same system".
inline uint64_t SystemOfZone(uintptr_t zone, const ZoneOps& ops) {
    static const char prefix[] = "SolarSystem";
    for (int steps = 0; zone && steps < 64; ++steps, zone = ops.parent(zone)) {
        const char* name = ops.name(zone);
        bool match = name != nullptr;
        for (size_t i = 0; match && i < sizeof(prefix) - 1; ++i) {
            const char c = name[i];
            const char lower = (c >= 'A' && c <= 'Z') ? static_cast<char>(c - 'A' + 'a') : c;
            const char want = prefix[i] >= 'A' && prefix[i] <= 'Z' ? static_cast<char>(prefix[i] - 'A' + 'a') : prefix[i];
            match = c != 0 && lower == want;
        }
        if (!match) continue;
        const uint64_t id = ops.id(zone);
        return id && ops.fromId(id) == zone ? id : 0;
    }
    return 0;
}
// Quaternion (x y z w) of the rotation whose matrix columns are axis[0..2], the world directions of a
// zone's local axes. False unless the axes form a proper rotation (orthonormal, determinant +1).
inline bool QuatFromAxes(const double axis[3][3], double q[4]) {
    for (int i = 0; i < 3; ++i)
        for (int k = 0; k < 3; ++k)
            if (!std::isfinite(axis[i][k])) return false;
    double dotSelf[3], dotCross[3];
    for (int i = 0; i < 3; ++i) {
        dotSelf[i] = axis[i][0] * axis[i][0] + axis[i][1] * axis[i][1] + axis[i][2] * axis[i][2];
        const int j = (i + 1) % 3;
        dotCross[i] = axis[i][0] * axis[j][0] + axis[i][1] * axis[j][1] + axis[i][2] * axis[j][2];
        if (std::abs(dotSelf[i] - 1.0) > 1e-6 || std::abs(dotCross[i]) > 1e-6) return false;
    }
    const double det = axis[0][0] * (axis[1][1] * axis[2][2] - axis[1][2] * axis[2][1]) -
                       axis[0][1] * (axis[1][0] * axis[2][2] - axis[1][2] * axis[2][0]) +
                       axis[0][2] * (axis[1][0] * axis[2][1] - axis[1][1] * axis[2][0]);
    if (std::abs(det - 1.0) > 1e-6) return false;
    const double m00 = axis[0][0], m10 = axis[0][1], m20 = axis[0][2];
    const double m01 = axis[1][0], m11 = axis[1][1], m21 = axis[1][2];
    const double m02 = axis[2][0], m12 = axis[2][1], m22 = axis[2][2];
    const double trace = m00 + m11 + m22;
    double x, y, z, w;
    if (trace > 0) {
        const double s = std::sqrt(trace + 1.0) * 2;
        w = 0.25 * s; x = (m21 - m12) / s; y = (m02 - m20) / s; z = (m10 - m01) / s;
    } else if (m00 > m11 && m00 > m22) {
        const double s = std::sqrt(1.0 + m00 - m11 - m22) * 2;
        w = (m21 - m12) / s; x = 0.25 * s; y = (m01 + m10) / s; z = (m02 + m20) / s;
    } else if (m11 > m22) {
        const double s = std::sqrt(1.0 + m11 - m00 - m22) * 2;
        w = (m02 - m20) / s; x = (m01 + m10) / s; y = 0.25 * s; z = (m12 + m21) / s;
    } else {
        const double s = std::sqrt(1.0 + m22 - m00 - m11) * 2;
        w = (m10 - m01) / s; x = (m02 + m20) / s; y = (m12 + m21) / s; z = 0.25 * s;
    }
    q[0] = x; q[1] = y; q[2] = z; q[3] = w;
    return true;
}
// zoneAxes * local (Hamilton product, x y z w): the world orientation of a rotation given in the zone's
// frame. Normalised. False when the zone's frame is not a proper rotation or the product is not finite.
inline bool WorldRotation(const double zoneAxes[3][3], const double local[4], double world[4]) {
    double a[4];
    if (!QuatFromAxes(zoneAxes, a)) return false;
    const double b0 = local[0], b1 = local[1], b2 = local[2], b3 = local[3];
    double r[4] = {
        a[3] * b0 + a[0] * b3 + a[1] * b2 - a[2] * b1,
        a[3] * b1 - a[0] * b2 + a[1] * b3 + a[2] * b0,
        a[3] * b2 + a[0] * b1 - a[1] * b0 + a[2] * b3,
        a[3] * b3 - a[0] * b0 - a[1] * b1 - a[2] * b2,
    };
    double norm = 0;
    for (double v : r) norm = std::hypot(norm, v);
    if (!std::isfinite(norm) || norm < 1e-12) return false;
    for (int i = 0; i < 4; ++i) world[i] = r[i] / norm;
    return true;
}
class Machine {
    struct Slot {
        const void* owner = nullptr;
        uint64_t session = 0, player = 0, system = 0, since = 0;
        sc_relocation_request request{};
        sc_relocation_status status{};
    };
public:
    sco_result Request(const void* owner, const sc_relocation_request& r, const Evidence& e,
                       uint64_t now, uint64_t* ticket) {
        if (!owner || !ticket || r.size < sizeof(r) || !r.zone_id || !r.timeout_ms || r.timeout_ms > 60000)
            return SCO_BAD_ARG;
        double norm = 0;
        for (double x : r.pos) if (!std::isfinite(x) || std::abs(x) > 1e12) return SCO_BAD_ARG;
        for (double x : r.rot_xyzw) {
            if (!std::isfinite(x)) return SCO_BAD_ARG;
            norm = std::hypot(norm,x);
        }
        if (!std::isfinite(norm) || norm < 1e-12) return SCO_BAD_ARG;
        if (e.fault || !e.offline || !e.transport || !e.player || !e.session || !e.playerId || !e.streamed ||
            !e.currentSystem || !e.targetSystem || e.currentSystem != e.targetSystem) return SCO_UNAVAILABLE;
        for (const auto& s : slots_) if (s.status.state == SC_RELOCATION_PENDING) return SCO_TOO_MANY;
        Slot* chosen = nullptr;
        for (auto& s : slots_) if (!s.owner) { chosen = &s; break; }
        if (!chosen) {
            chosen = &slots_[0];
            for (auto& s : slots_) if (s.status.ticket < chosen->status.ticket) chosen = &s;
        }
        if (sequence_ == UINT64_MAX) return SCO_TOO_MANY; // never recycle a ticket
        *chosen = {};
        chosen->owner = owner; chosen->session = e.session; chosen->player = e.playerId;
        chosen->system = e.currentSystem; chosen->since = now; chosen->request = r;
        for (double& x : chosen->request.rot_xyzw) x /= norm;
        chosen->status = {sizeof(sc_relocation_status),SC_RELOCATION_PENDING,0,0,++sequence_};
        *ticket = sequence_; return SCO_OK;
    }
    sco_result Cancel(const void* owner, uint64_t ticket) {
        if (!owner || !ticket) return SCO_BAD_ARG;
        auto* s = Find(owner,ticket);
        if (!s) return SCO_NOT_FOUND;
        if (s->status.state == SC_RELOCATION_PENDING) s->status.state = SC_RELOCATION_CANCELLED;
        return SCO_OK;
    }
    sco_result Status(const void* owner, uint64_t ticket, sc_relocation_status* out) {
        if (!owner || !ticket || !out || out->size < sizeof(*out)) return SCO_BAD_ARG;
        auto* s = Find(owner,ticket);
        if (!s) return SCO_NOT_FOUND;
        std::memcpy(reinterpret_cast<char*>(out)+sizeof(out->size),
                    reinterpret_cast<const char*>(&s->status)+sizeof(s->status.size),sizeof(*out)-sizeof(out->size));
        return SCO_OK;
    }
    // Provider calls Tick before any move; only a still-admitted request can be applied.
    bool Tick(uint64_t now, const Evidence& e) {
        Slot* s = Active(); if (!s) return false;
        uint32_t reason = 0;
        if (e.fault) reason = SC_RELOCATION_REASON_FAULT;
        else if (!e.offline) reason = SC_RELOCATION_REASON_SAFETY;
        else if (!e.player || !e.session || s->session != e.session || s->player != e.playerId)
            reason = SC_RELOCATION_REASON_SESSION;
        else if (!e.currentSystem || e.currentSystem != s->system ||
                 (e.streamed && (!e.targetSystem || e.targetSystem != s->system)))
            reason = SC_RELOCATION_REASON_SYSTEM;
        else if (!e.transport) reason = SC_RELOCATION_REASON_TRANSPORT;
        else if (now < s->since || now-s->since >= s->request.timeout_ms) reason = SC_RELOCATION_REASON_TIMEOUT;
        if (reason) { s->status.state=SC_RELOCATION_FAILED; s->status.reason=reason; return false; }
        if (s->status.applied && e.streamed && e.arrived) { s->status.state=SC_RELOCATION_COMPLETE; return false; }
        return e.streamed && !s->status.applied;
    }
    void Applied(bool ok, bool fault = false) {
        Slot* s = Active(); if (!s) return;
        if (ok) s->status.applied=1;
        else { s->status.state=SC_RELOCATION_FAILED;
               s->status.reason=fault ? SC_RELOCATION_REASON_FAULT : SC_RELOCATION_REASON_TRANSPORT; }
    }
    const sc_relocation_request* Pending() { auto* s=Active(); return s ? &s->request : nullptr; }
    void Release(const void* owner) { for (auto& s : slots_) if (s.owner==owner) s={}; }
    void Stop() { for (auto& s : slots_) s={}; } // sequence retained across restart
private:
    Slot* Find(const void* owner,uint64_t ticket) {
        for (auto& s : slots_) if (s.owner==owner && s.status.ticket==ticket) return &s;
        return nullptr;
    }
    Slot* Active() { for (auto& s : slots_) if (s.status.state==SC_RELOCATION_PENDING) return &s; return nullptr; }
    std::array<Slot,32> slots_{};
    uint64_t sequence_ = 0;
};
} // namespace sco::game::services::relocation
