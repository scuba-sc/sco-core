#pragma once

// Internal, game-neutral file snapshot of one completed plugin startup pass.
// This is a historical UI hint, not a live or authenticated host channel.
#include "sco/plugins.h"
#include <filesystem>
#include <memory>
#include <optional>
#include <string>
#include <vector>

namespace sco::app::detail::plugin_startup {

constexpr size_t kMaxBytes = 65536;
constexpr size_t kMaxRows = 256;

enum class RecordState { Complete, Starting, Failed, Incomplete };

struct Row {
    std::string id;
    bool manifestValid = false;
    std::optional<std::string> version;
    std::string kind;
    std::string state;
    std::optional<std::string> reasonCode;
    bool restartRequired = false;
};

struct Record {
    RecordState state = RecordState::Starting;
    std::string sessionId;
    std::string startedAtUtc;
    std::optional<std::string> completedAtUtc;
    bool pluginsEnabledAtStart = false;
    size_t pluginCount = 0;
    size_t loadedCount = 0;
    // null while starting, failed or incomplete; present only for a complete projection.
    std::optional<std::vector<Row>> plugins;
};

// These functions are also used by the synthetic contract tests. Project never includes plugin
// prose, names, paths, authors, or addresses. A core row that cannot be represented exactly makes
// the whole record incomplete instead of silently truncating a field.
Record StartingRecord(std::string sessionId, std::string startedAtUtc, bool pluginsEnabled);
Record FailedRecord(const std::vector<plugins::Plugin>& list, bool pluginsEnabled,
                    std::string sessionId, std::string startedAtUtc, std::string completedAtUtc);
Record Project(const std::vector<plugins::Plugin>& list, bool pluginsEnabled,
               std::string sessionId, std::string startedAtUtc, std::string completedAtUtc);
bool Serialize(const Record& record, std::string& json);

class Writer {
public:
    Writer();
    ~Writer();
    Writer(const Writer&) = delete;
    Writer& operator=(const Writer&) = delete;

    // Acquires a process-shared exclusive file lock and publishes `starting`. Failure is advisory:
    // callers must continue startup without trying to infer status from an older file.
    bool Begin(const std::filesystem::path& target, bool pluginsEnabled) noexcept;
    // Publishes complete, or incomplete if the final core list does not fit the reader contract.
    bool Complete(const std::vector<plugins::Plugin>& list) noexcept;
    // Used only when the host catches a C++ exception during the load pass. Process crashes leave
    // the already published starting record, never a synthetic running/complete status.
    bool Failed(const std::vector<plugins::Plugin>& list) noexcept;
    void Reset() noexcept;
    bool Active() const noexcept;

private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};

} // namespace sco::app::detail::plugin_startup
