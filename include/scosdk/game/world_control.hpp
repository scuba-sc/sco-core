// GPL-3.0-only. Native C++ route; no mutation route in C#/sandboxed Lua.
#ifndef SCOSDK_GAME_WORLD_CONTROL_HPP
#define SCOSDK_GAME_WORLD_CONTROL_HPP
#include "../plugin.hpp"
#include <sc_world_control.h>
#include <cstring>
namespace sco::sdk::game {
class WorldControl {
public:
    sco_result Open(const sco_api* api, sco_plugin* self) noexcept {
        t_ = nullptr; self_ = self;
        if (!api || !self) return SCO_BAD_ARG;
        if (api->size < offsetof(sco_api, query_service) + sizeof(api->query_service) || !api->query_service)
            return SCO_UNAVAILABLE;
        const void* table = nullptr;
        const auto r = api->query_service(SC_WORLD_CONTROL_NAME, SC_WORLD_CONTROL_VERSION_1_0, &table);
        if (r != SCO_OK) return r;
        if (!table) return SCO_UNAVAILABLE;
        t_ = static_cast<const sc_world_control_v1*>(table); return SCO_OK;
    }
    sco_result Open(const Plugin& p) noexcept { return Open(p.Api(), p.Self()); }
    sco_result RaycastFiltered(uint64_t zone, const double (&from)[3], const double (&dir)[3],
        double distance, const uint64_t* skips, uint32_t count, sc_world_hit& hit) const noexcept {
        std::memset(&hit, 0, sizeof(hit)); hit.size = sizeof(hit);
        if (!t_ || t_->size < offsetof(sc_world_control_v1, raycast_filtered) + sizeof(t_->raycast_filtered) || !t_->raycast_filtered)
            return SCO_UNAVAILABLE;
        return t_->raycast_filtered(self_, zone, from, dir, distance, skips, count, &hit);
    }
    sco_result RaycastFilteredMany(uint64_t zone, const double (&from)[3], const double (&dir)[3],
        double distance, const uint64_t* skips, uint32_t count, sc_world_hits& hits) const noexcept {
        const auto capacity = hits.count;
        std::memset(&hits, 0, sizeof(hits)); hits.size = sizeof(hits);
        if (!t_ || t_->size < offsetof(sc_world_control_v1, raycast_filtered_many) + sizeof(t_->raycast_filtered_many) || !t_->raycast_filtered_many)
            return SCO_UNAVAILABLE;
        hits.count = capacity;
        return t_->raycast_filtered_many(self_, zone, from, dir, distance, skips, count, &hits);
    }
    sco_result CameraAcquire(uint64_t& lease) const noexcept {
        lease = 0;
        if (!t_ || t_->size < offsetof(sc_world_control_v1, camera_acquire) + sizeof(t_->camera_acquire) || !t_->camera_acquire)
            return SCO_UNAVAILABLE;
        return t_->camera_acquire(self_, &lease);
    }
    sco_result CameraStep(uint64_t lease, const sc_world_camera_step& step) const noexcept {
        if (!t_ || t_->size < offsetof(sc_world_control_v1, camera_step) + sizeof(t_->camera_step) || !t_->camera_step)
            return SCO_UNAVAILABLE;
        return t_->camera_step(self_, lease, &step);
    }
    sco_result CameraRelease(uint64_t lease) const noexcept {
        if (!t_ || t_->size < offsetof(sc_world_control_v1, camera_release) + sizeof(t_->camera_release) || !t_->camera_release)
            return SCO_UNAVAILABLE;
        return t_->camera_release(self_, lease);
    }
private:
    const sc_world_control_v1* t_ = nullptr;
    sco_plugin* self_ = nullptr;
};
}
#endif
