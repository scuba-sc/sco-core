// SPDX-License-Identifier: GPL-3.0-only
#include "diagnostics_audit.h"
#ifdef _WIN32
#define WIN32_LEAN_AND_MEAN
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#include <aclapi.h>
#include <sddl.h>
#include <winevt.h>
#include "diagnostics_ipc.h"
#include <atomic>
#include <chrono>
#include <condition_variable>
#include <cstring>
#include <mutex>
#include <thread>
#include <vector>
#endif
namespace sco::diagnostics::audit {
bool Protected(const Receipt& r, uint64_t deadline, uint64_t observed) {
    return (r.status == Status::Verified || r.status == Status::Accepted) &&
        r.protectedSink && r.effectiveIdentity && r.completedMs &&
        r.completedMs <= observed && observed < deadline;
}
bool Acknowledged(const Receipt& r, uint64_t deadline, uint64_t observed) {
    return r.status == Status::Accepted && Protected(r, deadline, observed);
}
#ifndef _WIN32
namespace {
class Unavailable final : public Collector {
    Receipt Verify(uint64_t) override { return {}; }
    Receipt Append(const ipc::AuditEvent&, const Coordinates&, uint64_t) override { return {}; }
};
}
std::shared_ptr<Collector> OpenWindowsCollector(const WindowsOptions&) {
    return std::make_shared<Unavailable>();
}
#else
namespace {
struct Handle {
    HANDLE h = nullptr;
    explicit Handle(HANDLE value = nullptr) : h(value == INVALID_HANDLE_VALUE ? nullptr : value) {}
    ~Handle() { if (h) CloseHandle(h); }
    Handle(const Handle&) = delete;
    Handle& operator=(const Handle&) = delete;
};
struct RegKey {
    HKEY h = nullptr;
    ~RegKey() { if (h) RegCloseKey(h); }
};
struct Local {
    void* p = nullptr;
    ~Local() { if (p) LocalFree(p); }
};
struct EventHandle {
    EVT_HANDLE h = nullptr;
    ~EventHandle() { if (h) EvtClose(h); }
};
struct Source {
    HANDLE h = nullptr;
    ~Source() { if (h) DeregisterEventSource(h); }
};
uint64_t Now() { return GetTickCount64(); }
Receipt Failure(Status status) { return {status, Now(), false, false}; }
bool Name(const std::wstring& name) {
    if (name.size() < 4 || name.size() > 64 || name.rfind(L"SCO", 0) != 0) return false;
    for (wchar_t c : name)
        if (!((c >= L'a' && c <= L'z') || (c >= L'A' && c <= L'Z') ||
              (c >= L'0' && c <= L'9') || c == L'_')) return false;
    return true;
}
bool TokenData(HANDLE token, TOKEN_INFORMATION_CLASS type, std::vector<BYTE>& data) {
    DWORD bytes = 0;
    GetTokenInformation(token, type, nullptr, 0, &bytes);
    if (!bytes || bytes > 65536) return false;
    data.resize(bytes);
    return GetTokenInformation(token, type, data.data(), bytes, &bytes) != FALSE;
}
bool Identity(Handle& impersonation) {
    // The RPC worker uses the process identity. An impersonating caller cannot
    // pass a check under one token and append using another token.
    HANDLE raw = nullptr;
    if (OpenThreadToken(GetCurrentThread(), TOKEN_QUERY, TRUE, &raw)) {
        CloseHandle(raw); return false;
    }
    if (GetLastError() != ERROR_NO_TOKEN ||
        !OpenProcessToken(GetCurrentProcess(), TOKEN_QUERY | TOKEN_DUPLICATE, &raw)) return false;
    Handle process(raw);
    TOKEN_ELEVATION elevation{}; TOKEN_ELEVATION_TYPE type{}; DWORD bytes = 0;
    if (!GetTokenInformation(raw, TokenElevation, &elevation, sizeof(elevation), &bytes) ||
        elevation.TokenIsElevated ||
        !GetTokenInformation(raw, TokenElevationType, &type, sizeof(type), &bytes) ||
        type != TokenElevationTypeDefault) return false;
    std::vector<BYTE> groups, privileges;
    if (!TokenData(raw, TokenGroups, groups) || !TokenData(raw, TokenPrivileges, privileges)) return false;
    const auto* g = reinterpret_cast<const TOKEN_GROUPS*>(groups.data());
    for (DWORD i = 0; i < g->GroupCount; ++i)
        if (IsWellKnownSid(g->Groups[i].Sid, WinBuiltinAdministratorsSid) ||
            IsWellKnownSid(g->Groups[i].Sid, WinLocalSystemSid)) return false; // includes deny-only admin groups
    const wchar_t* dangerous[] = {L"SeBackupPrivilege", L"SeRestorePrivilege", L"SeTakeOwnershipPrivilege",
        L"SeSecurityPrivilege", L"SeDebugPrivilege", L"SeTcbPrivilege", L"SeImpersonatePrivilege", L"SeLoadDriverPrivilege",
        L"SeAssignPrimaryTokenPrivilege", L"SeCreateTokenPrivilege"};
    const auto* p = reinterpret_cast<const TOKEN_PRIVILEGES*>(privileges.data());
    for (const auto* name : dangerous) {
        LUID id{};
        if (!LookupPrivilegeValueW(nullptr, name, &id)) return false;
        for (DWORD i = 0; i < p->PrivilegeCount; ++i)
            if (p->Privileges[i].Luid.LowPart == id.LowPart &&
                p->Privileges[i].Luid.HighPart == id.HighPart) return false; // even disabled but enableable
    }
    return DuplicateToken(raw, SecurityImpersonation, &impersonation.h) != FALSE;
}
bool Descriptor(PSECURITY_DESCRIPTOR sd, HANDLE token, GENERIC_MAPPING mapping,
                DWORD forbidden, DWORD required = 0) {
    if (!sd || !IsValidSecurityDescriptor(sd)) return false;
    PSID owner = nullptr; PACL dacl = nullptr; BOOL present = FALSE, defaulted = FALSE;
    if (!GetSecurityDescriptorOwner(sd, &owner, &defaulted) || !owner ||
        !(IsWellKnownSid(owner, WinBuiltinAdministratorsSid) || IsWellKnownSid(owner, WinLocalSystemSid)) ||
        !GetSecurityDescriptorDacl(sd, &present, &dacl, &defaulted) || !present || !dacl ||
        !IsValidAcl(dacl)) return false;
    // Reject callback/object ACEs whose semantics this narrow adapter does not model.
    for (DWORD i = 0; i < dacl->AceCount; ++i) {
        void* ace = nullptr;
        if (!GetAce(dacl, i, &ace)) return false;
        const auto kind = static_cast<ACE_HEADER*>(ace)->AceType;
        if (kind != ACCESS_ALLOWED_ACE_TYPE && kind != ACCESS_DENIED_ACE_TYPE) return false;
    }
    DWORD granted = 0; BOOL allowed = FALSE;
    alignas(PRIVILEGE_SET) BYTE privilegeStorage[4096]{};
    DWORD size = sizeof(privilegeStorage);
    return AccessCheck(sd, token, MAXIMUM_ALLOWED, &mapping,
                       reinterpret_cast<PRIVILEGE_SET*>(privilegeStorage), &size, &granted, &allowed) &&
        allowed && !(granted & forbidden) && (granted & required) == required;
}
bool Object(HANDLE object, SE_OBJECT_TYPE type, HANDLE token, GENERIC_MAPPING mapping, DWORD forbidden) {
    PSECURITY_DESCRIPTOR sd = nullptr;
    const DWORD status = GetSecurityInfo(object, type, OWNER_SECURITY_INFORMATION | DACL_SECURITY_INFORMATION,
                                         nullptr, nullptr, nullptr, nullptr, &sd);
    Local memory; memory.p = sd;
    return status == ERROR_SUCCESS && Descriptor(sd, token, mapping, forbidden);
}
constexpr DWORD kMutate = DELETE | WRITE_DAC | WRITE_OWNER;
bool Registry(const std::wstring& path, HANDLE token, RegKey& out) {
    if (RegOpenKeyExW(HKEY_LOCAL_MACHINE, path.c_str(), 0, KEY_READ | READ_CONTROL | KEY_WOW64_64KEY,
                      &out.h) != ERROR_SUCCESS) return false;
    GENERIC_MAPPING map{KEY_READ, KEY_WRITE, KEY_EXECUTE, KEY_ALL_ACCESS};
    return Object(out.h, SE_REGISTRY_KEY, token, map, kMutate | KEY_SET_VALUE | KEY_CREATE_SUB_KEY);
}
bool Dword(HKEY key, const wchar_t* name, DWORD expected) {
    DWORD type = 0, value = 0, size = sizeof(value);
    return RegQueryValueExW(key, name, nullptr, &type, reinterpret_cast<BYTE*>(&value), &size) == ERROR_SUCCESS &&
        type == REG_DWORD && size == sizeof(value) && value == expected;
}
bool Storage(HKEY log, HANDLE token, std::vector<std::unique_ptr<Handle>>& pins) {
    DWORD type = 0, size = 0;
    if (RegQueryValueExW(log, L"File", nullptr, &type, nullptr, &size) != ERROR_SUCCESS ||
        (type != REG_SZ && type != REG_EXPAND_SZ) || size < 4 || size > 32768 || size % 2) return false;
    std::vector<wchar_t> value(size / 2);
    if (RegQueryValueExW(log, L"File", nullptr, &type, reinterpret_cast<BYTE*>(value.data()), &size) != ERROR_SUCCESS ||
        value.back() != 0 || std::wcslen(value.data()) != value.size() - 1) return false;
    std::wstring path = value.data();
    if (type == REG_EXPAND_SZ && path.size() >= 12 && _wcsnicmp(path.c_str(), L"%SystemRoot%", 12) == 0) {
        wchar_t windows[32768]{};
        const auto length = GetWindowsDirectoryW(windows, 32768);
        if (!length || length >= 32768) return false;
        path = std::wstring(windows, length) + path.substr(12);
    }
    // Never resolve arbitrary client-controlled environment variables to infer
    // the Event Log service's backing file identity.
    if (path.find(L'%') != std::wstring::npos) return false;
    if (path.size() < 6 || path[1] != L':' || path[2] != L'\\' ||
        !((path[0] >= L'A' && path[0] <= L'Z') || (path[0] >= L'a' && path[0] <= L'z')) ||
        path.find(L'/', 3) != std::wstring::npos || path.find(L':', 3) != std::wstring::npos) return false;
    const auto root = path.substr(0, 3);
    DWORD flags = 0;
    if (GetDriveTypeW(root.c_str()) != DRIVE_FIXED ||
        !GetVolumeInformationW(root.c_str(), nullptr, 0, nullptr, nullptr, &flags, nullptr, 0) ||
        !(flags & FILE_PERSISTENT_ACLS)) return false;
    GENERIC_MAPPING map{FILE_GENERIC_READ, FILE_GENERIC_WRITE, FILE_GENERIC_EXECUTE, FILE_ALL_ACCESS};
    auto rootPin = std::make_unique<Handle>(CreateFileW(root.c_str(), READ_CONTROL | FILE_READ_ATTRIBUTES | FILE_LIST_DIRECTORY,
        FILE_SHARE_READ | FILE_SHARE_WRITE, nullptr, OPEN_EXISTING,
        FILE_FLAG_OPEN_REPARSE_POINT | FILE_FLAG_BACKUP_SEMANTICS, nullptr));
    BY_HANDLE_FILE_INFORMATION rootInfo{};
    if (!rootPin->h || !GetFileInformationByHandle(rootPin->h, &rootInfo) ||
        !(rootInfo.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) ||
        (rootInfo.dwFileAttributes & FILE_ATTRIBUTE_REPARSE_POINT) ||
        !Object(rootPin->h, SE_FILE_OBJECT, token, map, kMutate | FILE_DELETE_CHILD)) return false;
    pins.push_back(std::move(rootPin));
    unsigned components = 0;
    for (size_t pos = 3;;) {
        if (++components > 64) return false;
        const auto end = path.find(L'\\', pos);
        const bool file = end == std::wstring::npos;
        const auto part = path.substr(pos, file ? std::wstring::npos : end - pos);
        if (part.empty() || part == L"." || part == L".." || part.back() == L'.' || part.back() == L' ') return false;
        auto pin = std::make_unique<Handle>(CreateFileW(path.substr(0, file ? path.size() : end).c_str(),
            READ_CONTROL | FILE_READ_ATTRIBUTES | (file ? 0 : FILE_LIST_DIRECTORY),
            FILE_SHARE_READ | FILE_SHARE_WRITE, nullptr, OPEN_EXISTING,
            FILE_FLAG_OPEN_REPARSE_POINT | FILE_FLAG_BACKUP_SEMANTICS, nullptr));
        BY_HANDLE_FILE_INFORMATION info{};
        if (!pin->h || GetFileType(pin->h) != FILE_TYPE_DISK ||
            !GetFileInformationByHandle(pin->h, &info) || (info.dwFileAttributes & FILE_ATTRIBUTE_REPARSE_POINT) ||
            bool(info.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) == file ||
            !Object(pin->h, SE_FILE_OBJECT, token, map,
                kMutate | (file ? FILE_WRITE_DATA | FILE_APPEND_DATA | FILE_WRITE_EA | FILE_WRITE_ATTRIBUTES : FILE_DELETE_CHILD))) return false;
        pins.push_back(std::move(pin));
        if (file) return info.nNumberOfLinks == 1;
        pos = end + 1;
    }
}
bool ServiceProtected(HANDLE token) {
    struct ServiceHandle { SC_HANDLE h; ~ServiceHandle() { if (h) CloseServiceHandle(h); } };
    ServiceHandle manager{OpenSCManagerW(nullptr, nullptr, SC_MANAGER_CONNECT)};
    if (!manager.h) return false;
    ServiceHandle service{OpenServiceW(manager.h, L"EventLog", READ_CONTROL)};
    if (!service.h) return false;
    DWORD needed = 0;
    QueryServiceObjectSecurity(service.h, OWNER_SECURITY_INFORMATION | DACL_SECURITY_INFORMATION,
                               nullptr, 0, &needed);
    if (GetLastError() != ERROR_INSUFFICIENT_BUFFER || !needed || needed > 65536) return false;
    std::vector<BYTE> descriptor(needed);
    if (!QueryServiceObjectSecurity(service.h, OWNER_SECURITY_INFORMATION | DACL_SECURITY_INFORMATION,
                                    descriptor.data(), needed, &needed)) return false;
    GENERIC_MAPPING map{STANDARD_RIGHTS_READ | SERVICE_QUERY_CONFIG | SERVICE_QUERY_STATUS |
        SERVICE_INTERROGATE | SERVICE_ENUMERATE_DEPENDENTS,
        STANDARD_RIGHTS_WRITE | SERVICE_CHANGE_CONFIG, STANDARD_RIGHTS_EXECUTE |
        SERVICE_START | SERVICE_STOP | SERVICE_PAUSE_CONTINUE | SERVICE_USER_DEFINED_CONTROL,
        SERVICE_ALL_ACCESS};
    return Descriptor(descriptor.data(), token, map, kMutate | SERVICE_CHANGE_CONFIG |
        SERVICE_STOP | SERVICE_PAUSE_CONTINUE | SERVICE_USER_DEFINED_CONTROL);
}
bool UniqueSource(const WindowsOptions& options) {
    RegKey root;
    if (RegOpenKeyExW(HKEY_LOCAL_MACHINE, L"SYSTEM\\CurrentControlSet\\Services\\EventLog", 0,
                      KEY_READ | KEY_WOW64_64KEY, &root.h) != ERROR_SUCCESS) return false;
    for (DWORD index = 0; index < 256; ++index) {
        wchar_t name[256]{}; DWORD length = 256;
        const auto status = RegEnumKeyExW(root.h, index, name, &length, nullptr, nullptr, nullptr, nullptr);
        if (status == ERROR_NO_MORE_ITEMS) return true;
        if (status != ERROR_SUCCESS) return false;
        if (_wcsicmp(name, options.log.c_str()) == 0) continue;
        RegKey duplicate;
        const auto path = std::wstring(name) + L"\\" + options.source;
        const auto result = RegOpenKeyExW(root.h, path.c_str(), 0, KEY_READ | KEY_WOW64_64KEY, &duplicate.h);
        if (result != ERROR_FILE_NOT_FOUND && result != ERROR_PATH_NOT_FOUND) return false;
    }
    return false;
}
Status Check(const WindowsOptions& options, HANDLE token, std::vector<std::unique_ptr<Handle>>& pins) {
    if (!UniqueSource(options) || !ServiceProtected(token)) return Status::ProtectionDenied;
    const std::wstring base = L"SYSTEM\\CurrentControlSet\\Services\\EventLog\\";
    // Verify each registry ancestor; a writable parent could replace a protected key.
    const std::wstring leaf = base + options.log + L"\\" + options.source;
    for (size_t end = leaf.find(L'\\');; end = leaf.find(L'\\', end + 1)) {
        RegKey key;
        if (!Registry(leaf.substr(0, end), token, key)) return Status::ProtectionDenied;
        if (end == std::wstring::npos) break;
    }
    // Classic logs can have a corresponding modern channel configuration key.
    // If present, it is a second retention/enable/ACL mutation path to protect.
    const std::wstring channel = L"SOFTWARE\\Microsoft\\Windows\\CurrentVersion\\WINEVT\\Channels\\" + options.log;
    for (size_t end = channel.find(L'\\');; end = channel.find(L'\\', end + 1)) {
        const auto path = channel.substr(0, end);
        RegKey probe;
        const auto status = RegOpenKeyExW(HKEY_LOCAL_MACHINE, path.c_str(), 0,
            KEY_READ | KEY_WOW64_64KEY, &probe.h);
        if (status == ERROR_FILE_NOT_FOUND || status == ERROR_PATH_NOT_FOUND) break;
        RegKey key;
        if (status != ERROR_SUCCESS || !Registry(path, token, key)) return Status::ProtectionDenied;
        if (end == std::wstring::npos) break;
    }
    RegKey log;
    if (!Registry(base + options.log, token, log) || !Dword(log.h, L"Retention", 604800) ||
        !Dword(log.h, L"MaxSize", options.capacityBytes) || !Storage(log.h, token, pins)) return Status::ProtectionDenied;
    EventHandle config; config.h = EvtOpenChannelConfig(nullptr, options.log.c_str(), 0);
    if (!config.h) return Status::Unavailable;
    DWORD needed = 0;
    EvtGetChannelConfigProperty(config.h, EvtChannelConfigAccess, 0, 0, nullptr, &needed);
    if (GetLastError() != ERROR_INSUFFICIENT_BUFFER || needed < sizeof(EVT_VARIANT) || needed > 65536)
        return Status::ProtectionDenied;
    std::vector<BYTE> buffer(needed);
    auto* property = reinterpret_cast<EVT_VARIANT*>(buffer.data());
    if (!EvtGetChannelConfigProperty(config.h, EvtChannelConfigAccess, 0, needed, property, &needed) ||
        property->Type != EvtVarTypeString || !property->StringVal) return Status::ProtectionDenied;
    PSECURITY_DESCRIPTOR sd = nullptr;
    if (!ConvertStringSecurityDescriptorToSecurityDescriptorW(property->StringVal, SDDL_REVISION_1, &sd, nullptr))
        return Status::ProtectionDenied;
    Local memory; memory.p = sd;
    GENERIC_MAPPING map{1 | READ_CONTROL, 2 | READ_CONTROL, READ_CONTROL, 7 | STANDARD_RIGHTS_ALL};
    // 0x2 is EVENTLOG_WRITE, 0x4 EVENTLOG_CLEAR. Only read/publish rights allowed.
    if (!Descriptor(sd, token, map, ~(DWORD(1 | 2 | READ_CONTROL | SYNCHRONIZE)), 2)) return Status::ProtectionDenied;
    return Status::Verified;
}
bool Safe(std::string_view value, size_t maximum, bool filter = false) {
    if (value.size() > maximum) return false;
    for (char c : value)
        if (!((c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') ||
              (c >= '0' && c <= '9') || c == '_' || c == '-' || c == '.' || (filter && c == ':'))) return false;
    return true;
}
std::string HexId(const std::array<uint8_t, 16>& id) {
    static constexpr char digits[] = "0123456789abcdef";
    std::string s;
    for (auto c : id) { s += digits[c >> 4]; s += digits[c & 15]; }
    return s;
}
bool Serialize(const ipc::AuditEvent& event, const Coordinates& coordinates, std::wstring& output) {
    if (!event.outcome || coordinates.schema != 1 || !coordinates.aggregateCount ||
        coordinates.build.empty() || !Safe(coordinates.build, 128) || !Safe(coordinates.filter, 128, true) ||
        event.bytes > 65536 || !(event.opcode <= 5 || (event.opcode >= 0x100 && event.opcode <= 0x105))) return false;
    const std::string_view outcome(event.outcome);
    constexpr std::string_view allowed[] = {"ok", "stale", "restart_required", "refused", "opened", "closed",
        "challenge", "ready", "client_exit", "auth_failed", "rate_limited", "audit_unavailable", "deadline_expired",
        "invalid_request", "offline_denied", "revoked", "unavailable", "unknown", "snapshot_busy", "not_found",
        "snapshot_changed", "oversize", "corrupt_frame"};
    bool found = false;
    for (auto item : allowed) if (outcome == item) found = true;
    if (!found) return false;
    const auto line = "schema=1 session=" + HexId(coordinates.session) + " request=" + HexId(event.request) +
        " build=" + coordinates.build + " opcode=" + std::to_string(event.opcode) +
        " outcome=" + std::string(outcome) + " time_ms=" + std::to_string(event.time) +
        " bytes=" + std::to_string(event.bytes) + " count=" + std::to_string(coordinates.aggregateCount) +
        " filter=" + coordinates.filter;
    output.assign(line.begin(), line.end());
    return true;
}
Status AppendProtected(const WindowsOptions& options, const std::wstring& text, uint64_t deadline) {
    HANDLE log = OpenEventLogW(nullptr, options.log.c_str());
    if (!log) return Status::Unavailable;
    struct CloseLog { HANDLE h; ~CloseLog() { CloseEventLog(h); } } close{log};
    DWORD count = 0, oldest = 0;
    if (!GetNumberOfEventLogRecords(log, &count) || (count && !GetOldestEventLogRecord(log, &oldest)) ||
        (count && uint64_t(oldest) + count - 1 > MAXDWORD)) return Status::Unavailable;
    const DWORD previous = count ? oldest + count - 1 : 0;
    Source source; source.h = RegisterEventSourceW(nullptr, options.source.c_str());
    if (!source.h) return Status::Unavailable;
    if (Now() >= deadline) return Status::Deadline;
    const wchar_t* strings[] = {text.c_str()};
    if (!ReportEventW(source.h, EVENTLOG_INFORMATION_TYPE, 0, 1, nullptr, 1, 0, strings, nullptr))
        return GetLastError() == ERROR_LOG_FILE_FULL ? Status::Full : Status::Unavailable;
    if (Now() >= deadline) return Status::Deadline;
    // Read back the exact protected target; a stale OS source registration that
    // routes into Application must never produce an accepted receipt.
    std::vector<BYTE> buffer(65536);
    DWORD read = 0, needed = 0;
    if (!ReadEventLogW(log, EVENTLOG_SEQUENTIAL_READ | EVENTLOG_BACKWARDS_READ, 0,
                      buffer.data(), DWORD(buffer.size()), &read, &needed)) return Status::Unavailable;
    auto matches = [](const BYTE* record, DWORD length, DWORD offset, const std::wstring& wanted) {
        const uint64_t bytes = (uint64_t(wanted.size()) + 1) * sizeof(wchar_t);
        return offset % sizeof(wchar_t) == 0 && offset <= length && bytes <= length - offset &&
            std::memcmp(record + offset, wanted.c_str(), size_t(bytes)) == 0;
    };
    unsigned scanned = 0;
    for (DWORD offset = 0; offset < read && scanned++ < 64;) {
        if (read - offset < sizeof(EVENTLOGRECORD)) return Status::Unavailable;
        const auto* record = reinterpret_cast<const EVENTLOGRECORD*>(buffer.data() + offset);
        if (record->Length < sizeof(EVENTLOGRECORD) || record->Length > read - offset) return Status::Unavailable;
        if (record->RecordNumber <= previous) break;
        if (record->EventID == 1 && record->NumStrings == 1 &&
            matches(buffer.data() + offset, record->Length, sizeof(EVENTLOGRECORD), options.source) &&
            matches(buffer.data() + offset, record->Length, record->StringOffset, text)) return Status::Accepted;
        offset += record->Length;
    }
    return Status::Unavailable; // no unbounded scan or retries when flooded
}
// Global cap: at most ONE unfinished OS RPC, even across collector instances.
// A stalled RPC keeps admission Busy; timeout never turns its eventual return
// into a successful receipt. No thread termination or wait on the game thread.
std::atomic_flag inFlight = ATOMIC_FLAG_INIT;
struct Job {
    std::mutex mutex;
    std::condition_variable ready;
    Receipt receipt{};
    bool done = false;
};
class WindowsCollector final : public Collector {
    WindowsOptions options_;
    Receipt Run(uint64_t deadline, std::wstring text, bool append) {
        const auto start = Now();
        if (start >= deadline) return Failure(Status::Deadline);
        if (deadline - start > 2000) return Failure(Status::Invalid);
        if (!Name(options_.log) || !Name(options_.source) || options_.capacityBytes < 65536 ||
            options_.capacityBytes % 65536) return Failure(Status::Invalid);
        HANDLE callerToken = nullptr;
        if (OpenThreadToken(GetCurrentThread(), TOKEN_QUERY, TRUE, &callerToken)) {
            CloseHandle(callerToken); return Failure(Status::IdentityDenied);
        }
        if (GetLastError() != ERROR_NO_TOKEN) return Failure(Status::IdentityDenied);
        if (inFlight.test_and_set()) return Failure(Status::Busy);
        std::shared_ptr<Job> job;
        try {
            job = std::make_shared<Job>();
            // Detached OS RPC may outlive a timeout/collector. Pin its code until
            // process exit; this is a host component, never a reloadable plugin.
            HMODULE module = nullptr;
            if (!GetModuleHandleExW(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS | GET_MODULE_HANDLE_EX_FLAG_PIN,
                                   reinterpret_cast<LPCWSTR>(&Now), &module)) {
                inFlight.clear(); return Failure(Status::Unavailable);
            }
            const auto options = options_;
            std::thread([job, options, text = std::move(text), append, deadline] {
                Receipt receipt = Failure(Status::Unavailable);
                try {
                    Handle token;
                    std::vector<std::unique_ptr<Handle>> pins;
                    if (!Identity(token)) receipt = Failure(Status::IdentityDenied);
                    else {
                        const auto status = Check(options, token.h, pins);
                        receipt = {status, Now(), status == Status::Verified, true};
                        if (receipt.completedMs >= deadline) receipt.status = Status::Deadline;
                        else if (status == Status::Verified && append) {
                            receipt.status = AppendProtected(options, text, deadline);
                            if (receipt.status == Status::Accepted) {
                                // Revalidate privileges/config AFTER OS service acknowledgement.
                                std::vector<std::unique_ptr<Handle>> after;
                                if (Check(options, token.h, after) != Status::Verified)
                                    receipt.status = Status::ProtectionDenied;
                            }
                            receipt.completedMs = Now();
                            if (receipt.completedMs >= deadline) receipt.status = Status::Deadline;
                        }
                    }
                } catch (...) { receipt = Failure(Status::Unavailable); }
                { std::lock_guard<std::mutex> lock(job->mutex); job->receipt = receipt; job->done = true; }
                inFlight.clear(); job->ready.notify_one();
            }).detach();
        } catch (...) { inFlight.clear(); return Failure(Status::Unavailable); }
        std::unique_lock<std::mutex> lock(job->mutex);
        while (!job->done) {
            const auto now = Now();
            if (now >= deadline) return Failure(Status::Deadline);
            job->ready.wait_for(lock, std::chrono::milliseconds(deadline - now));
        }
        if (Now() >= deadline) return Failure(Status::Deadline);
        return job->receipt;
    }
public:
    explicit WindowsCollector(WindowsOptions options) : options_(std::move(options)) {}
    Receipt Verify(uint64_t deadline) override { return Run(deadline, {}, false); }
    Receipt Append(const ipc::AuditEvent& event, const Coordinates& coordinates, uint64_t deadline) override {
        if (Now() >= deadline) return Failure(Status::Deadline);
        std::wstring text;
        if (!Serialize(event, coordinates, text)) return Failure(Status::Invalid);
        return Run(deadline, std::move(text), true);
    }
};
}
std::shared_ptr<Collector> OpenWindowsCollector(const WindowsOptions& options) {
    return std::make_shared<WindowsCollector>(options);
}
#endif
} // namespace sco::diagnostics::audit
