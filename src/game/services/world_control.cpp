// GPL-3.0-only. Policy implementation with no engine offsets or OS dependencies.
#include "world_control.h"
#include <algorithm>
#include <cmath>
#include <cstring>
#include <limits>
namespace sco::game::services {
namespace {
struct Busy { bool& b; explicit Busy(bool& v) : b(v) { b = true; } ~Busy() { b = false; } };
bool Finite(const double* v, unsigned n) {
    if (!v) return false;
    for (unsigned i = 0; i < n; ++i) if (!std::isfinite(v[i])) return false;
    return true;
}
void Zero(sc_world_hit* h) {
    const auto size = h->size; std::memset(h, 0, sizeof(*h)); h->size = size;
}
}
sco_result WorldControl::Admission(sco_plugin* self) const {
    if (!p_.on_thread || !p_.on_thread()) return SCO_WRONG_THREAD;
    if (!self || !p_.live || !p_.live(self)) return SCO_BAD_ARG;
    if (busy_) return SCO_FAILED;
    if (stopped_ || failed_) return SCO_UNAVAILABLE;
    return SCO_OK;
}
sco_result WorldControl::Ray(sco_plugin* self, uint64_t zone, const double* from,
    const double* dir, double distance, const uint64_t* ids, uint32_t count, sc_world_hit* out) {
    if (!out || out->size < sizeof(*out)) return SCO_BAD_ARG;
    Zero(out);
    auto r = Admission(self); if (r != SCO_OK) return r;
    if (!zone || !Finite(from, 3) || !Finite(dir, 3) || !std::isfinite(distance) ||
        distance <= 0 || count > SC_WORLD_CONTROL_MAX_SKIPS || (count && !ids)) return SCO_BAD_ARG;
    const double norm = std::hypot(dir[0], dir[1], dir[2]);
    if (!std::isfinite(norm) || norm == 0) return SCO_BAD_ARG;
    uint64_t skips[SC_WORLD_CONTROL_MAX_SKIPS] = {};
    for (uint32_t i = 0; i < count; ++i) {
        if (!ids[i]) return SCO_BAD_ARG;
        for (uint32_t j = 0; j < i; ++j) if (skips[j] == ids[i]) return SCO_BAD_ARG;
        skips[i] = ids[i];
    }
    if (!p_.ray || (count && !p_.owned)) return SCO_UNAVAILABLE;
    Busy guard(busy_);
    for (uint32_t i = 0; i < count; ++i) if (!p_.owned(self, skips[i])) return SCO_FAILED;
    const double unit[3] = {dir[0]/norm, dir[1]/norm, dir[2]/norm};
    sc_world_hit hit{}; hit.size = sizeof(hit);
    r = p_.ray(zone, from, unit, std::min(distance, double(SC_WORLD_RAY_MAX_DISTANCE)), skips, count, &hit);
    if (r != SCO_OK) return r;
    if (!Finite(hit.pos, 3) || !std::isfinite(hit.distance) || hit.distance < 0 ||
        hit.distance > std::min(distance, double(SC_WORLD_RAY_MAX_DISTANCE))) return SCO_FAILED;
    // Unknown entity/normal are not fabricated even if a provider supplies them.
    std::memcpy(out->pos, hit.pos, sizeof(out->pos)); out->distance = hit.distance;
    return SCO_OK;
}
sco_result WorldControl::Rays(sco_plugin* self, uint64_t zone, const double* from,
    const double* dir, double distance, const uint64_t* ids, uint32_t count, sc_world_hits* out) {
    if (!out || out->size < sizeof(*out)) return SCO_BAD_ARG;
    const auto size = out->size, capacity = out->count;
    std::memset(out, 0, sizeof(*out)); out->size = size;
    auto r = Admission(self); if (r != SCO_OK) return r;
    if (capacity > SC_WORLD_CONTROL_MAX_HITS || !zone || !Finite(from, 3) ||
        !Finite(dir, 3) || !std::isfinite(distance) || distance <= 0 ||
        count > SC_WORLD_CONTROL_MAX_SKIPS || (count && !ids)) return SCO_BAD_ARG;
    const double norm = std::hypot(dir[0], dir[1], dir[2]);
    if (!std::isfinite(norm) || norm == 0) return SCO_BAD_ARG;
    uint64_t skips[SC_WORLD_CONTROL_MAX_SKIPS]{};
    for (uint32_t i = 0; i < count; ++i) {
        if (!ids[i]) return SCO_BAD_ARG;
        for (uint32_t j = 0; j < i; ++j) if (skips[j] == ids[i]) return SCO_BAD_ARG;
        skips[i] = ids[i];
    }
    if (!p_.ray_acquire || !p_.ray_query || !p_.ray_release || (count && !p_.owned))
        return SCO_UNAVAILABLE;
    Busy guard(busy_);
    for (uint32_t i = 0; i < count; ++i) if (!p_.owned(self, skips[i])) return SCO_FAILED;
    void* scope = nullptr;
    r = p_.ray_acquire(zone, &scope);
    if (r != SCO_OK) return r;
    if (!scope) { (void)p_.ray_release(scope); failed_ = true; return SCO_FAILED; }
    const double unit[3]{dir[0]/norm, dir[1]/norm, dir[2]/norm};
    const double limit = std::min(distance, double(SC_WORLD_RAY_MAX_DISTANCE));
    sc_world_hits hits{}; hits.size = sizeof(hits); hits.count = SC_WORLD_CONTROL_MAX_HITS;
    r = p_.ray_query(scope, from, unit, limit, skips, count, &hits);
    if (p_.ray_release(scope) != SCO_OK) {
        fault_scope_ = scope; failed_ = true; return SCO_FAILED;
    }
    if (r != SCO_OK) return r;
    if (hits.size != sizeof(hits) || hits._pad || hits.count > SC_WORLD_CONTROL_MAX_HITS ||
        hits.more > std::numeric_limits<uint32_t>::max() - hits.count) return SCO_FAILED;
    for (uint32_t i = 0; i < hits.count; ++i) {
        const auto& h = hits.hits[i];
        if (h.size != sizeof(h) || (h.flags & ~(SC_WORLD_HIT_ENTITY | SC_WORLD_HIT_NORMAL)) ||
            !Finite(h.pos, 3) || !std::isfinite(h.distance) || h.distance < 0 || h.distance > limit ||
            (i && h.distance < hits.hits[i-1].distance) ||
            ((h.flags & SC_WORLD_HIT_ENTITY) ? !h.entity_id : h.entity_id != 0)) return SCO_FAILED;
        if (h.flags & SC_WORLD_HIT_NORMAL) {
            if (!Finite(h.normal, 3) || std::abs(std::hypot(h.normal[0], h.normal[1], h.normal[2])-1) > 1e-6)
                return SCO_FAILED;
        } else if (h.normal[0] != 0 || h.normal[1] != 0 || h.normal[2] != 0) return SCO_FAILED;
    }
    if (!hits.count && !hits.more) return SCO_NOT_FOUND;
    out->count = std::min(capacity, hits.count);
    out->more = hits.more + hits.count - out->count;
    std::memcpy(out->hits, hits.hits, out->count * sizeof(hits.hits[0]));
    return SCO_OK;
}
sco_result WorldControl::Acquire(sco_plugin* self, uint64_t* out) {
    if (!out) return SCO_BAD_ARG;
    *out = 0;
    auto r = Admission(self); if (r != SCO_OK) return r;
    if (!cleanup_ready_ || !p_.begin || !p_.step || !p_.restore) return SCO_UNAVAILABLE;
    if (owner_) {
        if (owner_ != self) return SCO_FAILED;
        *out = lease_; return SCO_OK;
    }
    if (next_ == std::numeric_limits<uint64_t>::max()) return SCO_TOO_MANY;
    Busy guard(busy_);
    r = p_.begin();
    if (r != SCO_OK) return r;
    owner_ = self; lease_ = next_++; *out = lease_; return SCO_OK;
}
sco_result WorldControl::CheckLease(sco_plugin* self, uint64_t token) const {
    if (!owner_ || !token || token != lease_) return SCO_NOT_FOUND;
    return owner_ == self ? SCO_OK : SCO_FAILED;
}
sco_result WorldControl::Restore() {
    if (!owner_) return SCO_OK;
    const auto r = p_.restore ? p_.restore() : SCO_UNAVAILABLE;
    if (r != SCO_OK) { failed_ = true; return SCO_FAILED; }
    owner_ = nullptr; lease_ = 0; return SCO_OK;
}
sco_result WorldControl::Step(sco_plugin* self, uint64_t token, const sc_world_camera_step* input) {
    auto r = Admission(self); if (r != SCO_OK) return r;
    r = CheckLease(self, token); if (r != SCO_OK) return r;
    if (!input || input->size < sizeof(*input)) return SCO_BAD_ARG;
    const auto step = *input; // caller tail ignored, no pointer retained
    if (step._pad || !Finite(step.translation, 3) || !Finite(step.rotation_xyzw, 4)) return SCO_BAD_ARG;
    if (std::hypot(step.translation[0], step.translation[1], step.translation[2]) > SC_WORLD_CONTROL_STEP_METRES)
        return SCO_BAD_ARG;
    const auto* q = step.rotation_xyzw;
    const double norm = std::hypot(std::hypot(q[0], q[1]), std::hypot(q[2], q[3]));
    if (!std::isfinite(norm) || std::abs(norm - 1.0) > 1e-6 ||
        2 * std::acos(std::min(1.0, std::abs(q[3]) / norm)) > SC_WORLD_CONTROL_STEP_RADIANS)
        return SCO_BAD_ARG;
    Busy guard(busy_);
    r = p_.step(&step);
    if (r != SCO_OK) { failed_ = true; (void)Restore(); return SCO_FAILED; }
    return SCO_OK;
}
sco_result WorldControl::Release(sco_plugin* self, uint64_t token) {
    // Recovery release remains possible after Stop/failure; no new mutations.
    if (!p_.on_thread || !p_.on_thread()) return SCO_WRONG_THREAD;
    if (!self || !p_.live || !p_.live(self)) return SCO_BAD_ARG;
    if (busy_) return SCO_FAILED;
    const auto r = CheckLease(self, token); if (r != SCO_OK) return r;
    Busy guard(busy_); return Restore();
}
sco_result WorldControl::ReleaseOwner(const void* owner) {
    if (!p_.on_thread || !p_.on_thread()) return SCO_WRONG_THREAD;
    if (busy_) return SCO_FAILED;
    if (!owner || owner != owner_) return SCO_OK;
    Busy guard(busy_); return Restore();
}
sco_result WorldControl::Start(bool cleanup_ready) {
    if (!p_.on_thread || !p_.on_thread()) return SCO_WRONG_THREAD;
    if (busy_ || owner_ || failed_) return SCO_FAILED;
    stopped_ = false; cleanup_ready_ = cleanup_ready; return SCO_OK;
}
sco_result WorldControl::Stop() {
    if (!p_.on_thread || !p_.on_thread()) return SCO_WRONG_THREAD;
    if (busy_) return SCO_FAILED;
    stopped_ = true;
    Busy guard(busy_); return Restore();
}
}
