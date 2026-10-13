// GPL-3.0-only. Push-only advisory source. No plugin callbacks or observation egress.
#include "sco/diagnostics.h"
#include "sco/host.h"
#include "../api/internal.h"
#include <chrono>
#ifdef _WIN32
#include <windows.h>
#endif
namespace sco::diagnostics {
namespace {
bool active = false;
std::string selected;
sco_plugin* owner = nullptr;
std::optional<DiagnosticsPluginMetadata> latest;
uint64_t Now() {
#ifdef _WIN32
    // App snapshots and IPC use this machine clock. MSVC steady_clock uses a
    // different QPC epoch and can make a just-submitted observation look future.
    return GetTickCount64();
#else
    return static_cast<uint64_t>(std::chrono::duration_cast<std::chrono::milliseconds>(
        std::chrono::steady_clock::now().time_since_epoch()).count());
#endif
}
bool ValidId(const std::string& id) {
    if (id.empty() || id.size() > 31 || id == "sco" || id == "host" || id == "menu" || id == "game") return false;
    for (const char c : id) if (!((c >= 'a' && c <= 'z') || (c >= '0' && c <= '9') || c == '_')) return false;
    return true;
}
sco_result Submit(sco_plugin* self, const sco_diagnostics_observation_v1* observation) {
    if (!OnGameThread()) return SCO_WRONG_THREAD;
    if (!active || detail::RuntimeStopping()) return SCO_UNAVAILABLE;
    const auto* id = host::PluginId(self);
    if (!id || detail::Released(self) || selected != id || !observation ||
        observation->size < sizeof(*observation) ||
        (observation->health != SCO_DIAGNOSTICS_HEALTH_UNKNOWN &&
         observation->health != SCO_DIAGNOSTICS_HEALTH_READY &&
         observation->health != SCO_DIAGNOSTICS_HEALTH_DEGRADED)) return SCO_BAD_ARG;
    const auto now = Now();
    if (now > kMaxMonotonicMs) return SCO_UNAVAILABLE;
    // New owner handles cannot inherit the prior load's record or rate budget.
    if (owner == self && latest && latest->observedMs &&
        (now < *latest->observedMs || now - *latest->observedMs < 1000)) return SCO_TOO_MANY;
    latest = DiagnosticsPluginMetadata{true, observation->health, now}; owner = self;
    return SCO_OK;
}
const sco_diagnostics_publisher_v1 table{sizeof(table), 0, Submit};
}
Result StartPublisher(const SourceOptions& options) {
    if (!OnGameThread()) return Result::WrongThread;
    if (active) return Result::BadArg;
    if (!options.diagnosticsPlugin || !options.offlineAuthorized || detail::RuntimeStopping()) return Result::Unavailable;
    if (!ValidId(options.diagnosticsPluginId)) return Result::BadArg;
    try { selected = options.diagnosticsPluginId; }
    catch (...) { return Result::TooMany; }
    const auto r = host::ProvideHostService(SCO_DIAGNOSTICS_PUBLISHER_NAME,
        SCO_DIAGNOSTICS_PUBLISHER_VERSION_1_0, &table);
    if (r != Result::Ok) { selected.clear(); return r; }
    active = true; latest.reset(); owner = nullptr;
    return Result::Ok;
}
void StopPublisher() {
    if (!OnGameThread()) return;
    active = false; selected.clear(); latest.reset(); owner = nullptr;
    host::WithdrawHostService(SCO_DIAGNOSTICS_PUBLISHER_NAME);
}
std::optional<DiagnosticsPluginMetadata> CopyPublisherMetadata(uint64_t now) {
    if (!OnGameThread() || !active || detail::RuntimeStopping() || now > kMaxMonotonicMs ||
        !owner || detail::Released(owner) || !latest || !latest->observedMs || *latest->observedMs > now) return {};
    return latest;
}
const sco_diagnostics_publisher_v1* PublisherTable() { return &table; }
}
