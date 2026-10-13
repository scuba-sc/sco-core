// SPDX-License-Identifier: GPL-3.0-only
#ifdef _WIN32
#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>
#include <winternl.h>
#include <aclapi.h>
#include <bcrypt.h>
#include "diagnostics_platform.h"
#include <atomic>
#include <cstring>
#include <limits>
#include <memory>
#include <new>
#include <vector>

namespace sco::diagnostics::platform {
namespace {
constexpr DWORD kMappingBytes = 262144;
constexpr size_t kCredentialLimit = 65536;
constexpr wchar_t kMappingName[] = L"Local\\SCO_sco.diagnostics";
constexpr wchar_t kLeaseName[] = L"Local\\SCO_sco.diagnostics.peer-lease";

struct Handle {
    HANDLE value = nullptr;
    explicit Handle(HANDLE h = nullptr) : value(h == INVALID_HANDLE_VALUE ? nullptr : h) {}
    ~Handle() { if (value) CloseHandle(value); }
    Handle(const Handle&) = delete;
    Handle& operator=(const Handle&) = delete;
    Handle(Handle&& other) noexcept : value(other.value) { other.value = nullptr; }
    HANDLE detach() { HANDLE h = value; value = nullptr; return h; }
};

bool UserSid(std::vector<BYTE>& sid) {
    HANDLE raw = nullptr;
    if (!OpenProcessToken(GetCurrentProcess(), TOKEN_QUERY, &raw)) return false;
    Handle token(raw);
    DWORD bytes = 0;
    if (GetTokenInformation(token.value, TokenUser, nullptr, 0, &bytes) ||
        GetLastError() != ERROR_INSUFFICIENT_BUFFER || !bytes) return false;
    std::vector<BYTE> info(bytes);
    if (!GetTokenInformation(token.value, TokenUser, info.data(), bytes, &bytes)) return false;
    PSID user = reinterpret_cast<TOKEN_USER*>(info.data())->User.Sid;
    if (!IsValidSid(user)) return false;
    sid.resize(GetLengthSid(user));
    return CopySid(static_cast<DWORD>(sid.size()), sid.data(), user) != FALSE;
}

struct Security {
    std::vector<BYTE> sid, acl;
    SECURITY_DESCRIPTOR descriptor{};
    SECURITY_ATTRIBUTES attributes{};
    bool init(DWORD access) {
        if (!UserSid(sid)) return false;
        const DWORD bytes = static_cast<DWORD>(sizeof(ACL) + sizeof(ACCESS_ALLOWED_ACE) - sizeof(DWORD) + sid.size());
        acl.resize(bytes);
        PACL a = reinterpret_cast<PACL>(acl.data());
        if (!InitializeAcl(a, bytes, ACL_REVISION) ||
            !AddAccessAllowedAce(a, ACL_REVISION, access, sid.data()) ||
            !InitializeSecurityDescriptor(&descriptor, SECURITY_DESCRIPTOR_REVISION) ||
            !SetSecurityDescriptorOwner(&descriptor, sid.data(), FALSE) ||
            !SetSecurityDescriptorDacl(&descriptor, TRUE, a, FALSE) ||
            !SetSecurityDescriptorControl(&descriptor, SE_DACL_PROTECTED, SE_DACL_PROTECTED)) return false;
        attributes.nLength = sizeof(attributes);
        attributes.lpSecurityDescriptor = &descriptor;
        attributes.bInheritHandle = FALSE;
        return true;
    }
};

// No NULL DACL, foreign owner, broad inherited ACE, callback/object ACE or
// unknown ACL semantics is accepted. Even harmless foreign deny ACEs fail closed.
bool Private(HANDLE h, SE_OBJECT_TYPE type, PSID user) {
    PSID owner = nullptr;
    PACL acl = nullptr;
    PSECURITY_DESCRIPTOR sd = nullptr;
    const DWORD result = GetSecurityInfo(h, type, OWNER_SECURITY_INFORMATION | DACL_SECURITY_INFORMATION,
                                         &owner, nullptr, &acl, nullptr, &sd);
    bool valid = result == ERROR_SUCCESS && sd && owner && IsValidSid(owner) &&
                 EqualSid(owner, user) && acl && IsValidAcl(acl) && acl->AceCount;
    bool effective = false;
    if (valid) {
        SECURITY_DESCRIPTOR_CONTROL control = 0;
        DWORD revision = 0;
        valid = GetSecurityDescriptorControl(sd, &control, &revision) && (control & SE_DACL_PRESENT);
        for (DWORD i = 0; valid && i < acl->AceCount; ++i) {
            void* raw = nullptr;
            valid = GetAce(acl, i, &raw) != FALSE;
            if (!valid) break;
            const auto* ace = static_cast<const ACCESS_ALLOWED_ACE*>(raw);
            const auto* header = static_cast<const ACE_HEADER*>(raw);
            if (header->AceType != ACCESS_ALLOWED_ACE_TYPE ||
                header->AceSize < offsetof(ACCESS_ALLOWED_ACE, SidStart) + 8) { valid = false; break; }
            PSID sid = const_cast<DWORD*>(&ace->SidStart);
            const auto* sidHeader = static_cast<const SID*>(sid);
            const size_t sidBytes = 8u + 4u * sidHeader->SubAuthorityCount;
            valid = offsetof(ACCESS_ALLOWED_ACE, SidStart) + sidBytes <= header->AceSize &&
                    IsValidSid(sid) && EqualSid(sid, user);
            if (!(header->AceFlags & INHERIT_ONLY_ACE) && ace->Mask) effective = true;
        }
    }
    if (sd) LocalFree(sd);
    return valid && effective;
}

bool ExactSection(HANDLE h) {
    // VirtualQuery reports a view region, not the section's maximum extent.
    struct SectionBasic { void* base; ULONG attributes; LARGE_INTEGER bytes; };
    using Query = LONG (NTAPI*)(HANDLE, ULONG, void*, ULONG, ULONG*);
    HMODULE ntdll = GetModuleHandleW(L"ntdll.dll");
    FARPROC address = ntdll ? GetProcAddress(ntdll, "NtQuerySection") : nullptr;
    Query query = nullptr;
    static_assert(sizeof(query) == sizeof(address), "Windows function pointer size");
    std::memcpy(&query, &address, sizeof(query));
    if (!query) return false;
    SectionBasic info{};
    ULONG returned = 0;
    return query(h, 0 /* SectionBasicInformation */, &info, sizeof(info), &returned) >= 0 &&
           returned == sizeof(info) && info.bytes.QuadPart == kMappingBytes;
}

bool Empty(const Mapping& m) {
    return !m.base && !m.handle && !m.bytes && !m.owner && m.name.empty();
}
bool FinishMapping(Mapping& out, Handle& h, Security& security, bool owner) {
    if (!Private(h.value, SE_KERNEL_OBJECT, security.sid.data()) || !ExactSection(h.value)) return false;
    void* view = MapViewOfFile(h.value, FILE_MAP_READ | FILE_MAP_WRITE, 0, 0, kMappingBytes);
    if (!view) return false;
    // Build the potentially allocating string before transferring OS resources.
    try { out.name = MappingName(); }
    catch (...) { UnmapViewOfFile(view); return false; }
    out.base = view;
    out.bytes = kMappingBytes;
    out.owner = owner;
    out.handle = reinterpret_cast<uintptr_t>(h.detach());
    return true;
}

std::atomic_flag leaseHeld = ATOMIC_FLAG_INIT;
struct LeaseState {
    Handle mutex, stop, ready, thread;
    std::atomic<DWORD> result{WAIT_FAILED};
    LeaseState(HANDLE m, HANDLE s, HANDLE r) : mutex(m), stop(s), ready(r) {}
};
DWORD WINAPI LeaseThread(void* raw) {
    auto& state = *static_cast<LeaseState*>(raw);
    state.result = WaitForSingleObject(state.mutex.value, 0); // Never wait for a competing peer.
    SetEvent(state.ready.value);
    if (state.result == WAIT_OBJECT_0 || state.result == WAIT_ABANDONED) {
        WaitForSingleObject(state.stop.value, INFINITE);
        ReleaseMutex(state.mutex.value);
    }
    return 0;
}

bool Component(const std::wstring& value) {
    if (value.empty() || value == L"." || value == L".." || value.back() == L'.' || value.back() == L' ') return false;
    for (wchar_t c : value) if (c < 32 || c == L'/' || c == L':' || c == L'<' || c == L'>' ||
                              c == L'"' || c == L'|' || c == L'?' || c == L'*') return false;
    std::wstring stem = value.substr(0, value.find(L'.'));
    for (auto& c : stem) if (c >= L'a' && c <= L'z') c -= L'a' - L'A';
    if (stem == L"CON" || stem == L"PRN" || stem == L"AUX" || stem == L"NUL" ||
        stem == L"CONIN$" || stem == L"CONOUT$") return false;
    if (stem.size() == 4 && (stem.compare(0, 3, L"COM") == 0 || stem.compare(0, 3, L"LPT") == 0) &&
        ((stem[3] >= L'0' && stem[3] <= L'9') || stem[3] == L'\u00b9' ||
         stem[3] == L'\u00b2' || stem[3] == L'\u00b3')) return false;
    return true;
}

bool DiskInfo(HANDLE h, BY_HANDLE_FILE_INFORMATION& info, bool directory) {
    return GetFileType(h) == FILE_TYPE_DISK && GetFileInformationByHandle(h, &info) &&
           !(info.dwFileAttributes & FILE_ATTRIBUTE_REPARSE_POINT) &&
           !!(info.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) == directory &&
           (directory || info.nNumberOfLinks == 1);
}
struct Path {
    std::wstring full, parent, leaf;
    std::vector<Handle> ancestors;
    Security security;
    bool init(const std::string& input) {
        if (input.empty() || input.size() > 24000 || input.find('\0') != std::string::npos ||
            !security.init(FILE_ALL_ACCESS)) return false;
        const int length = MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, input.data(),
                                               static_cast<int>(input.size()), nullptr, 0);
        if (!length) return false;
        std::wstring path(length, L'\0');
        if (MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, input.data(), static_cast<int>(input.size()),
                                path.data(), length) != length) return false;
        // Only local, fully qualified DOS drive paths. No UNC/device/ADS or
        // drive-relative paths; extended syntax is generated, never operator supplied.
        if (path.size() < 6 || !((path[0] >= L'A' && path[0] <= L'Z') ||
            (path[0] >= L'a' && path[0] <= L'z')) || path[1] != L':' || path[2] != L'\\') return false;
        const std::wstring root = path.substr(0, 3);
        DWORD flags = 0;
        if (GetDriveTypeW(root.c_str()) != DRIVE_FIXED ||
            !GetVolumeInformationW(root.c_str(), nullptr, 0, nullptr, nullptr, &flags, nullptr, 0) ||
            !(flags & FILE_PERSISTENT_ACLS)) return false;
        std::vector<std::wstring> parts;
        size_t begin = 3;
        while (begin < path.size()) {
            size_t end = path.find(L'\\', begin);
            if (end == std::wstring::npos) end = path.size();
            std::wstring part = path.substr(begin, end - begin);
            if (!Component(part)) return false;
            parts.push_back(std::move(part));
            if (end == path.size()) break;
            begin = end + 1;
            if (begin == path.size()) return false;
        }
        if (parts.size() < 2) return false; // The immediate directory cannot be the drive root.
        parent = L"\\\\?\\" + root;
        for (size_t i = 0; i < parts.size(); ++i) {
            if (i) { if (parent.back() != L'\\') parent += L'\\'; parent += parts[i - 1]; }
            // FILE_LIST_DIRECTORY participates in share-access accounting;
            // metadata-only opens do not reliably pin against delete/rename.
            Handle dir(CreateFileW(parent.c_str(), FILE_LIST_DIRECTORY | FILE_READ_ATTRIBUTES | READ_CONTROL,
                                   FILE_SHARE_READ, nullptr, OPEN_EXISTING,
                                   FILE_FLAG_BACKUP_SEMANTICS | FILE_FLAG_OPEN_REPARSE_POINT, nullptr));
            BY_HANDLE_FILE_INFORMATION info{};
            if (!dir.value || !DiskInfo(dir.value, info, true)) return false;
            // Ancestors such as C:\\ and Users are necessarily shared OS directories;
            // hold ALL of them without share-delete to pin resolution. The immediate
            // operator directory, unlike traversal ancestors, must be user-only.
            if (i == parts.size() - 1 && !Private(dir.value, SE_FILE_OBJECT, security.sid.data())) return false;
            ancestors.push_back(std::move(dir));
        }
        leaf = parts.back();
        full = parent + L"\\" + leaf;
        return true;
    }
};

