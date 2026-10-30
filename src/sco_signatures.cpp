#include "sco/signatures.h"
#include "sco/signature_registry.h"
#include "sco/log.h"
#include <memory>
#include <mutex>
#include <limits>

namespace sco {
namespace {
// Ownership transitions serialize separately from short-lived registry publication locks.
std::mutex g_hostOwnership;
size_t g_hostPins = 0;
std::mutex g_hostRegistryMutex;
std::shared_ptr<SignatureRegistry> g_hostRegistry;

std::shared_ptr<SignatureRegistry> LoadHostRegistry() noexcept {
    std::lock_guard<std::mutex> lock(g_hostRegistryMutex);
    return g_hostRegistry;
}
void StoreHostRegistry(const std::shared_ptr<SignatureRegistry>& registry) noexcept {
    std::lock_guard<std::mutex> lock(g_hostRegistryMutex);
    g_hostRegistry = registry;
}
}

bool InstallHostSignatureRegistry(const std::shared_ptr<SignatureRegistry>& registry,
                                  void (*beforeInstall)() noexcept) noexcept {
    if (!registry) return false;
    std::lock_guard<std::mutex> lock(g_hostOwnership);
    if (g_hostPins || LoadHostRegistry() ||
        registry->SealForHost() != SigRegistryStatus::Ok) return false;
    if (beforeInstall) beforeInstall();
    StoreHostRegistry(registry);
    return true;
}
bool ClearHostSignatureRegistry(const std::shared_ptr<SignatureRegistry>& registry,
                                void (*beforeClear)() noexcept) noexcept {
    if (!registry) return false;
    std::lock_guard<std::mutex> lock(g_hostOwnership);
    if (g_hostPins || LoadHostRegistry() != registry) return false;
    if (beforeClear) beforeClear();
    StoreHostRegistry({});
    return true;
}
bool UnpinAndClearHostSignatureRegistry(const std::shared_ptr<SignatureRegistry>& registry,
                                        void (*beforeClear)() noexcept) noexcept {
    if (!registry) return false;
    std::lock_guard<std::mutex> lock(g_hostOwnership);
    if (g_hostPins != 1 || LoadHostRegistry() != registry) return false;
    if (beforeClear) beforeClear();
    g_hostPins = 0;
    StoreHostRegistry({});
    return true;
}
bool WithHostSignatureRegistry(const std::shared_ptr<const SignatureRegistry>& registry,
                               void (*callback)() noexcept) noexcept {
    if (!registry || !callback) return false;
    std::lock_guard<std::mutex> lock(g_hostOwnership);
    if (LoadHostRegistry() != registry) return false;
    callback();
    return true;
}
bool WithNoHostSignatureRegistry(void (*callback)() noexcept) noexcept {
    if (!callback) return false;
    std::lock_guard<std::mutex> lock(g_hostOwnership);
    if (LoadHostRegistry()) return false;
    callback();
    return true;
}
bool WithHostSignatureRegistry(const std::shared_ptr<const SignatureRegistry>& registry,
                               void (*callback)(void*) noexcept, void* context) noexcept {
    if (!registry || !callback) return false;
    std::lock_guard<std::mutex> lock(g_hostOwnership);
    if (LoadHostRegistry() != registry) return false;
    callback(context);
    return true;
}
bool WithNoHostSignatureRegistry(void (*callback)(void*) noexcept, void* context) noexcept {
    if (!callback) return false;
    std::lock_guard<std::mutex> lock(g_hostOwnership);
    if (LoadHostRegistry()) return false;
    callback(context);
    return true;
}
bool PinHostSignatureRegistry(const std::shared_ptr<const SignatureRegistry>& expected) noexcept {
    std::lock_guard<std::mutex> lock(g_hostOwnership);
    if (LoadHostRegistry() != expected ||
        g_hostPins == std::numeric_limits<size_t>::max()) return false;
    ++g_hostPins;
    return true;
}
bool UnpinHostSignatureRegistry(const std::shared_ptr<const SignatureRegistry>& expected) noexcept {
    std::lock_guard<std::mutex> lock(g_hostOwnership);
    if (!g_hostPins || LoadHostRegistry() != expected) return false;
    --g_hostPins;
    return true;
}
std::shared_ptr<const SignatureRegistry> AcquireHostSignatureRegistry() noexcept {
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
