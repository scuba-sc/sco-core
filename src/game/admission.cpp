// SPDX-License-Identifier: GPL-3.0-only
// SPDX-FileCopyrightText: 2026 the sco-core contributors
#include "admission.h"
#include "sco/runtime.h"
#include "../api/internal.h"
#include <atomic>
namespace sco::game::host {
namespace {
std::atomic<Admission (*)()> inspector{nullptr};
std::atomic<bool> enabled{false};
std::atomic<std::shared_ptr<OwnedShutdown>> shutdownOwner;
thread_local bool cvarCleanup = false;
#ifdef SCO_FRESH_ADMISSION_TESTING
void (*beforeOwnerLoad)() = nullptr;
#endif
bool InspectAdmission() noexcept {
    const auto inspect = inspector.load();
    if (!inspect) return false;
    try {
        const Admission a = inspect(); // host-only, fresh, thread-safe evidence
        return a.session == Session::Offline &&
               a.antiCheat == Check::Clear && a.officialNetwork == Check::Clear &&
               a.targetProcess == Check::Clear;
    } catch (...) { return false; }
}
}
bool FreshAdmissionAllowed() noexcept {
    try {
        if (!enabled.load()) return false;
        // Enroll in the runtime binding census before taking the inspector's owner
        // snapshot. A pre-bind call prevents a subsequent owned binding; an
        // already-bound call holds that owner's lease for the full callback.
        sco::detail::RuntimeLease runtime;
        if (!runtime || runtime.startup()) return false;
#ifdef SCO_FRESH_ADMISSION_TESTING
        if (beforeOwnerLoad) beforeOwnerLoad();
#endif
        const auto owner = shutdownOwner.load();
        auto read = owner ? owner->try_read() : OwnedShutdown::Read{};
        return enabled.load() && (!owner || read) && InspectAdmission() &&
               enabled.load() && (!owner || owner->open());
    } catch (...) { return false; }
}
namespace detail {
#ifdef SCO_FRESH_ADMISSION_TESTING
void SetBeforeOwnerLoadForTest(void (*hook)()) { beforeOwnerLoad = hook; }
#endif
void SetFreshInspector(Admission (*inspect)()) { inspector.store(inspect); }
void SetFreshShutdownOwner(std::shared_ptr<OwnedShutdown> owner) { shutdownOwner.store(std::move(owner)); }
void SetFreshAdmissionEnabled(bool on) { enabled.store(on); }
void SetCvarCleanupEnabled(bool on) { if (OnGameThread()) cvarCleanup = on; }
bool CvarCleanupAllowed() noexcept {
    return OnGameThread() && cvarCleanup && InspectAdmission() && cvarCleanup;
}
}
}
