// GPL-3.0-only. Startup snapshots over the private content index; public calls
// only copy memory. No game, parser, transport, arbitrary-path or script execution.
#include "sco/content.h"
#include "sco/host.h"
#include "sco/log.h"
#include "../api/internal.h"
#include <algorithm>
#include <cstring>
#include <limits>
#include <string>
#include <vector>
#ifdef _WIN32
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#else
#include <fcntl.h>
#include <sys/stat.h>
#include <unistd.h>
#endif

namespace sco::content {
namespace {
struct Entry { sco_content_item info{}; std::vector<unsigned char> bytes; };
bool g_started = false, g_snapshotted = false;
uint64_t g_nextId = 1; // never reset: old IDs cannot alias a new host start
std::vector<Entry> g_entries;

bool Supported(plugins::ContentKind k) {
    return k == plugins::ContentKind::List || k == plugins::ContentKind::Script;
}

// Names originate in ContentIndex, not callers. Still reject traversal, empty
// components, Windows alternate streams/separators and absolute paths here.
std::vector<std::string> Components(const std::string& name) {
    std::vector<std::string> out;
    if (name.empty() || name.size() > SCO_CONTENT_MAX_NAME_BYTES) return out;
    size_t at = 0;
    while (at < name.size()) {
        size_t end = name.find('/', at);
        if (end == std::string::npos) end = name.size();
        const std::string s = name.substr(at, end - at);
        if (s.empty() || s == "." || s == ".." || s.find_first_of("\\:\0", 0, 3) != std::string::npos)
            return {};
        out.push_back(s);
        at = end + 1;
    }
    if (name.back() == '/') return {};
    return out;
}

// Open beneath an anchored pack directory without following links. POSIX uses
// openat/O_NOFOLLOW at each step. Windows holds each non-reparse directory handle
// without FILE_SHARE_DELETE, preventing replacement while opening the next step.
// Ancestors of the host-configured pack root are trusted installation policy.
bool ReadFile(const plugins::Plugin& pack, const plugins::ContentItem& item,
              std::vector<unsigned char>& bytes) {
    const auto parts = Components(item.name);
    if (parts.empty()) return false;
#ifdef _WIN32
    struct Handles {
        std::vector<HANDLE> all;
        ~Handles() { for (auto h : all) CloseHandle(h); }
        bool Add(const std::filesystem::path& p, bool directory) {
            const HANDLE h = CreateFileW(p.c_str(), GENERIC_READ, FILE_SHARE_READ,
                nullptr, OPEN_EXISTING, FILE_FLAG_OPEN_REPARSE_POINT | FILE_FLAG_BACKUP_SEMANTICS, nullptr);
            if (h == INVALID_HANDLE_VALUE) return false;
            all.push_back(h);
            BY_HANDLE_FILE_INFORMATION info{};
            return GetFileInformationByHandle(h, &info) &&
                !(info.dwFileAttributes & FILE_ATTRIBUTE_REPARSE_POINT) &&
                bool(info.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) == directory &&
                GetFileType(h) == FILE_TYPE_DISK;
        }
    } handles;
    auto path = pack.dir;
    if (!handles.Add(path, true)) return false;
    for (size_t i = 0; i < parts.size(); ++i) {
        const auto& part = parts[i];
        // Win32 normalizes trailing periods/spaces; refuse rather than alias.
        if (part.back() == '.' || part.back() == ' ') return false;
        // char8_t selects the UTF-8 path constructor (unlike a native char path).
        path /= std::filesystem::path(std::u8string(part.begin(), part.end()));
        if (!handles.Add(path, i + 1 < parts.size())) return false;
    }
    const HANDLE file = handles.all.back();
    LARGE_INTEGER size{};
    if (!GetFileSizeEx(file, &size) || size.QuadPart < 0 || size.QuadPart > SCO_CONTENT_MAX_FILE_BYTES) return false;
    bytes.resize(static_cast<size_t>(size.QuadPart));
    DWORD read = 0;
    if (!bytes.empty() && (!::ReadFile(file, bytes.data(), static_cast<DWORD>(bytes.size()), &read, nullptr) || read != bytes.size()))
        return false;
    unsigned char extra;
    return ::ReadFile(file, &extra, 1, &read, nullptr) && read == 0;
#else
    struct Fd {
        int value = -1;
        ~Fd() { if (value >= 0) close(value); }
    } fd;
    fd.value = open(pack.dir.c_str(), O_RDONLY | O_DIRECTORY | O_NOFOLLOW | O_CLOEXEC);
    if (fd.value < 0) return false;
    for (size_t i = 0; i < parts.size(); ++i) {
        const int flags = O_RDONLY | O_NOFOLLOW | O_CLOEXEC | O_NONBLOCK |
                          (i + 1 < parts.size() ? O_DIRECTORY : 0);
        const int next = openat(fd.value, parts[i].c_str(), flags);
        if (next < 0) return false;
        close(fd.value); fd.value = next;
    }
    struct stat st{};
    if (fstat(fd.value, &st) || !S_ISREG(st.st_mode) || st.st_size < 0 ||
        static_cast<uint64_t>(st.st_size) > SCO_CONTENT_MAX_FILE_BYTES) return false;
    bytes.resize(static_cast<size_t>(st.st_size));
    size_t done = 0;
    while (done < bytes.size()) {
        const auto n = read(fd.value, bytes.data() + done, bytes.size() - done);
        if (n <= 0) return false;
        done += static_cast<size_t>(n);
    }
    unsigned char extra;
    return read(fd.value, &extra, 1) == 0;
#endif
}

sco_result Admission(sco_plugin* self) {
    if (!OnGameThread()) return SCO_WRONG_THREAD;
    if (!g_started || detail::RuntimeStopping()) return SCO_UNAVAILABLE;
    if (!host::PluginId(self) || detail::Released(self)) return SCO_BAD_ARG;
    return SCO_OK;
}
sco_result Item(sco_plugin* self, sco_content_kind kind, uint32_t ordinal, sco_content_item* out) {
    const auto r = Admission(self);
    if (r != SCO_OK) return r;
    if (!out || out->size < sizeof(*out) || (kind != SCO_CONTENT_LIST && kind != SCO_CONTENT_SCRIPT)) return SCO_BAD_ARG;
    for (const auto& e : g_entries) {
        if (e.info.kind != kind) continue;
        if (ordinal-- == 0) { *out = e.info; return SCO_OK; }
    }
    return SCO_NOT_FOUND;
}
sco_result Read(sco_plugin* self, uint64_t id, void* out, uint32_t* io) {
    const auto r = Admission(self);
    if (r != SCO_OK) return r;
    if (!io || (!out && *io)) return SCO_BAD_ARG;
    for (const auto& e : g_entries) {
        if (e.info.id != id) continue;
        const auto n = static_cast<uint32_t>(e.bytes.size());
        if (*io < n) { *io = n; return SCO_TOO_MANY; }
        if (n) std::memcpy(out, e.bytes.data(), n);
        *io = n;
        return SCO_OK;
    }
    return SCO_NOT_FOUND;
}
const sco_content_v1 kTable{ sizeof(sco_content_v1), 0, Item, Read };
}

Result Start() {
    if (!OnGameThread()) return Result::WrongThread;
    if (g_started) return Result::BadArg;
    const auto r = host::ProvideHostService(SCO_CONTENT_NAME, SCO_CONTENT_VERSION_1_0, &kTable);
    if (r == Result::Ok) { g_started = true; g_snapshotted = false; g_entries.clear(); }
    return r;
}
Result Snapshot(std::vector<plugins::Plugin>& list, plugins::ContentIndex& index) {
    if (!OnGameThread()) return Result::WrongThread;
    if (!g_started || detail::RuntimeStopping()) return Result::Unavailable;
    if (g_snapshotted) return Result::BadArg;
    try {
        std::vector<Entry> all;
        uint64_t total = 0;
        bool refused = false;
        for (auto& pack : list) {
            if (pack.manifest.kind != plugins::Kind::Data || pack.state != plugins::State::Loaded) continue;
            std::vector<Entry> mine;
            uint64_t packBytes = 0;
            bool valid = pack.manifest.id.size() <= SCO_CONTENT_MAX_PACK_BYTES;
            for (const auto* item : index.FromPlugin(pack.manifest.id)) {
                if (!Supported(item->kind)) continue;
                Entry e;
                if (!valid || !ReadFile(pack, *item, e.bytes) ||
                    all.size() + mine.size() >= SCO_CONTENT_MAX_ITEMS ||
                    total + packBytes + e.bytes.size() > SCO_CONTENT_MAX_TOTAL_BYTES ||
                    g_nextId == std::numeric_limits<uint64_t>::max()) { valid = false; break; }
                packBytes += e.bytes.size();
                e.info.size = sizeof(e.info);
                e.info.kind = static_cast<sco_content_kind>(item->kind);
                e.info.id = g_nextId++;
                std::memcpy(e.info.pack, pack.manifest.id.c_str(), pack.manifest.id.size() + 1);
                std::memcpy(e.info.name, item->name.c_str(), item->name.size() + 1);
                mine.push_back(std::move(e));
            }
            if (!valid) {
                pack.state = plugins::State::Refused;
                pack.reason = "content snapshot refused: unreadable/unsafe file or content limit";
                Log("[plugin] refused %s: %s", pack.manifest.id.c_str(), pack.reason.c_str());
                refused = true;
                continue;
            }
            total += packBytes;
            for (auto& e : mine) all.push_back(std::move(e));
        }
        // A snapshot refusal also refuses data dependents (including dependents
        // visited earlier in folder order). Never keep a partial dependency set.
        for (size_t pass = 0; pass < list.size(); ++pass) {
            bool changed = false;
            for (auto& pack : list) {
                if (pack.manifest.kind != plugins::Kind::Data || pack.state != plugins::State::Loaded) continue;
                if (const auto why = plugins::UnmetRequires(pack, list); !why.empty()) {
                    pack.state = plugins::State::Refused; pack.reason = why;
                    changed = true; refused = true;
                }
            }
            if (!changed) break;
        }
        if (refused) {
            std::erase_if(all, [&](const Entry& e) {
                for (const auto& p : list)
                    if (p.manifest.id == e.info.pack) return p.state != plugins::State::Loaded;
                return true;
            });
            index.Build(list);
        }
        g_entries = std::move(all);
        g_snapshotted = true;
        return Result::Ok;
    } catch (...) {
        // No partial snapshot becomes public. Caller must refuse consumer loading.
        g_entries.clear();
        return Result::TooMany;
    }
}
void Stop() {
    if (!OnGameThread()) return;
    g_started = false; g_snapshotted = false; g_entries.clear();
    host::WithdrawHostService(SCO_CONTENT_NAME);
}
const sco_content_v1* Table() { return &kTable; }
}
