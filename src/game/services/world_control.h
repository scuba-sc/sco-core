#pragma once
// GPL-3.0-only. Portable policy; game-pack only, never linked into the kernel.
#include <sc_world_control.h>
namespace sco::game::services {
// Host-only seam. All callbacks are synchronous, nonthrowing, bounded, and may
// not reenter. A production adapter must use verified offline admission and
// SEH around engine calls, pin ownership/streaming, and capture/restore ALL state.
// No engine implementation is installed yet. begin() must be failure-atomic;
// restore() keeps its captured state on failure for host recovery/restart.
struct WorldControlProvider {
    bool (*on_thread)() = nullptr;
    bool (*live)(const sco_plugin*) = nullptr;
    bool (*owned)(const sco_plugin*, uint64_t) = nullptr;
    sco_result (*ray)(uint64_t, const double*, const double*, double,
                      const uint64_t*, uint32_t, sc_world_hit*) = nullptr;
    sco_result (*begin)() = nullptr;
    sco_result (*step)(const sc_world_camera_step*) = nullptr;
    sco_result (*restore)() = nullptr;
    // Host-only scope, never exported. Successful acquire must produce a nonnull
    // scope; release is called exactly once even when query/validation fails.
    // Failed release retains the scope/backing context until process restart;
    // the controller and provider cannot be replaced or unloaded after failure.
    sco_result (*ray_acquire)(uint64_t, void**) = nullptr;
    sco_result (*ray_query)(void*, const double*, const double*, double,
                           const uint64_t*, uint32_t, sc_world_hits*) = nullptr;
    sco_result (*ray_release)(void*) = nullptr;
};
class WorldControl {
public:
    // Service lifetime: do not replace this object or adapter while leased or
    // restart-required. cleanup_ready proves an ordinary owner-release hook.
    WorldControl(WorldControlProvider provider, bool cleanup_ready)
        : p_(provider), cleanup_ready_(cleanup_ready) {}
    sco_result Ray(sco_plugin*, uint64_t, const double*, const double*, double,
                   const uint64_t*, uint32_t, sc_world_hit*);
    sco_result Acquire(sco_plugin*, uint64_t*);
    sco_result Rays(sco_plugin*, uint64_t, const double*, const double*, double,
                    const uint64_t*, uint32_t, sc_world_hits*);
    sco_result Step(sco_plugin*, uint64_t, const sc_world_camera_step*);
    sco_result Release(sco_plugin*, uint64_t);
    sco_result ReleaseOwner(const void*);
    sco_result Stop();
    sco_result Start(bool cleanup_ready);
private:
    sco_result Admission(sco_plugin*) const;
    sco_result CheckLease(sco_plugin*, uint64_t) const;
    sco_result Restore();
    WorldControlProvider p_;
    bool cleanup_ready_, stopped_ = false, failed_ = false, busy_ = false;
    const void* owner_ = nullptr;
    uint64_t lease_ = 0, next_ = 1;
    void* fault_scope_ = nullptr;
};
}
