#include "sco/signatures.h"
#include "sco/signature_registry.h"
#include "sco/log.h"
#include "owned_registry_reservation.h"
#include "reserved_legacy_gate.h"
#include <memory>
#include <mutex>
#include <limits>

namespace sco {
namespace {
// Ownership transitions serialize separately from short-lived registry publication locks.
std::mutex g_hostOwnership;
size_t g_hostPins = 0;
bool g_hostOwnedReservation = false;
std::shared_ptr<OwnedShutdown> g_hostOwnedOwner;
std::mutex g_hostRegistryMutex;
std::shared_ptr<SignatureRegistry> g_hostRegistry;
thread_local unsigned g_hostCallbackDepth = 0;
struct HostCallbackScope {
    HostCallbackScope() { ++g_hostCallbackDepth; }
    ~HostCallbackScope() { --g_hostCallbackDepth; }
};

std::shared_ptr<SignatureRegistry> LoadHostRegistry() noexcept {
    std::lock_guard<std::mutex> lock(g_hostRegistryMutex);
    return g_hostRegistry;
}
void StoreHostRegistry(const std::shared_ptr<SignatureRegistry>& registry) noexcept {
    std::lock_guard<std::mutex> lock(g_hostRegistryMutex);
    g_hostRegistry = registry;
}
}

bool detail::ReserveEmptyHostRegistry(const std::shared_ptr<OwnedShutdown>& owner,
                                      const OwnedShutdown::Transition& transition) noexcept {
    LegacyEntry entry;
    if (!entry) return false;
    if (!owner || g_hostCallbackDepth || !transition.valid_for(*owner)) return false;
    // Never wait on a legacy registry callback which may synchronously re-enter
    // host startup. A busy registry is a conservative refusal.
    std::unique_lock<std::mutex> lock(g_hostOwnership, std::try_to_lock);
    if (!lock.owns_lock()) return false;
    if (!transition.valid_for(*owner) || g_hostOwnedReservation || g_hostPins || LoadHostRegistry()) return false;
    g_hostOwnedReservation = true;
    g_hostOwnedOwner = owner;
    return true;
}
bool detail::HostRegistryReservedFor(const std::shared_ptr<OwnedShutdown>& owner) noexcept {
    LegacyEntry entry(false);
    if (!entry) return false;
    if (g_hostCallbackDepth) return false;
    std::unique_lock<std::mutex> lock(g_hostOwnership, std::try_to_lock);
    return lock.owns_lock() && owner && g_hostOwnedReservation && g_hostOwnedOwner == owner;
}
void detail::ReleaseUnusedHostRegistryReservation(const std::shared_ptr<OwnedShutdown>& owner) noexcept {
    LegacyEntry entry;
    if (!entry) return;
    std::lock_guard<std::mutex> lock(g_hostOwnership);
    if (owner && g_hostOwnedReservation && g_hostOwnedOwner == owner && !g_hostPins && !LoadHostRegistry()) {
        g_hostOwnedReservation = false;
        g_hostOwnedOwner.reset();
    }
}

bool InstallHostSignatureRegistry(const std::shared_ptr<SignatureRegistry>& registry,
                                  void (*beforeInstall)() noexcept) noexcept {
    detail::LegacyEntry entry;
    if (!entry) return false;
    if (!registry) return false;
    std::lock_guard<std::mutex> lock(g_hostOwnership);
    if (g_hostOwnedReservation || g_hostPins || LoadHostRegistry() ||
        registry->SealForHost() != SigRegistryStatus::Ok) return false;
    if (beforeInstall) { HostCallbackScope scope; beforeInstall(); }
    StoreHostRegistry(registry);
    return true;
}
bool ClearHostSignatureRegistry(const std::shared_ptr<SignatureRegistry>& registry,
                                void (*beforeClear)() noexcept) noexcept {
    detail::LegacyEntry entry;
    if (!entry) return false;
    if (!registry) return false;
    std::lock_guard<std::mutex> lock(g_hostOwnership);
    if (g_hostOwnedReservation) return false;
    if (g_hostPins || LoadHostRegistry() != registry) return false;
    if (beforeClear) { HostCallbackScope scope; beforeClear(); }
    StoreHostRegistry({});
    return true;
}
bool UnpinAndClearHostSignatureRegistry(const std::shared_ptr<SignatureRegistry>& registry,
                                        void (*beforeClear)() noexcept) noexcept {
    detail::LegacyEntry entry;
    if (!entry) return false;
    if (!registry) return false;
    std::lock_guard<std::mutex> lock(g_hostOwnership);
    if (g_hostOwnedReservation) return false;
    if (g_hostPins != 1 || LoadHostRegistry() != registry) return false;
    if (beforeClear) { HostCallbackScope scope; beforeClear(); }
    g_hostPins = 0;
    StoreHostRegistry({});
    return true;
}
bool WithHostSignatureRegistry(const std::shared_ptr<const SignatureRegistry>& registry,
                               void (*callback)() noexcept) noexcept {
    detail::LegacyEntry entry;
    if (!entry) return false;
    if (!registry || !callback) return false;
    std::lock_guard<std::mutex> lock(g_hostOwnership);
    if (g_hostOwnedReservation) return false;
    if (LoadHostRegistry() != registry) return false;
    { HostCallbackScope scope; callback(); }
    return true;
}
bool WithNoHostSignatureRegistry(void (*callback)() noexcept) noexcept {
    detail::LegacyEntry entry;
    if (!entry) return false;
    if (!callback) return false;
    std::lock_guard<std::mutex> lock(g_hostOwnership);
    if (g_hostOwnedReservation) return false;
    if (LoadHostRegistry()) return false;
    { HostCallbackScope scope; callback(); }
    return true;
}
bool WithHostSignatureRegistry(const std::shared_ptr<const SignatureRegistry>& registry,
                               void (*callback)(void*) noexcept, void* context) noexcept {
    detail::LegacyEntry entry;
    if (!entry) return false;
    if (!registry || !callback) return false;
    std::lock_guard<std::mutex> lock(g_hostOwnership);
    if (g_hostOwnedReservation) return false;
    if (LoadHostRegistry() != registry) return false;
    { HostCallbackScope scope; callback(context); }
    return true;
}
bool WithNoHostSignatureRegistry(void (*callback)(void*) noexcept, void* context) noexcept {
    detail::LegacyEntry entry;
    if (!entry) return false;
    if (!callback) return false;
    std::lock_guard<std::mutex> lock(g_hostOwnership);
    if (g_hostOwnedReservation) return false;
    if (LoadHostRegistry()) return false;
    { HostCallbackScope scope; callback(context); }
    return true;
}
bool PinHostSignatureRegistry(const std::shared_ptr<const SignatureRegistry>& expected) noexcept {
    detail::LegacyEntry entry;
    if (!entry) return false;
    std::lock_guard<std::mutex> lock(g_hostOwnership);
    if (g_hostOwnedReservation) return false;
    if (LoadHostRegistry() != expected ||
        g_hostPins == std::numeric_limits<size_t>::max()) return false;
    ++g_hostPins;
    return true;
}
bool UnpinHostSignatureRegistry(const std::shared_ptr<const SignatureRegistry>& expected) noexcept {
    detail::LegacyEntry entry;
    if (!entry) return false;
    std::lock_guard<std::mutex> lock(g_hostOwnership);
    if (g_hostOwnedReservation) return false;
    if (!g_hostPins || LoadHostRegistry() != expected) return false;
    --g_hostPins;
    return true;
}
std::shared_ptr<const SignatureRegistry> AcquireHostSignatureRegistry() noexcept {
    detail::LegacyEntry entry;
    if (!entry) return {};
    return LoadHostRegistry();
}
std::shared_ptr<const SignatureSnapshot> AcquireHostSignatureSnapshot() noexcept {
    auto registry = AcquireHostSignatureRegistry();
    return registry ? registry->Snapshot() : nullptr;
}

const char* SigStateName(SigState s) {
    switch (s) {
    case SigState::NotRun:    return "NOT RUN";
    case SigState::Ok:        return "OK";
    case SigState::Missing:   return "MISSING";
    case SigState::Ambiguous: return "AMBIG";
    case SigState::Failed:    return "FAILED";
    case SigState::Blocked:   return "BLOCKED";
    }
    return "?";
}

SigResult SigPattern(const Section& text, const char* pattern) {
    int n = 0;
    uint8_t* at = FindUniquePattern(text, pattern, n);
    if (n == 0) return { SigState::Missing, nullptr, 0, nullptr };
    if (n > 1)  return { SigState::Ambiguous, nullptr, n, nullptr };
    return { SigState::Ok, at, 1, nullptr };
}

uint8_t* Sig(const char* id) {
    auto snapshot = AcquireHostSignatureSnapshot();
    const SigResult* r = snapshot ? snapshot->Lookup(id) : nullptr;
    return r && r->state == SigState::Ok ? r->at : nullptr;
}
bool SigReady(const char* id) { return Sig(id) != nullptr; }

void LogSignatureReport(const SignatureSnapshot& snapshot, bool verbose) {
    size_t ok = 0;
    for (size_t i = 0; i < snapshot.Count(); ++i) ok += snapshot.ResultAt(i)->state == SigState::Ok;
    Log("[core] signatures: %zu/%zu OK", ok, snapshot.Count());
    for (size_t i = 0; i < snapshot.Count(); ++i) {
        const SigResult& r = *snapshot.ResultAt(i);
        const char* id = snapshot.IdAt(i);
        switch (r.state) {
        case SigState::Ok:
            if (verbose) Log("[core] OK       %s", id);
            break;
        case SigState::Missing:   Log("[core] MISSING  %s (pattern: 0 matches)", id); break;
        case SigState::Ambiguous: Log("[core] AMBIG    %s (pattern: %d matches)", id, r.matches); break;
        case SigState::Blocked:   Log("[core] BLOCKED  %s (needs %s)", id, r.why); break;
        case SigState::Failed:    Log("[core] FAILED   %s (%s)", id, r.why ? r.why : "no reason"); break;
        case SigState::NotRun:    Log("[core] NOT RUN  %s", id); break;
        }
    }
}

} // namespace sco
