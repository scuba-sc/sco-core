#pragma once
// Owned, instance-scoped signature definitions and immutable resolution snapshots.
#include "sco/signatures.h"
#include <cstddef>
#include <memory>

namespace sco {

struct SignatureTable { const SigDef* rows; size_t count; };
enum class SigRegistryStatus {
    Ok, InvalidArgument, DuplicateId, Frozen, NotFrozen,
    OutOfMemory, BudgetExceeded, TooLarge, ResolverException, Busy, NotResolved, HostSealed
};
// A resource error not tied to a particular input row uses this sentinel.
constexpr size_t kSignatureNoIndex = static_cast<size_t>(-1);
struct SigRegistrationResult { SigRegistryStatus status; size_t table; size_t row; };
const char* SigRegistryStatusName(SigRegistryStatus) noexcept;

// All returned pointers, including SigResult::why, are valid while this snapshot is held.
// The image behind at and the executable code of resolvers are borrowed from the host;
// keeping a snapshot does not keep either alive or make an address valid after unload.
class SignatureSnapshot {
    struct Token {};
public:
    struct Impl;
    explicit SignatureSnapshot(Token, std::shared_ptr<const Impl> impl) noexcept;
    size_t Count() const noexcept;
    const char* IdAt(size_t i) const noexcept;
    const SigResult* ResultAt(size_t i) const noexcept;
    const SigResult* Lookup(const char* id) const noexcept;
private:
    std::shared_ptr<const Impl> impl_;
    friend class SignatureRegistry;
};

// One writer owns registration, freeze and resolution; immutable snapshots can be
// acquired and read from any thread after safe publication. A registry must not be
// destroyed concurrently with a call to it. The constructor may throw bad_alloc.
// A zero budget means allocator-limited; otherwise dynamically allocated registry,
// graph, snapshot and resolution-work storage share the byte budget, including old
// snapshots held by readers. Fixed registry/resource control objects and external
// image, callback and allocator overhead are not included.
class SignatureRegistry {
public:
    explicit SignatureRegistry(size_t budgetBytes = 0);
    ~SignatureRegistry();
    SignatureRegistry(const SignatureRegistry&) = delete;
    SignatureRegistry& operator=(const SignatureRegistry&) = delete;
    // Tables commit together. Inputs and their NUL-terminated strings must be valid
    // for this call; callback code must remain loaded while it can be invoked.
    // A failed registration may retain container capacity charged to the budget.
    SigRegistrationResult RegisterTables(const SignatureTable* tables, size_t count) noexcept;
    // Prelinks dependencies. A snapshot is published only by successful Resolve.
    SigRegistryStatus Freeze() noexcept;
    SigRegistryStatus Resolve(const Image& image) noexcept;
    // Before host installation, seal a resolved registry against further resolution.
    // Requires the single writer; independent unsealed registries can re-resolve.
    SigRegistryStatus SealForHost() noexcept;
    std::shared_ptr<const SignatureSnapshot> Snapshot() const noexcept;
    size_t UsedBytes() const noexcept;
    size_t BudgetBytes() const noexcept;
private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};

// Host composition owns one registry; callbacks use their explicit context instead.
std::shared_ptr<const SignatureRegistry> AcquireHostSignatureRegistry() noexcept;
// Callbacks run while host ownership is locked and must not re-enter host registry
// composition. Null is for hosts with no dependent capability transition.
bool InstallHostSignatureRegistry(const std::shared_ptr<SignatureRegistry>& registry,
                                  void (*beforeInstall)() noexcept) noexcept;
bool ClearHostSignatureRegistry(const std::shared_ptr<SignatureRegistry>& expected,
                                void (*beforeClear)() noexcept) noexcept;
bool WithHostSignatureRegistry(const std::shared_ptr<const SignatureRegistry>& expected,
                               void (*callback)() noexcept) noexcept;
bool WithHostSignatureRegistry(const std::shared_ptr<const SignatureRegistry>& expected,
                               void (*callback)(void*) noexcept, void* context) noexcept;
bool WithNoHostSignatureRegistry(void (*callback)() noexcept) noexcept;
bool WithNoHostSignatureRegistry(void (*callback)(void*) noexcept, void* context) noexcept;
// Ownership transitions and pins are serialized. A null expected value pins absence.
// Each successful pin must be balanced by its owner after all uses are finished;
// install and clear refuse while any pins exist.
bool PinHostSignatureRegistry(const std::shared_ptr<const SignatureRegistry>& expected) noexcept;
bool UnpinHostSignatureRegistry(const std::shared_ptr<const SignatureRegistry>& expected) noexcept;
// Clears only when expected owns the registry and exactly one pin remains. On
// failure both ownership and the pin are retained. beforeClear must not re-enter.
bool UnpinAndClearHostSignatureRegistry(const std::shared_ptr<SignatureRegistry>& expected,
                                        void (*beforeClear)() noexcept) noexcept;
std::shared_ptr<const SignatureSnapshot> AcquireHostSignatureSnapshot() noexcept;

} // namespace sco
