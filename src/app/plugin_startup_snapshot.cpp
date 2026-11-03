#include "plugin_startup_snapshot.h"

#include <algorithm>
#include <chrono>
#include <cstdio>
#include <ctime>
#include <random>
#include <string_view>
#include <unordered_set>
#include <utility>

#ifdef _WIN32
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#else
#include <cerrno>
#include <fcntl.h>
#include <sys/file.h>
#include <sys/stat.h>
#include <unistd.h>
#endif

namespace sco::app::detail::plugin_startup {
namespace {

bool LowerHex(std::string_view value, size_t exact) {
    if (value.size() != exact) return false;
    for (char c : value)
        if (!((c >= '0' && c <= '9') || (c >= 'a' && c <= 'f'))) return false;
    return true;
}

bool ValidId(std::string_view value) {
    if (value.empty() || value.size() > 31) return false;
    for (char c : value)
        if (!((c >= 'a' && c <= 'z') || (c >= '0' && c <= '9') || c == '_')) return false;
    return true;
}

bool ValidAlias(std::string_view value) {
    constexpr std::string_view prefix = "unknown_";
    if (!value.starts_with(prefix) || value.size() == prefix.size() || value.size() > 31) return false;
    for (char c : value.substr(prefix.size())) if (c < '0' || c > '9') return false;
    return true;
}

bool ValidVersion(std::string_view value) {
    if (value.empty() || value.size() > 31) return false;
    for (char c : value)
        if (!((c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') ||
              (c >= '0' && c <= '9') || c == '.' || c == '_' || c == '+' || c == '-')) return false;
    return true;
}

bool Decimal(std::string_view value, int& out) {
    if (value.empty()) return false;
    int result = 0;
    for (char c : value) {
        if (c < '0' || c > '9') return false;
        result = result * 10 + (c - '0');
    }
    out = result;
    return true;
}

bool ValidUtc(std::string_view value) {
    if (value.size() != 20 || value[4] != '-' || value[7] != '-' || value[10] != 'T' ||
        value[13] != ':' || value[16] != ':' || value[19] != 'Z') return false;
    int year = 0, month = 0, day = 0, hour = 0, minute = 0, second = 0;
    if (!Decimal(value.substr(0, 4), year) || !Decimal(value.substr(5, 2), month) ||
        !Decimal(value.substr(8, 2), day) || !Decimal(value.substr(11, 2), hour) ||
        !Decimal(value.substr(14, 2), minute) || !Decimal(value.substr(17, 2), second)) return false;
    static constexpr int days[] = {31, 28, 31, 30, 31, 30, 31, 31, 30, 31, 30, 31};
    if (year < 1 || month < 1 || month > 12 || hour > 23 || minute > 59 || second > 59) return false;
    int lastDay = days[month - 1];
    const bool leap = (year % 4 == 0 && year % 100 != 0) || year % 400 == 0;
    if (month == 2 && leap) ++lastDay;
    return day >= 1 && day <= lastDay;
}

const char* RecordStateName(RecordState state) {
    switch (state) {
    case RecordState::Complete: return "complete";
    case RecordState::Starting: return "starting";
    case RecordState::Failed: return "failed";
    case RecordState::Incomplete: return "incomplete";
    }
    return nullptr;
}

const char* PluginStateName(plugins::State state) {
    switch (state) {
    case plugins::State::Off: return "off";
    case plugins::State::Disabled: return "disabled";
    case plugins::State::Refused: return "refused";
    case plugins::State::Ready: return "ready";
    case plugins::State::Loaded: return "loaded";
    case plugins::State::Crashed: return "crashed";
    case plugins::State::Unloaded: return "unloaded";
    case plugins::State::Faulted: return "faulted";
    }
    return nullptr;
}

const char* PluginKindName(plugins::Kind kind) {
    switch (kind) {
    case plugins::Kind::Native: return "native";
    case plugins::Kind::Lua: return "lua";
    case plugins::Kind::Data: return "data";
    case plugins::Kind::Builtin: return "builtin";
    }
    return nullptr;
}

std::optional<std::string> ReasonCode(plugins::State state, bool manifestValid) {
    switch (state) {
    case plugins::State::Off: return "plugins_off";
    case plugins::State::Disabled: return "plugin_disabled";
    case plugins::State::Refused: return manifestValid ? "plugin_refused" : "invalid_manifest";
    case plugins::State::Crashed: return "plugin_crashed";
    case plugins::State::Faulted: return "provider_fault_restart_required";
    case plugins::State::Ready:
    case plugins::State::Loaded:
    case plugins::State::Unloaded: return {};
    }
    return std::nullopt;
}

size_t LoadedCount(const std::vector<plugins::Plugin>& list) {
    return static_cast<size_t>(std::count_if(list.begin(), list.end(), [](const auto& plugin) {
        return plugin.state == plugins::State::Loaded;
    }));
}

bool ValidRow(const Row& row) {
    if (row.manifestValid) {
        if (!ValidId(row.id) || !row.version || !ValidVersion(*row.version) ||
            (row.kind != "native" && row.kind != "lua" && row.kind != "data" && row.kind != "builtin"))
            return false;
    } else if (!ValidAlias(row.id) || row.version || row.kind != "unknown") {
        return false;
    }
    if (row.state != "off" && row.state != "disabled" && row.state != "refused" && row.state != "ready" &&
        row.state != "loaded" && row.state != "crashed" && row.state != "unloaded" && row.state != "faulted")
        return false;
    if (row.restartRequired != (row.state == "faulted")) return false;
    if (!row.manifestValid && row.state != "off" && row.state != "disabled" && row.state != "refused") return false;

    const char* expected = nullptr;
    if (row.state == "off") expected = "plugins_off";
    else if (row.state == "disabled") expected = "plugin_disabled";
    else if (row.state == "refused") expected = "plugin_refused";
    else if (row.state == "crashed") expected = "plugin_crashed";
    else if (row.state == "faulted") expected = "provider_fault_restart_required";
    if (row.state == "refused" && !row.manifestValid) expected = "invalid_manifest";
    if (expected) return row.reasonCode && *row.reasonCode == expected;
    return !row.reasonCode;
}

bool Append(std::string& out, std::string_view part) {
    if (part.size() > kMaxBytes - out.size()) return false;
    out.append(part);
    return true;
}

bool AppendQuoted(std::string& out, std::string_view value) {
    if (!Append(out, "\"")) return false;
    for (unsigned char c : value) {
        if (c < 0x20 || c > 0x7e) return false;
        if (c == '\\' || c == '"') {
            const char escaped[] = {'\\', static_cast<char>(c)};
            if (!Append(out, std::string_view(escaped, sizeof(escaped)))) return false;
        } else if (!Append(out, std::string_view(reinterpret_cast<const char*>(&c), 1))) return false;
    }
    return Append(out, "\"");
}

bool AppendBool(std::string& out, bool value) { return Append(out, value ? "true" : "false"); }
bool AppendCount(std::string& out, size_t value) { return Append(out, std::to_string(value)); }

bool ValidRecord(const Record& record) {
    if (!LowerHex(record.sessionId, 32) || !ValidUtc(record.startedAtUtc) || !RecordStateName(record.state)) return false;
    if (record.state == RecordState::Starting) {
        return !record.completedAtUtc && !record.plugins && record.pluginCount == 0 && record.loadedCount == 0;
    }
    if (!record.completedAtUtc || !ValidUtc(*record.completedAtUtc) || *record.completedAtUtc < record.startedAtUtc)
        return false;
    if (record.state != RecordState::Complete) return !record.plugins;
    if (!record.plugins || record.plugins->size() > kMaxRows || record.pluginCount != record.plugins->size()) return false;
    size_t loaded = 0;
    for (const Row& row : *record.plugins) {
        if (!ValidRow(row)) return false;
        loaded += row.state == "loaded";
    }
    return loaded == record.loadedCount;
}

std::optional<std::string> UtcNow() {
    const std::time_t raw = std::chrono::system_clock::to_time_t(std::chrono::system_clock::now());
    std::tm utc{};
#ifdef _WIN32
    if (gmtime_s(&utc, &raw) != 0) return {};
#else
    if (!gmtime_r(&raw, &utc)) return {};
#endif
    char buffer[32]{};
    const int written = std::snprintf(buffer, sizeof(buffer), "%04d-%02d-%02dT%02d:%02d:%02dZ",
                                      utc.tm_year + 1900, utc.tm_mon + 1, utc.tm_mday,
                                      utc.tm_hour, utc.tm_min, utc.tm_sec);
    if (written != 20) return {};
    std::string result(buffer, static_cast<size_t>(written));
    return ValidUtc(result) ? std::optional<std::string>(std::move(result)) : std::nullopt;
}

std::optional<std::string> NewSessionId() {
    static constexpr char hex[] = "0123456789abcdef";
    try {
        std::random_device random;
        std::string result;
        result.reserve(32);
        for (size_t i = 0; i < 16; ++i) {
            const unsigned byte = random() & 0xffu;
            result.push_back(hex[(byte >> 4) & 0xfu]);
            result.push_back(hex[byte & 0xfu]);
        }
        return result;
    } catch (...) {
        return {};
    }
}

bool IsWriterTempName(std::string_view name) {
    constexpr std::string_view prefix = "plugin-startup.json.";
    constexpr std::string_view suffix = ".tmp";
    return name.size() == prefix.size() + 32 + suffix.size() && name.starts_with(prefix) &&
           name.ends_with(suffix) && LowerHex(name.substr(prefix.size(), 32), 32);
}

void RemoveAbandonedTemps(const std::filesystem::path& parent) noexcept {
    try {
        std::error_code ec;
        std::filesystem::directory_iterator it(parent, ec), end;
        for (; !ec && it != end; it.increment(ec)) {
            const std::string name = it->path().filename().string();
            if (!IsWriterTempName(name)) continue;
            std::error_code statusError;
            const auto type = std::filesystem::symlink_status(it->path(), statusError).type();
            if (statusError || type != std::filesystem::file_type::regular) continue;
            std::error_code removeError;
            std::filesystem::remove(it->path(), removeError);
        }
    } catch (...) {}
}

struct FileLock {
#ifdef _WIN32
    HANDLE handle = INVALID_HANDLE_VALUE;
    OVERLAPPED overlapped{};
#else
    int descriptor = -1;
#endif
    bool locked = false;

    bool Acquire(const std::filesystem::path& path) noexcept {
#ifdef _WIN32
        handle = CreateFileW(path.c_str(), GENERIC_READ | GENERIC_WRITE,
                             FILE_SHARE_READ | FILE_SHARE_WRITE,
                             nullptr, OPEN_ALWAYS,
                             FILE_ATTRIBUTE_NORMAL | FILE_FLAG_OPEN_REPARSE_POINT | FILE_FLAG_OVERLAPPED, nullptr);
        if (handle == INVALID_HANDLE_VALUE) return false;
        BY_HANDLE_FILE_INFORMATION info{};
        if (!GetFileInformationByHandle(handle, &info) ||
            (info.dwFileAttributes & (FILE_ATTRIBUTE_DIRECTORY | FILE_ATTRIBUTE_REPARSE_POINT))) {
            CloseHandle(handle); handle = INVALID_HANDLE_VALUE; return false;
        }
        if (!LockFileEx(handle, LOCKFILE_EXCLUSIVE_LOCK | LOCKFILE_FAIL_IMMEDIATELY,
                        0, MAXDWORD, MAXDWORD, &overlapped)) {
            CloseHandle(handle); handle = INVALID_HANDLE_VALUE; return false;
        }
        locked = true;
        return true;
#else
        int flags = O_CREAT | O_RDWR;
#ifdef O_CLOEXEC
        flags |= O_CLOEXEC;
#endif
#ifdef O_NOFOLLOW
        flags |= O_NOFOLLOW;
#endif
        descriptor = open(path.c_str(), flags, 0600);
        if (descriptor < 0) return false;
        struct stat info{};
        if (fstat(descriptor, &info) != 0 || !S_ISREG(info.st_mode) || flock(descriptor, LOCK_EX | LOCK_NB) != 0) {
            close(descriptor); descriptor = -1; return false;
        }
        locked = true;
        return true;
#endif
    }

    ~FileLock() { Release(); }
    void Release() noexcept {
        if (!locked) {
#ifdef _WIN32
            if (handle != INVALID_HANDLE_VALUE) { CloseHandle(handle); handle = INVALID_HANDLE_VALUE; }
#else
            if (descriptor >= 0) { close(descriptor); descriptor = -1; }
#endif
            return;
        }
#ifdef _WIN32
        UnlockFileEx(handle, 0, MAXDWORD, MAXDWORD, &overlapped);
        CloseHandle(handle);
        handle = INVALID_HANDLE_VALUE;
#else
        flock(descriptor, LOCK_UN);
        close(descriptor);
        descriptor = -1;
#endif
        locked = false;
    }
};

bool WriteAllAndFlush(const std::filesystem::path& temporary, std::string_view bytes) noexcept {
#ifdef _WIN32
    HANDLE file = CreateFileW(temporary.c_str(), GENERIC_WRITE, 0, nullptr, CREATE_NEW,
                              FILE_ATTRIBUTE_TEMPORARY | FILE_FLAG_WRITE_THROUGH, nullptr);
    if (file == INVALID_HANDLE_VALUE) return false;
    size_t offset = 0;
    bool ok = true;
    while (offset < bytes.size()) {
        const DWORD chunk = static_cast<DWORD>(std::min<size_t>(bytes.size() - offset, MAXDWORD));
        DWORD written = 0;
        if (!WriteFile(file, bytes.data() + offset, chunk, &written, nullptr) || written == 0) { ok = false; break; }
        offset += written;
    }
    if (ok && !FlushFileBuffers(file)) ok = false;
    if (!CloseHandle(file)) ok = false;
    if (!ok) DeleteFileW(temporary.c_str());
    return ok;
#else
    int flags = O_CREAT | O_EXCL | O_WRONLY;
#ifdef O_CLOEXEC
    flags |= O_CLOEXEC;
#endif
#ifdef O_NOFOLLOW
    flags |= O_NOFOLLOW;
#endif
    const int file = open(temporary.c_str(), flags, 0600);
    if (file < 0) return false;
    size_t offset = 0;
    bool ok = true;
    while (offset < bytes.size()) {
        const ssize_t written = write(file, bytes.data() + offset, bytes.size() - offset);
        if (written < 0 && errno == EINTR) continue;
        if (written <= 0) { ok = false; break; }
        offset += static_cast<size_t>(written);
    }
    if (ok && fsync(file) != 0) ok = false;
    if (close(file) != 0) ok = false;
    if (!ok) unlink(temporary.c_str());
    return ok;
#endif
}

bool ReplaceAtomically(const std::filesystem::path& temporary, const std::filesystem::path& target) noexcept {
#ifdef _WIN32
    // Inspect the final path component itself, not a possible reparse target. The parent
    // directory is host-controlled; rejecting a static reparse point here prevents replacing
    // through an existing target link. A hostile concurrent parent-directory mutation is outside
    // this best-effort snapshot's ownership model.
    HANDLE existing = CreateFileW(target.c_str(), FILE_READ_ATTRIBUTES,
                                  FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE,
                                  nullptr, OPEN_EXISTING,
                                  FILE_FLAG_OPEN_REPARSE_POINT | FILE_FLAG_BACKUP_SEMANTICS, nullptr);
    if (existing != INVALID_HANDLE_VALUE) {
        BY_HANDLE_FILE_INFORMATION info{};
        const bool inspected = GetFileInformationByHandle(existing, &info) != 0;
        CloseHandle(existing);
        if (!inspected || (info.dwFileAttributes & (FILE_ATTRIBUTE_DIRECTORY | FILE_ATTRIBUTE_REPARSE_POINT)))
            return false;
    } else {
        const DWORD error = GetLastError();
        if (error != ERROR_FILE_NOT_FOUND && error != ERROR_PATH_NOT_FOUND) return false;
    }
    return MoveFileExW(temporary.c_str(), target.c_str(), MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH) != 0;
#else
    if (rename(temporary.c_str(), target.c_str()) != 0) return false;
    const auto parent = target.parent_path().empty() ? std::filesystem::path(".") : target.parent_path();
    int flags = O_RDONLY;
#ifdef O_CLOEXEC
    flags |= O_CLOEXEC;
#endif
#ifdef O_DIRECTORY
    flags |= O_DIRECTORY;
#endif
    const int directory = open(parent.c_str(), flags);
    if (directory < 0) return false;
    const bool synced = fsync(directory) == 0;
    close(directory);
    return synced;
#endif
}

bool WriteAtomic(const std::filesystem::path& target, std::string_view sessionId, const Record& record) {
    std::string json;
    if (!Serialize(record, json) || json.size() > kMaxBytes) return false;
    std::filesystem::path temporary = target;
    temporary += "." + std::string(sessionId) + ".tmp";
    if (!WriteAllAndFlush(temporary, json)) return false;
    if (!ReplaceAtomically(temporary, target)) {
#ifdef _WIN32
        DeleteFileW(temporary.c_str());
#else
        unlink(temporary.c_str());
#endif
        return false;
    }
    return true;
}

} // namespace

Record StartingRecord(std::string sessionId, std::string startedAtUtc, bool pluginsEnabled) {
    Record record;
    record.state = RecordState::Starting;
    record.sessionId = std::move(sessionId);
    record.startedAtUtc = std::move(startedAtUtc);
    record.pluginsEnabledAtStart = pluginsEnabled;
    return record;
}

Record FailedRecord(const std::vector<plugins::Plugin>& list, bool pluginsEnabled,
                    std::string sessionId, std::string startedAtUtc, std::string completedAtUtc) {
    Record record;
    record.state = RecordState::Failed;
    record.sessionId = std::move(sessionId);
    record.startedAtUtc = std::move(startedAtUtc);
    record.completedAtUtc = std::move(completedAtUtc);
    record.pluginsEnabledAtStart = pluginsEnabled;
    record.pluginCount = list.size();
    record.loadedCount = LoadedCount(list);
    return record;
}

Record Project(const std::vector<plugins::Plugin>& list, bool pluginsEnabled,
               std::string sessionId, std::string startedAtUtc, std::string completedAtUtc) {
    if (list.size() > kMaxRows) {
        Record record;
        record.state = RecordState::Incomplete;
        record.sessionId = std::move(sessionId);
        record.startedAtUtc = std::move(startedAtUtc);
        record.completedAtUtc = std::move(completedAtUtc);
        record.pluginsEnabledAtStart = pluginsEnabled;
        record.pluginCount = list.size();
        record.loadedCount = LoadedCount(list);
        return record;
    }

    Record record;
    record.state = RecordState::Complete;
    record.sessionId = std::move(sessionId);
    record.startedAtUtc = std::move(startedAtUtc);
    record.completedAtUtc = std::move(completedAtUtc);
    record.pluginsEnabledAtStart = pluginsEnabled;
    record.pluginCount = list.size();
    record.loadedCount = LoadedCount(list);
    record.plugins.emplace();
    record.plugins->reserve(list.size());

    std::unordered_set<std::string> usedIds;
    for (const auto& plugin : list) {
        if (plugin.manifestOk && ValidId(plugin.manifest.id)) usedIds.insert(plugin.manifest.id);
    }
    for (size_t ordinal = 0; ordinal < list.size(); ++ordinal) {
        const plugins::Plugin& plugin = list[ordinal];
        const char* state = PluginStateName(plugin.state);
        const bool manifestValid = plugin.manifestOk && ValidId(plugin.manifest.id);
        const char* kind = manifestValid ? PluginKindName(plugin.manifest.kind) : "unknown";
        std::optional<std::string> version;
        if (manifestValid && ValidVersion(plugin.manifest.version)) version = plugin.manifest.version;
        if (!state || !kind || (manifestValid && !version)) {
            record.state = RecordState::Incomplete;
            record.plugins.reset();
            return record;
        }
        Row row;
        row.manifestValid = manifestValid;
        row.version = std::move(version);
        row.kind = kind;
        row.state = state;
        row.reasonCode = ReasonCode(plugin.state, manifestValid);
        row.restartRequired = plugin.state == plugins::State::Faulted;
        if (manifestValid) row.id = plugin.manifest.id;
        else {
            size_t alias = ordinal;
            do { row.id = "unknown_" + std::to_string(alias++); } while (usedIds.contains(row.id));
            usedIds.insert(row.id);
        }
        record.plugins->push_back(std::move(row));
    }
    std::string validation;
    if (!Serialize(record, validation)) {
        // The row/count limits make oversize output impossible with current fields; retain the
        // explicit incomplete fallback if a future schema field changes that assumption.
        record.state = RecordState::Incomplete;
        record.plugins.reset();
    }
    return record;
}

bool Serialize(const Record& record, std::string& json) {
    json.clear();
    if (!ValidRecord(record)) return false;
    const char* recordState = RecordStateName(record.state);
    std::string out;
    out.reserve(1024);
    if (!Append(out, "{\"schema_version\":1,\"record_state\":" ) || !AppendQuoted(out, recordState) ||
        !Append(out, ",\"session_id\":") || !AppendQuoted(out, record.sessionId) ||
        !Append(out, ",\"started_at_utc\":") || !AppendQuoted(out, record.startedAtUtc) ||
        !Append(out, ",\"completed_at_utc\":")) return false;
    if (record.completedAtUtc) {
        if (!AppendQuoted(out, *record.completedAtUtc)) return false;
    } else if (!Append(out, "null")) return false;
    if (!Append(out, ",\"plugins_enabled_at_start\":") || !AppendBool(out, record.pluginsEnabledAtStart) ||
        !Append(out, ",\"plugin_count\":") || !AppendCount(out, record.pluginCount) ||
        !Append(out, ",\"loaded_count\":") || !AppendCount(out, record.loadedCount) ||
        !Append(out, ",\"plugins\":")) return false;
    if (!record.plugins) {
        if (!Append(out, "null}")) return false;
    } else {
        if (!Append(out, "[")) return false;
        for (size_t i = 0; i < record.plugins->size(); ++i) {
            const Row& row = (*record.plugins)[i];
            if (i && !Append(out, ",")) return false;
            if (!Append(out, "{\"id\":") || !AppendQuoted(out, row.id) ||
                !Append(out, ",\"manifest_valid\":") || !AppendBool(out, row.manifestValid) ||
                !Append(out, ",\"version\":")) return false;
            if (row.version) {
                if (!AppendQuoted(out, *row.version)) return false;
            } else if (!Append(out, "null")) return false;
            if (!Append(out, ",\"kind\":") || !AppendQuoted(out, row.kind) ||
                !Append(out, ",\"state\":") || !AppendQuoted(out, row.state) ||
                !Append(out, ",\"reason_code\":")) return false;
            if (row.reasonCode) {
                if (!AppendQuoted(out, *row.reasonCode)) return false;
            } else if (!Append(out, "null")) return false;
            if (!Append(out, ",\"restart_required\":") || !AppendBool(out, row.restartRequired) ||
                !Append(out, "}")) return false;
        }
        if (!Append(out, "]}")) return false;
    }
    if (out.size() > kMaxBytes) return false;
    json = std::move(out);
    return true;
}

struct Writer::Impl {
    std::filesystem::path target;
    bool pluginsEnabled = false;
    std::string sessionId;
    std::string startedAtUtc;
    FileLock lock;
};

Writer::Writer() = default;
Writer::~Writer() { Reset(); }

bool Writer::Begin(const std::filesystem::path& target, bool pluginsEnabled) noexcept {
    Reset();
    try {
        if (target.empty() || target.filename() != "plugin-startup.json") return false;
        const auto session = NewSessionId();
        if (!session) return false;
        const auto parent = target.parent_path().empty() ? std::filesystem::path(".") : target.parent_path();
        std::error_code ec;
        std::filesystem::create_directories(parent, ec);
        if (ec || !std::filesystem::is_directory(parent, ec) || ec) return false;
        auto next = std::make_unique<Impl>();
        next->target = target;
        next->pluginsEnabled = pluginsEnabled;
        next->sessionId = *session;
        if (!next->lock.Acquire(parent / "plugin-startup.lock")) return false;
        RemoveAbandonedTemps(parent);
        const auto started = UtcNow();
        if (!started) return false;
        next->startedAtUtc = *started;
        Record starting = StartingRecord(next->sessionId, next->startedAtUtc, pluginsEnabled);
        if (!WriteAtomic(target, next->sessionId, starting)) return false;
        impl_ = std::move(next);
        return true;
    } catch (...) {
        return false;
    }
}

bool Writer::Complete(const std::vector<plugins::Plugin>& list) noexcept {
    if (!impl_) return false;
    try {
        const auto completed = UtcNow();
        if (!completed) { Reset(); return false; }
        Record record = Project(list, impl_->pluginsEnabled, impl_->sessionId, impl_->startedAtUtc, *completed);
        if (record.state == RecordState::Complete && !WriteAtomic(impl_->target, impl_->sessionId, record)) {
            Reset(); return false;
        }
        if (record.state == RecordState::Incomplete && !WriteAtomic(impl_->target, impl_->sessionId, record)) {
            Reset(); return false;
        }
        Reset();
        return true;
    } catch (...) {
        Reset();
        return false;
    }
}

bool Writer::Failed(const std::vector<plugins::Plugin>& list) noexcept {
    if (!impl_) return false;
    try {
        const auto completed = UtcNow();
        if (!completed) { Reset(); return false; }
        Record record = FailedRecord(list, impl_->pluginsEnabled, impl_->sessionId, impl_->startedAtUtc, *completed);
        const bool written = WriteAtomic(impl_->target, impl_->sessionId, record);
        Reset();
        return written;
    } catch (...) {
        Reset();
        return false;
    }
}

void Writer::Reset() noexcept { impl_.reset(); }
bool Writer::Active() const noexcept { return static_cast<bool>(impl_); }

} // namespace sco::app::detail::plugin_startup
