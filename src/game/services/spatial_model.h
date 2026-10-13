#pragma once
// Portable shared game-provider logic. No game pointers, signatures or memory reads.
#include "sco/engine/zone.h"
#include <sc_game_spatial.h>
#include <algorithm>
#include <array>
#include <cstring>
#include <cmath>

namespace sco::game::services::spatial {

inline bool ParentMatches(uint64_t recorded, bool exists, uint64_t observed, bool roundTrips) {
    return exists ? recorded != 0 && recorded == observed && roundTrips : recorded == 0;
}

inline bool Rotation(const engine::ZoneTree& tree, uint64_t id, engine::Quatd& q) {
    q = engine::Quatd::Identity();
    for (int depth = 0; id; ++depth) {
        engine::Zone z;
        if (depth >= engine::ZoneTree::kMaxDepth || !tree.Find(id, &z)) return false;
        q = z.local.rotation * q;
        id = z.parentId;
    }
    q = q.Normalized();
    return true;
}

inline bool Convert(const engine::ZoneTree& tree, uint64_t from, uint64_t to,
                    const double pos[3], const double rot[4], double outPos[3], double outRot[4]) {
    if (!pos || !rot || !outPos || !outRot) return false;
    for (int i = 0; i < 3; ++i) if (!std::isfinite(pos[i])) return false;
    double length = 0;
    for (int i = 0; i < 4; ++i) {
        if (!std::isfinite(rot[i])) return false;
        length = std::hypot(length, rot[i]);
    }
    if (length < 1e-12 || !std::isfinite(length)) return false;
    const engine::Quatd input{rot[3] / length, rot[0] / length, rot[1] / length, rot[2] / length};
    engine::Vector3d p;
    engine::Quatd rf, rt;
    if (!tree.Transform(from, to, {pos[0], pos[1], pos[2]}, &p) ||
        !Rotation(tree, from, rf) || !Rotation(tree, to, rt)) return false;
    const engine::Quatd q = (rt.Conjugate() * rf * input).Normalized();
    if (!std::isfinite(p.x) || !std::isfinite(p.y) || !std::isfinite(p.z) ||
        !std::isfinite(q.w) || !std::isfinite(q.x) || !std::isfinite(q.y) || !std::isfinite(q.z)) return false;
    outPos[0] = p.x; outPos[1] = p.y; outPos[2] = p.z;
    engine::ToXYZW(q, outRot);
    return true;
}

// Fixed storage, transactional admission; callers supply only complete observed chains.
class Snapshot {
public:
    void Reset() { count_ = 0; Advance(); }
    uint32_t Count() const { return count_; }
    uint64_t Id(uint32_t i) const { return i < count_ ? records_[i].zone_id : 0; }

    bool Observe(const engine::ZoneTree& tree, uint64_t id) {
        if (!id) return false;
        std::array<sc_game_zone, engine::ZoneTree::kMaxDepth> chain{};
        uint32_t n = 0;
        for (uint64_t next = id; next;) {
            engine::Zone z;
            if (n == chain.size() || !tree.Find(next, &z)) return false;
            for (uint32_t i = 0; i < n; ++i) if (chain[i].zone_id == next) return false;
            auto& record = chain[n++];
            record.size = sizeof(record);
            record.flags = SC_GAME_ZONE_SYSTEM_UNKNOWN;
            record.zone_id = next; record.parent_id = z.parentId;
            const size_t bytes = std::min(z.name.size(), sizeof(record.name) - 1);
            // The read boundary already copies into 96 bytes. Treat a full slot
            // conservatively as possibly truncated, even if the original was 95.
            if (z.name.size() >= sizeof(record.name) - 1) record.flags |= SC_GAME_ZONE_NAME_TRUNCATED;
            std::memcpy(record.name, z.name.data(), bytes);
            next = z.parentId;
        }
        uint32_t added = 0;
        for (uint32_t i = 0; i < n; ++i) {
            chain[i].root_id = chain[n - 1].zone_id;
            if (Index(chain[i].zone_id) == count_) ++added;
        }
        if (added > SC_GAME_SPATIAL_MAX_ZONES - count_) return false;
        bool changedExisting = false;
        for (uint32_t i = 0; i < n; ++i) {
            const uint32_t slot = Index(chain[i].zone_id);
            if (slot < count_) {
                auto old = records_[slot]; old.epoch = 0;
                if (std::memcmp(&old, &chain[i], sizeof(old)) != 0) changedExisting = true;
            }
        }
        // Reparent/name changes could invalidate descendants outside this chain.
        // Discard the whole bounded observation set rather than invent their ancestry.
        if (changedExisting) count_ = 0;
        bool changed = changedExisting;
        for (uint32_t i = 0; i < n; ++i) {
            uint32_t slot = Index(chain[i].zone_id);
            if (slot == count_) { ++count_; changed = true; }
            else {
                auto old = records_[slot]; old.epoch = 0;
                if (std::memcmp(&old, &chain[i], sizeof(old)) != 0) changed = true;
            }
            records_[slot] = chain[i];
        }
        if (changed) Advance();
        for (uint32_t i = 0; i < count_; ++i) records_[i].epoch = epoch_;
        std::sort(records_.begin(), records_.begin() + count_,
                  [](const auto& a, const auto& b) { return a.zone_id < b.zone_id; });
        return true;
    }

