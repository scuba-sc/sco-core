// SPDX-License-Identifier: GPL-3.0-only
#pragma once
// Host-source C++ policy, never a plugin service or frozen sco_api member.
#include <atomic>
#include <memory>
#include <optional>
#include <string>
#include <vector>
namespace sco::diagnostics::audit { class Collector; }
namespace sco::diagnostics {
enum class OfflineAdmission { Unknown, Online, Denied, Authorized };
struct InspectionOptions {
    bool enabled = false; // product runtime_inspection=off
    bool disclosureConsent = false;
    bool freeFormConsent = false;
    // Trusted product updates this directly; no MCP/API tool may change it.
    std::shared_ptr<std::atomic<OfflineAdmission>> admission;
    std::shared_ptr<audit::Collector> collector;
    std::string credentialPath;
    std::vector<std::string> excludedRoots; // checkout AND plugin roots; never peer input
    std::optional<std::string> hostBuild, sourceSha;
};
inline bool Authorized(const InspectionOptions& o) {
    return o.enabled && o.disclosureConsent && o.admission &&
        o.admission->load(std::memory_order_acquire) == OfflineAdmission::Authorized;
}
} // namespace sco::diagnostics
