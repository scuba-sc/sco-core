#pragma once
// Host-private lifetime primitives. They expose no provider pointer or callable thunk.
#include "sco/runtime.h"
#include <cstdint>

namespace sco::detail {
using ServiceGeneration = uint64_t;
// Atomically refuse a process-wide shutdown while any managed pin is live;
// success closes managed acquisition and publication until the next app start.
Result BeginManagedShutdown();
void ResetManagedShutdown();
#ifdef SCO_MANAGED_TESTING
bool InReleaseSubscriptionsForTest() noexcept;
#endif
// A generation identifies one publication for the lifetime of this process.
Result FindManagedService(const char* name, uint32_t minVersion, ServiceGeneration& generation);
class ManagedServicePin {
public:
    ManagedServicePin() noexcept = default;
    ~ManagedServicePin();
    ManagedServicePin(const ManagedServicePin&) = delete;
    ManagedServicePin& operator=(const ManagedServicePin&) = delete;
    Result acquire(ServiceGeneration generation);
    void reset() noexcept;
    explicit operator bool() const noexcept { return generation_ != 0; }
private:
    ServiceGeneration generation_ = 0;
};
// Fault retention is distinct from a permanent raw-table handout.
void DenyManagedOwner(const void* owner) noexcept;
bool ManagedServiceEverUsed(const void* owner);
void ForgetManagedOwner(const void* owner);
bool OwnerReleaseWasStarted(const void* owner);
Result OwnerReleaseStatus(const void* owner);
// Terminal group reservation closes admission atomically; raw handouts are not drained.
Result ReserveTerminalOwners(const void* const* owners, size_t count);
// Only the loader may release an owner while its terminal reservation is held.
Result ReleaseTerminalOwner(const void* owner);
Result ReserveOwnerRelease(const void* owner, bool& created, bool terminal = false);
void CancelOwnerRelease(const void* owner, bool created);
long QuarantineServices(const void* owner);
}
