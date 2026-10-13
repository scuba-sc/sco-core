// SPDX-License-Identifier: GPL-3.0-only
#pragma once
#include <array>
#include <cstdint>
#include <memory>
#include <string>

// Host-private, project-authored collector contract; not a public SDK service.
namespace sco::diagnostics::ipc { struct AuditEvent; }
namespace sco::diagnostics::audit {
enum class Status { Verified, Accepted, Unavailable, IdentityDenied, ProtectionDenied,
                    Deadline, Full, Invalid, Busy };
struct Receipt {
    Status status = Status::Unavailable;
    uint64_t completedMs = 0; // same steady machine clock as the IPC deadline
    bool protectedSink = false;
    bool effectiveIdentity = false;
};
// Callers MUST recheck observedNow after return and before releasing any response.
bool Protected(const Receipt&, uint64_t deadlineMs, uint64_t observedNow);
bool Acknowledged(const Receipt&, uint64_t deadlineMs, uint64_t observedNow);
struct Coordinates {
    std::array<uint8_t, 16> session{};
    std::string build; // trusted sanitized build alias; never a path or user label
    uint32_t schema = 1;
    uint64_t aggregateCount = 1; // explicit denied-flood count, never zero
    std::string filter; // optional sanitized grammar ID, not raw input
};
class Collector {
public:
    virtual ~Collector() = default;
    virtual Receipt Verify(uint64_t deadlineMs) = 0;
    virtual Receipt Append(const ipc::AuditEvent&, const Coordinates&, uint64_t deadlineMs) = 0;
};
struct WindowsOptions {
    std::wstring log;    // administrator-created dedicated classic Event Log
    std::wstring source; // administrator-created source beneath that log
    uint32_t capacityBytes = 0; // exact provisioned MaxSize, required; >=64 KiB
};
// No creation, auto-provisioning, registry edits, sockets, or file fallback.
// Non-Windows returns a collector whose operations are Unavailable.
std::shared_ptr<Collector> OpenWindowsCollector(const WindowsOptions&);
} // namespace sco::diagnostics::audit