bool RenameInPlace(HANDLE h, const std::wstring& leaf) {
    // An absolute Win32 rename makes IopOpenLinkOrRenameTarget reopen the
    // pinned parent with FILE_WRITE_DATA, conflicting with FILE_SHARE_READ.
    // Native FileRenameInformation accepts a simple leaf with no root handle:
    // rename within the source file's directory without reopening that directory.
    // Keep every ancestor's no-write/no-delete sharing restriction intact.
    struct RenameInformation {
        BOOLEAN replace;
        HANDLE root;
        ULONG nameBytes;
        WCHAR name[1];
    };
    using Rename = LONG (NTAPI*)(HANDLE, IO_STATUS_BLOCK*, void*, ULONG, ULONG);
    HMODULE ntdll = GetModuleHandleW(L"ntdll.dll");
    FARPROC address = ntdll ? GetProcAddress(ntdll, "NtSetInformationFile") : nullptr;
    Rename rename = nullptr;
    static_assert(sizeof(rename) == sizeof(address), "Windows function pointer size");
    std::memcpy(&rename, &address, sizeof(rename));
    if (!rename || !Component(leaf)) return false;
    const size_t nameBytes = leaf.size() * sizeof(wchar_t);
    std::vector<BYTE> storage(sizeof(RenameInformation) + nameBytes, 0);
    auto* info = reinterpret_cast<RenameInformation*>(storage.data());
    info->replace = FALSE; // Never replace an existing credential or link.
    info->root = nullptr;
    info->nameBytes = static_cast<ULONG>(nameBytes);
    std::memcpy(info->name, leaf.data(), nameBytes);
    IO_STATUS_BLOCK status{};
    // CreateFileW opened h synchronously (no FILE_FLAG_OVERLAPPED).
    return rename(h, &status, info, static_cast<ULONG>(storage.size()),
                  10 /* FileRenameInformation */) == 0;
}

