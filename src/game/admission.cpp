// SPDX-License-Identifier: GPL-3.0-only
// SPDX-FileCopyrightText: 2026 the sco-core contributors
#include "admission.h"
#include <atomic>
namespace sco::game::host {
namespace {
std::atomic<Admission (*)()> inspector{nullptr};
std::atomic<bool> enabled{false};
}
bool FreshAdmissionAllowed() noexcept {
    if (!enabled.load()) return false;
    const auto inspect = inspector.load();
    if (!inspect) return false;
    try {
        const Admission a = inspect(); // host-only, fresh, thread-safe evidence
        return enabled.load() && a.session == Session::Offline &&
               a.antiCheat == Check::Clear && a.officialNetwork == Check::Clear &&
               a.targetProcess == Check::Clear;
    } catch (...) { return false; }
}
namespace detail {
void SetFreshInspector(Admission (*inspect)()) { inspector.store(inspect); }
void SetFreshAdmissionEnabled(bool on) { enabled.store(on); }
}
}