    bool Get(uint64_t id, sc_game_zone* out) const {
        const uint32_t i = Index(id);
        if (!out || out->size < sizeof(*out) || i == count_) return false;
        Copy(records_[i], out); return true;
    }
    bool Find(const char* name, sc_game_zone* out) const {
        if (!name || !*name || !out || out->size < sizeof(*out)) return false;
        size_t len = 0;
        while (len < sizeof(sc_game_zone::name) && name[len]) ++len;
        if (len == sizeof(sc_game_zone::name)) return false;
        uint32_t found = count_;
        for (uint32_t i = 0; i < count_; ++i) {
            if (!(records_[i].flags & SC_GAME_ZONE_NAME_TRUNCATED) && std::strcmp(name, records_[i].name) == 0) {
                if (found != count_) return false;
                found = i;
            }
        }
        if (found == count_) return false;
        Copy(records_[found], out); return true;
    }
    bool List(uint64_t epoch, uint32_t offset, sc_game_zone* out, uint32_t stride,
              uint32_t cap, uint32_t* count, uint32_t* more, uint64_t* outEpoch) const {
        if (!count || !more || !outEpoch || (epoch && epoch != epoch_) || offset > count_ ||
            cap > SC_GAME_SPATIAL_MAX_PAGE || stride < sizeof(sc_game_zone) || stride > 4096 ||
            stride % alignof(sc_game_zone) || (cap && !out)) return false;
        // Validate every offered slot before ANY write (also the unused last slots).
        for (uint32_t i = 0; i < cap; ++i) {
            const auto* slot = reinterpret_cast<const sc_game_zone*>(reinterpret_cast<const char*>(out) + i * stride);
            if (slot->size < sizeof(*slot)) return false;
        }
        const uint32_t copied = std::min(cap, count_ - offset);
        for (uint32_t i = 0; i < copied; ++i)
            Copy(records_[offset + i], reinterpret_cast<sc_game_zone*>(reinterpret_cast<char*>(out) + i * stride));
        *count = copied; *more = count_ - offset - copied; *outEpoch = epoch_;
        return true;
    }
private:
    uint32_t Index(uint64_t id) const {
        for (uint32_t i = 0; i < count_; ++i) if (records_[i].zone_id == id) return i;
        return count_;
    }
    static void Copy(const sc_game_zone& record, sc_game_zone* out) {
        std::memcpy(reinterpret_cast<char*>(out) + sizeof(out->size),
                    reinterpret_cast<const char*>(&record) + sizeof(record.size), sizeof(record) - sizeof(record.size));
    }
    void Advance() { if (!++epoch_) ++epoch_; }
    uint64_t epoch_ = 1;
    uint32_t count_ = 0;
    std::array<sc_game_zone, SC_GAME_SPATIAL_MAX_ZONES> records_{};
};

} // namespace sco::game::services::spatial
