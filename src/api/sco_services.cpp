// Services: function tables published by one owner and found by name (sco/runtime.h).
#include "sco/runtime.h"
#include "sco/diagnostics_services.h"
#include "sco/owned_shutdown.h"
#include "../owned_shutdown_access.h"
#include "internal.h"
#include "owned_publication.h"
#include <algorithm>
#include <atomic>
#include <cstring>
#include <mutex>
#include <string>
#include <type_traits>
#include <utility>
#include <vector>

namespace sco {

namespace {

struct Service {
    const void* owner;
    std::string name;
    uint32_t    version;
    const void* table;
};

#ifdef SCO_RUNTIME_DIAGNOSTICS_FIXTURE
std::atomic<uint64_t> g_fixtureQueryCalls{0}; // no counter/export in non-test builds
#endif
std::mutex           g_lock;
std::atomic<bool>    g_stopped{ false };
std::atomic<std::shared_ptr<OwnedShutdown>> g_shutdownOwner;
std::mutex g_bindingLock;
size_t g_runtimeEntries = 0;
bool g_legacyRuntimeTouched = false;
std::vector<Service> g_services;
// Owners whose table QueryService has returned at least once. Kept after the services are
// withdrawn: the loader asks once the owner is released, to keep its module mapped. One pointer
// per provider (owners are never reused), so it stays small.
std::vector<const void*> g_handedOut;
// Admission reservations are separate from publication: explicit withdrawal/republication must
// not reopen a provider while its unload export is destroying state.
std::vector<const void*> g_withdrawing;

// Diagnostic sidecars are independent of publication storage. Their allocation failures
// must never turn a successful provide/withdraw/release into a registry failure.
struct DiagnosticDetails {
    const void* owner;
    std::string name;
    std::optional<std::string> ownerId;
    std::string category;
    std::optional<uint32_t> tableSize;
};
struct WithdrawalRecord {
    const void* owner;  // opaque internal key only; never copied out or dereferenced
    diagnostics::ServiceMetadata metadata;
};
// Erase/replacement maintenance must stay allocation-free and non-throwing, so observation
// bookkeeping cannot add a failure path to provide/release.
static_assert(std::is_nothrow_move_assignable<DiagnosticDetails>::value);
static_assert(std::is_nothrow_move_assignable<WithdrawalRecord>::value);
std::vector<DiagnosticDetails> g_diagnosticDetails;
std::vector<WithdrawalRecord> g_withdrawalHistory;
bool g_diagnosticHistoryLost = false;
constexpr size_t kMaxDiagnosticServices = 1024;

bool Withdrawing(const void* owner) {
    return std::find(g_withdrawing.begin(), g_withdrawing.end(), owner) != g_withdrawing.end();
}

bool ValidServiceName(const char* name) {
    if (!name) return false;
    const size_t n = strnlen(name, kMaxServiceNameLen + 1);
    if (n == 0 || n > kMaxServiceNameLen || name[0] == '.' || name[n - 1] == '.') return false;
    for (size_t i = 0; i < n; ++i) {
        const char c = name[i];
        if (c == '.') { if (name[i + 1] == '.') return false; continue; }
        if (!((c >= 'a' && c <= 'z') || (c >= '0' && c <= '9') || c == '_')) return false;
    }
    return true;
}

bool Exposed(const void* owner) {
    return std::find(g_handedOut.begin(), g_handedOut.end(), owner) != g_handedOut.end();
}

// Requires g_lock. No service table is examined here (not even a size prefix).
diagnostics::ServiceMetadata Describe(const Service& s) {
    diagnostics::ServiceMetadata item{ s.name, s.version, std::nullopt, "unknown", "published",
                                      Exposed(s.owner), std::nullopt, std::nullopt };
    for (const auto& details : g_diagnosticDetails) {
        if (details.owner != s.owner || details.name != s.name) continue;
        item.ownerId = details.ownerId;
        item.ownerCategory = details.category;
        item.tableSize = details.tableSize;
        break;
    }
    if (detail::RuntimeStopping()) {
        item.status = "admission_blocked";
        item.reason = "runtime_stopping";
    } else if (Withdrawing(s.owner)) {
        item.status = "admission_blocked";
        item.reason = "owner_withdrawing";
    }
    return item;
}

void ForgetDiagnosticDetails(const Service& s) noexcept {
    g_diagnosticDetails.erase(std::remove_if(g_diagnosticDetails.begin(), g_diagnosticDetails.end(),
        [&](const DiagnosticDetails& details) { return details.owner == s.owner && details.name == s.name; }),
        g_diagnosticDetails.end());
}

void ForgetOldWithdrawal(const char* name) noexcept {
    g_withdrawalHistory.erase(std::remove_if(g_withdrawalHistory.begin(), g_withdrawalHistory.end(),
        [&](const WithdrawalRecord& record) { return record.metadata.service == name; }),
        g_withdrawalHistory.end());
}

void RecordWithdrawal(const Service& s) noexcept {
    try {
        auto item = Describe(s);
        item.status = "withdrawn";
        item.reason = "service_withdrawn";
        if (g_withdrawalHistory.size() == kMaxDiagnosticServices) {
            g_withdrawalHistory.erase(g_withdrawalHistory.begin());
            g_diagnosticHistoryLost = true;
        }
        g_withdrawalHistory.push_back({ s.owner, std::move(item) });
    } catch (...) {
        g_diagnosticHistoryLost = true;
    }
    ForgetDiagnosticDetails(s);
}

bool ValidOwnerId(const char* id) {
    if (!id) return true;
    const size_t n = strnlen(id, 32);
    if (n == 0 || n > 31) return false;
    for (size_t i = 0; i < n; ++i)
        if (!((id[i] >= 'a' && id[i] <= 'z') || (id[i] >= '0' && id[i] <= '9') || id[i] == '_'))
            return false;
    return true;
}

bool ValidCategory(const char* category) {
    if (!category || strnlen(category, 10) > 9) return false;
    return strcmp(category, "host") == 0 || strcmp(category, "game_pack") == 0 ||
           strcmp(category, "plugin") == 0 || strcmp(category, "unknown") == 0;
}

}  // namespace

detail::RuntimeLease::Entry::Entry() {
    std::lock_guard<std::mutex> lock(g_bindingLock);
    ++g_runtimeEntries;
    if (!g_shutdownOwner.load()) g_legacyRuntimeTouched = true;
}
detail::RuntimeLease::Entry::~Entry() {
    std::lock_guard<std::mutex> lock(g_bindingLock);
    --g_runtimeEntries;
}
bool detail::RuntimeStopping() {
    const auto owner = g_shutdownOwner.load();
    return g_stopped.load() || (owner && !owner->open());
}
detail::RuntimeLease::RuntimeLease()
    : owner_(g_shutdownOwner.load()),
      startup_(owner_ && OwnedShutdownHostAccess::on_transition_thread(*owner_)),
      read_(owner_ && !startup_ ? owner_->try_read() : OwnedShutdown::Read{}) {}
detail::RuntimeLease::~RuntimeLease() = default;
bool detail::RuntimeBoundTo(const std::shared_ptr<OwnedShutdown>& owner) {
    return owner && g_shutdownOwner.load() == owner;
}
bool detail::RuntimeCanStartLegacy() { return !g_shutdownOwner.load(); }
bool detail::BindRuntimeStartup(const std::shared_ptr<OwnedShutdown>& owner,
                                const OwnedShutdown::Transition& transition) {
    std::lock_guard<std::mutex> lock(g_bindingLock);
    if (!owner || !transition.valid_for(*owner) || g_shutdownOwner.load() ||
        g_legacyRuntimeTouched || g_runtimeEntries) return false;
    g_shutdownOwner.store(owner);
    return true;
}
bool detail::StartupPublish::valid() const {
    return owner_ && transition_ && RuntimeBoundTo(owner_) && transition_->valid_for(*owner_);
}
void detail::SetRuntimeStopping(bool stopped) {
    // Serializes the terminal boundary with the first successful hand-out.
    std::lock_guard<std::mutex> hold(g_lock);
    g_stopped.store(stopped);
}

static Result ProvideServiceImpl(const detail::StartupPublish* cap, const void* owner, const char* prefix,
                                 const char* name, uint32_t version, const void* table) {
    if (!owner || !table || !ValidServiceName(name)) return Result::BadArg;
    detail::RuntimeLease admission;
    if (cap ? !cap->valid() : (!admission || admission.startup())) return Result::Unavailable;
    if (prefix) {
        const size_t p = strlen(prefix);
        if (p == 0 || strncmp(name, prefix, p) != 0 || (name[p] != '\0' && name[p] != '.')) return Result::BadArg;
    }
    std::lock_guard<std::mutex> hold(g_lock);
    if (cap ? !cap->valid() : detail::RuntimeStopping()) return Result::Unavailable;
    if (detail::Released(owner)) return Result::BadArg;
    if (Withdrawing(owner)) return Result::Unavailable;
    for (const Service& s : g_services)
        if (s.name == name) return Result::BadArg;
    try {
        g_services.push_back({ owner, name, version, table });
    } catch (...) {
        return Result::TooMany;
    }
    ForgetOldWithdrawal(name);
    return Result::Ok;
}
Result ProvideService(const void* owner, const char* prefix, const char* name, uint32_t version,
                      const void* table) {
    return ProvideServiceImpl(nullptr, owner, prefix, name, version, table);
}
Result detail::ProvideServiceOwned(const StartupPublish& cap, const void* owner, const char* prefix,
                                   const char* name, uint32_t version, const void* table) {
    return ProvideServiceImpl(&cap, owner, prefix, name, version, table);
}

Result QueryService(const char* name, uint32_t minVersion, const void** out) {
#ifdef SCO_RUNTIME_DIAGNOSTICS_FIXTURE
    g_fixtureQueryCalls.fetch_add(1);
#endif
    if (out) *out = nullptr;
    if (!name || !out) return Result::BadArg;
    detail::RuntimeLease admission;
    if (!admission || admission.startup()) return Result::Unavailable;
    std::lock_guard<std::mutex> hold(g_lock);
    for (const Service& s : g_services) {
        if (s.name != name) continue;
        if (detail::RuntimeStopping() || Withdrawing(s.owner)) return Result::Unavailable;
        if ((s.version >> 16) != (minVersion >> 16) || s.version < minVersion) return Result::Unavailable;
        // Record the hand-out first: a table that is returned must be one the loader knows about.
        if (std::find(g_handedOut.begin(), g_handedOut.end(), s.owner) == g_handedOut.end()) {
            try {
                g_handedOut.push_back(s.owner);
            } catch (...) {
                return Result::TooMany;   // nothing handed out
            }
        }
        *out = s.table;
        return Result::Ok;
    }
    return Result::NotFound;
}

bool ServiceExists(const char* name) {
    if (!name) return false;
    detail::RuntimeLease admission;
    if (!admission || admission.startup() || (admission.owned() && detail::RuntimeStopping())) return false;
    std::lock_guard<std::mutex> hold(g_lock);
    for (const Service& s : g_services)
        if (s.name == name) return true;
    return false;
}

bool ServiceTableHandedOut(const void* owner) {
    detail::RuntimeLease admission;
    if (!admission || admission.startup() || (admission.owned() && detail::RuntimeStopping())) return false;
    std::lock_guard<std::mutex> hold(g_lock);
    return std::find(g_handedOut.begin(), g_handedOut.end(), owner) != g_handedOut.end();
}

Result ServiceWithdrawal(const void* const* owners, size_t count, bool withdraw,
                         bool allowInUse, bool& inUse) {
    inUse = false;
    detail::RuntimeLease admission;
    if (!admission || admission.owned()) return Result::Unavailable;
    std::lock_guard<std::mutex> hold(g_lock);
    // Off-thread quarantine marks the owner before its game-thread state verdict. Reject
    // administrative teardown under the admission lock even while Plugin::state is Loaded.
    if (!allowInUse)
        for (size_t i = 0; i < count; ++i)
            if (owners[i] && detail::Released(owners[i])) return Result::Crashed;
    for (size_t i = 0; i < count; ++i)
        if (std::find(g_handedOut.begin(), g_handedOut.end(), owners[i]) != g_handedOut.end())
            inUse = true;
    if (inUse && !allowInUse) return Result::Failed;
    if (!withdraw) return Result::Ok;
    // Allocate before changing any admission. Idempotent teardown of an already-reserved
    // member needs no new reservation storage (reload groups reserve once, then unload each).
    size_t missing = 0;
    for (size_t i = 0; i < count; ++i) if (owners[i] && !Withdrawing(owners[i])) ++missing;
    try { g_withdrawing.reserve(g_withdrawing.size() + missing); }
    catch (...) { return Result::TooMany; }
    for (size_t i = 0; i < count; ++i)
        if (owners[i] && !Withdrawing(owners[i])) g_withdrawing.push_back(owners[i]);
    return Result::Ok;
}

void CancelServiceWithdrawal(const void* const* owners, size_t count) {
    detail::RuntimeLease admission;
    if (!admission || admission.owned()) return;
    std::lock_guard<std::mutex> hold(g_lock);
    for (size_t i = 0; i < count; ++i) {
        if (detail::Released(owners[i])) continue;   // quarantine reservations stay terminal
        g_withdrawing.erase(std::remove(g_withdrawing.begin(), g_withdrawing.end(), owners[i]),
                            g_withdrawing.end());
    }
}

Result ReleaseService(const void* owner, const char* name) {
    if (!owner || !name) return Result::BadArg;
    detail::RuntimeLease admission;
    if (!admission || admission.owned()) return Result::Unavailable;
    std::lock_guard<std::mutex> hold(g_lock);
    for (size_t i = 0; i < g_services.size(); ++i)
        if (g_services[i].owner == owner && g_services[i].name == name) {
            RecordWithdrawal(g_services[i]);
            g_services.erase(g_services.begin() + static_cast<std::ptrdiff_t>(i));
            return Result::Ok;
        }
    return Result::NotFound;
}

long detail::ReleaseServices(const void* owner) {
    std::lock_guard<std::mutex> hold(g_lock);
    long n = 0;
    for (size_t i = g_services.size(); i-- > 0;)
        if (g_services[i].owner == owner) {
            RecordWithdrawal(g_services[i]);
            g_services.erase(g_services.begin() + static_cast<std::ptrdiff_t>(i));
            ++n;
        }
    return n;
}

namespace diagnostics {
#ifdef SCO_RUNTIME_DIAGNOSTICS_FIXTURE
uint64_t FixtureQueryServiceCalls() { return g_fixtureQueryCalls.load(); }
#endif

ServiceInventory CopyServiceMetadata() {
    detail::RuntimeLease admission;
    if (!admission || admission.startup() || (admission.owned() && detail::RuntimeStopping())) return {};
    std::lock_guard<std::mutex> hold(g_lock);
    ServiceInventory result;
    const size_t liveCount = std::min(g_services.size(), kMaxDiagnosticServices);
    const size_t historyCount = std::min(g_withdrawalHistory.size(), kMaxDiagnosticServices - liveCount);
    result.truncated = g_diagnosticHistoryLost || liveCount < g_services.size() ||
                       historyCount < g_withdrawalHistory.size();
    try {
        result.items.reserve(liveCount + historyCount);
        for (size_t i = 0; i < liveCount; ++i) result.items.push_back(Describe(g_services[i]));
        // If active publications consume the snapshot cap, prefer the newest history.
        for (size_t i = 0; i < historyCount; ++i) {
            const auto& record = g_withdrawalHistory[g_withdrawalHistory.size() - 1 - i];
            auto item = record.metadata;
            item.exposed = Exposed(record.owner);
            result.items.push_back(std::move(item));
        }
    } catch (...) {
        result.truncated = true;
    }
    std::sort(result.items.begin(), result.items.end(),
        [](const ServiceMetadata& a, const ServiceMetadata& b) { return a.service < b.service; });
    return result;
}

Result SetServiceDiagnosticMetadata(const void* owner, const char* name, const char* safeOwnerId,
                                    const char* category, std::optional<uint32_t> tableSize) {
    if (!owner || !ValidServiceName(name) || !ValidOwnerId(safeOwnerId) || !ValidCategory(category) ||
        (tableSize && *tableSize > 65536)) return Result::BadArg;
    detail::RuntimeLease admission;
    if (!admission || admission.startup() || (admission.owned() && detail::RuntimeStopping())) return Result::Unavailable;
    std::lock_guard<std::mutex> hold(g_lock);
    if (std::none_of(g_services.begin(), g_services.end(),
        [&](const Service& s) { return s.owner == owner && s.name == name; })) return Result::NotFound;
    try {
        DiagnosticDetails details{ owner, name, std::nullopt, category, tableSize };
        if (safeOwnerId) details.ownerId = safeOwnerId;
        const auto found = std::find_if(g_diagnosticDetails.begin(), g_diagnosticDetails.end(),
            [&](const DiagnosticDetails& d) { return d.owner == owner && d.name == name; });
        if (found == g_diagnosticDetails.end()) g_diagnosticDetails.push_back(std::move(details));
        else *found = std::move(details);
    } catch (...) {
        return Result::TooMany;
    }
    return Result::Ok;
}

void ResetServiceDiagnosticHistory() {
    detail::RuntimeLease admission;
    if (!admission || admission.owned()) return;
    std::lock_guard<std::mutex> hold(g_lock);
    g_withdrawalHistory.clear();
    g_diagnosticHistoryLost = false;
}

}  // namespace diagnostics

}  // namespace sco
