// SPDX-License-Identifier: GPL-3.0-only
#pragma once
#include "sco/host.h"
#include "sco/caps.h"
#include "sco/owned_shutdown.h"
#include "sco/runtime.h"
#include <memory>

namespace sco::detail {
// Private host-source token. The host passes the actual transition explicitly;
// a synchronous external callback does not inherit this capability.
class StartupPublish {
public:
    StartupPublish(const std::shared_ptr<OwnedShutdown>& owner, const OwnedShutdown::Transition& transition)
        : owner_(owner), transition_(&transition) {}
    StartupPublish(const StartupPublish&) = delete;
    StartupPublish& operator=(const StartupPublish&) = delete;
    bool valid() const;
    const std::shared_ptr<OwnedShutdown>& owner() const { return owner_; }
private:
    std::shared_ptr<OwnedShutdown> owner_;
    const OwnedShutdown::Transition* transition_;
};
Result ProvideServiceOwned(const StartupPublish& cap, const void* owner, const char* prefix,
                           const char* name, uint32_t version, const void* table);
Result AddReleaseHookOwned(const StartupPublish* cap, ReleaseHook hook);
Result RemoveReleaseHookOwned(const StartupPublish* cap, ReleaseHook hook);
void SetGameThreadOwned(const StartupPublish* cap);
void SetCalloutGuardOwned(const StartupPublish* cap, CalloutGuard guard);
void SetCapabilityCheckOwned(const StartupPublish* cap, CapabilityCheck check);
inline Result PublishHostOwned(const StartupPublish* cap, const char* name, uint32_t version, const void* table) {
    if (!cap) return host::ProvideHostService(name, version, table);
    return ProvideServiceOwned(*cap, host::HostOwner(), host::kHostId, name, version, table);
}

inline Result PublishGameOwned(const StartupPublish* cap, const char* name, uint32_t version, const void* table) {
    if (!cap) return host::ProvideGameService(name, version, table);
    return ProvideServiceOwned(*cap, host::GameOwner(), "game", name, version, table);
}
}

namespace sco::host::owned {
const sco_api* BuildApi(const HostInfo& info, const sco::detail::StartupPublish& cap);
}

namespace sco::caps::owned {
Result Set(const sco::detail::StartupPublish& cap, const char* name, bool ready, const char* reason = nullptr);
void InvalidateForOwner(const std::shared_ptr<OwnedShutdown>& owner, const char* reason) noexcept;
}
