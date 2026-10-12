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