bool SafeFile(HANDLE h, Path& path) {
    BY_HANDLE_FILE_INFORMATION info{};
    return DiskInfo(h, info, false) && Private(h, SE_FILE_OBJECT, path.security.sid.data());
}
bool DeleteHandle(HANDLE h) {
    FILE_DISPOSITION_INFO info{};
    info.DeleteFile = TRUE;
    return SetFileInformationByHandle(h, FileDispositionInfo, &info, sizeof(info)) != FALSE;
}
} // namespace

bool Random(void* buffer, size_t bytes) {
    if (!bytes) return true;
    if (!buffer || bytes > std::numeric_limits<ULONG>::max()) return false;
    if (BCryptGenRandom(nullptr, static_cast<PUCHAR>(buffer), static_cast<ULONG>(bytes),
                        BCRYPT_USE_SYSTEM_PREFERRED_RNG) >= 0) return true;
    SecureZeroMemory(buffer, bytes);
    return false;
}
std::string MappingName() { return "Local\\SCO_sco.diagnostics"; }

bool Create(Mapping& mapping) {
    if (!Empty(mapping)) return false; // Do not discard/leak an existing owned handle.
    Security security;
    if (!security.init(SECTION_ALL_ACCESS)) return false;
    SetLastError(ERROR_SUCCESS);
    Handle h(CreateFileMappingW(INVALID_HANDLE_VALUE, &security.attributes, PAGE_READWRITE, 0,
                                kMappingBytes, kMappingName));
    const DWORD error = GetLastError();
    return h.value && error == ERROR_SUCCESS && FinishMapping(mapping, h, security, true);
}
bool Open(Mapping& mapping) {
    if (!Empty(mapping)) return false;
    Security security;
    if (!security.init(SECTION_ALL_ACCESS)) return false;
    Handle h(OpenFileMappingW(FILE_MAP_READ | FILE_MAP_WRITE | READ_CONTROL | SECTION_QUERY,
                             FALSE, kMappingName));
    return h.value && FinishMapping(mapping, h, security, false);
}
void Close(Mapping& mapping) {
    if (mapping.base) UnmapViewOfFile(mapping.base);
    if (mapping.handle) CloseHandle(reinterpret_cast<HANDLE>(mapping.handle));
    mapping = Mapping{};
}

