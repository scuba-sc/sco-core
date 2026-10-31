// SPDX-License-Identifier: GPL-3.0-only
// SPDX-FileCopyrightText: 2026 the sco-core contributors
#include "admission.h"
#include "sco/runtime.h"
#include <atomic>
namespace sco::game::host {
namespace {
std::atomic<Admission (*)()> inspector{nullptr};
std::atomic<bool> enabled{false};
std::atomic<std::shared_ptr<OwnedShutdown>> shutdownOwner;
thread_local bool cvarCleanup = false;
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
    const auto owner = shutdownOwner.load();
    return enabled.load() && (!owner || owner->open()) && InspectAdmission() &&
           enabled.load() && (!owner || owner->open());
}
namespace detail {
void SetFreshInspector(Admission (*inspect)()) { inspector.store(inspect); }
void SetFreshShutdownOwner(std::shared_ptr<OwnedShutdown> owner) { shutdownOwner.store(std::move(owner)); }
void SetFreshAdmissionEnabled(bool on) { enabled.store(on); }
void SetCvarCleanupEnabled(bool on) { if (OnGameThread()) cvarCleanup = on; }
bool CvarCleanupAllowed() noexcept {
    return OnGameThread() && cvarCleanup && InspectAdmission() && cvarCleanup;
}
}
}