bool Acquire(Lease& lease) {
    if (lease.handle || leaseHeld.test_and_set(std::memory_order_acquire)) return false;
    Security security, eventSecurity;
    if (!security.init(MUTEX_ALL_ACCESS) || !eventSecurity.init(EVENT_ALL_ACCESS)) {
        leaseHeld.clear(std::memory_order_release); return false;
    }
    Handle mutex(CreateMutexW(&security.attributes, FALSE, kLeaseName));
    Handle stop(CreateEventW(&eventSecurity.attributes, TRUE, FALSE, nullptr));
    Handle ready(CreateEventW(&eventSecurity.attributes, TRUE, FALSE, nullptr));
    if (!mutex.value || !stop.value || !ready.value ||
        !Private(mutex.value, SE_KERNEL_OBJECT, security.sid.data()) ||
        !Private(stop.value, SE_KERNEL_OBJECT, security.sid.data()) ||
        !Private(ready.value, SE_KERNEL_OBJECT, security.sid.data())) {
        leaseHeld.clear(std::memory_order_release); return false;
    }
    auto state = std::unique_ptr<LeaseState>(new (std::nothrow) LeaseState(mutex.value, stop.value, ready.value));
    if (!state) { leaseHeld.clear(std::memory_order_release); return false; }
    mutex.detach(); stop.detach(); ready.detach();
    state->thread.value = CreateThread(nullptr, 0, LeaseThread, state.get(), 0, nullptr);
    if (!state->thread.value) { leaseHeld.clear(std::memory_order_release); return false; }
    const DWORD waited = WaitForSingleObject(state->ready.value, INFINITE);
    if (waited != WAIT_OBJECT_0 || (state->result != WAIT_OBJECT_0 && state->result != WAIT_ABANDONED)) {
        SetEvent(state->stop.value);
        WaitForSingleObject(state->thread.value, INFINITE);
        leaseHeld.clear(std::memory_order_release); return false;
    }
    // A private worker owns the mutex: the public lifetime lease may be released
    // on any caller thread without violating Windows mutex thread ownership.
    lease.handle = reinterpret_cast<uintptr_t>(state.release());
    return true;
}
void Release(Lease& lease) {
    if (!lease.handle) return;
    auto state = std::unique_ptr<LeaseState>(reinterpret_cast<LeaseState*>(lease.handle));
    SetEvent(state->stop.value);
    WaitForSingleObject(state->thread.value, INFINITE);
    state.reset();
    lease.handle = 0;
    leaseHeld.clear(std::memory_order_release);
}

bool WriteCredential(const std::string& input, const std::string& json) {
    if (json.empty() || json.size() > kCredentialLimit) return false;
    Path path;
    if (!path.init(input)) return false;
    BYTE nonce[16];
    if (!Random(nonce, sizeof(nonce))) return false;
    static constexpr wchar_t hex[] = L"0123456789abcdef";
    std::wstring temporary = path.parent + L"\\.sco-diagnostics-";
    for (BYTE b : nonce) { temporary += hex[b >> 4]; temporary += hex[b & 15]; }
    Handle file(CreateFileW(temporary.c_str(), GENERIC_WRITE | DELETE | READ_CONTROL | FILE_READ_ATTRIBUTES,
                            0, &path.security.attributes, CREATE_NEW,
                            FILE_ATTRIBUTE_NORMAL | FILE_FLAG_OPEN_REPARSE_POINT, nullptr));
    if (!file.value) return false;
    if (!SafeFile(file.value, path)) return false; // Unknown security must not be deleted either.
    DWORD written = 0;
    bool ok = WriteFile(file.value, json.data(), static_cast<DWORD>(json.size()), &written, nullptr) &&
              written == json.size() && FlushFileBuffers(file.value);
    if (ok) {
        // Rename the verified, still-open file atomically; never replace a target.
        ok = SafeFile(file.value, path) && RenameInPlace(file.value, path.leaf);
    }
    if (!ok && SafeFile(file.value, path)) DeleteHandle(file.value);
    return ok;
}
bool ReadCredential(const std::string& input, std::string& json) {
    json.clear();
    Path path;
    if (!path.init(input)) return false;
    Handle file(CreateFileW(path.full.c_str(), GENERIC_READ | READ_CONTROL, FILE_SHARE_READ, nullptr,
                            OPEN_EXISTING, FILE_FLAG_OPEN_REPARSE_POINT, nullptr));
    LARGE_INTEGER size{};
    if (!file.value || !SafeFile(file.value, path) || !GetFileSizeEx(file.value, &size) ||
        size.QuadPart <= 0 || size.QuadPart > static_cast<LONGLONG>(kCredentialLimit)) return false;
    std::string result(static_cast<size_t>(size.QuadPart), '\0');
    DWORD got = 0, extra = 0;
    char trailing = 0;
    if (!ReadFile(file.value, result.data(), static_cast<DWORD>(result.size()), &got, nullptr) ||
        got != result.size() || !ReadFile(file.value, &trailing, 1, &extra, nullptr) || extra ||
        !SafeFile(file.value, path)) return false;
    json.swap(result);
    return true;
}
void RemoveCredential(const std::string& input) {
    Path path;
    if (!path.init(input)) return;
    Handle file(CreateFileW(path.full.c_str(), DELETE | READ_CONTROL | FILE_READ_ATTRIBUTES, 0, nullptr,
                            OPEN_EXISTING, FILE_FLAG_OPEN_REPARSE_POINT, nullptr));
    if (file.value && SafeFile(file.value, path)) DeleteHandle(file.value);
}
uint64_t NowMs() { return GetTickCount64(); }
} // namespace sco::diagnostics::platform
#endif // _WIN32
