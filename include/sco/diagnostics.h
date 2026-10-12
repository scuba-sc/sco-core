#pragma once
// Host-only, game-neutral C++ copies. No plugin ABI export, IPC, MCP or authorization.
// Immutable host observations and default-off source publishers (#141). Endpoint policy is separate.
#include "sco/diagnostics_services.h"
#include "sco_diagnostics_publisher.h"
#include "sco/plugins.h"
#include <deque>
#include <memory>
#include <mutex>
#include <optional>
#include <string_view>

namespace sco::diagnostics {

constexpr size_t kInventoryLimit = 1024;
constexpr uint64_t kMaxMonotonicMs = 9007199254740991ULL;

struct PluginMetadata {
    std::string pluginId;
    std::optional<std::string> version;
    std::string kind, state;
    bool manifestValid = false;
    std::optional<std::string> reason;
    std::optional<uint64_t> loadGeneration;
    bool restartRequired = false;
};
struct ResolutionMetadata {
    std::string rowId, state;
    std::optional<uint32_t> matchCount;
    std::optional<std::string> reason;
    std::optional<uint64_t> resolutionGeneration;
    std::optional<std::string> gameBuild;
};
struct CapabilityMetadata {
    std::string id;
    std::optional<bool> enabled;
    std::optional<std::string> reason;
};
struct HostMetadata {
    std::optional<std::string> hostVersion, sdkVersion, gamePackVersion, hostBuild, sourceSha;
    uint32_t abiMajor = SCO_API_MAJOR, abiMinor = SCO_API_MINOR, abiTableSize = sizeof(sco_api);
    uint32_t diagnosticsVersion = 1;
    std::string readiness = "unknown";
    bool restartRequired = false;
};
struct LifecycleMetadata {
    std::string lifecycle = "unknown", offlineAdmission = "unknown";
    std::optional<uint64_t> lastTickMs;
    std::optional<bool> gameServicesRequested, gameServicesBuilt, gameServicesPublished;
    std::vector<CapabilityMetadata> capabilities;
    bool capabilitiesTruncated = false, restartRequired = false;
};
// Host-source options never authorize an endpoint. Positive offline admission and
// explicit source opt-ins are independent of plugin/game-service enablement.
struct SourceOptions {
    bool gamePack = false, diagnosticsPlugin = false, offlineAuthorized = false;
    std::string diagnosticsPluginId; // trusted host selection, never serialized
    std::optional<std::string> gameBuild; // actual host-observed build, not verified-build pin
};
// Metadata only, never game types. Extra source fields are NOT runtime JSON v1.
struct GamePackMetadata {
    std::optional<std::string> version, gameBuild;
    std::optional<uint64_t> observedMs = std::nullopt;
};
struct DiagnosticsPluginMetadata {
    bool available = false;
    sco_diagnostics_health health = SCO_DIAGNOSTICS_HEALTH_UNKNOWN; // untrusted advisory
    std::optional<uint64_t> observedMs = std::nullopt;
};
// Game thread; disabled options publish no service; selected live owner pushes
// copied fixed fields. Capture reads copies only and never invokes plugin code.
Result StartPublisher(const SourceOptions& options);
void StopPublisher(); // before shutdown callouts, clears retained source
std::optional<DiagnosticsPluginMetadata> CopyPublisherMetadata(uint64_t nowMs);
const sco_diagnostics_publisher_v1* PublisherTable(); // internal host/test seam
struct OptionalSources {
    std::optional<GamePackMetadata> gamePack;
    std::optional<DiagnosticsPluginMetadata> diagnosticsPlugin;
};
struct Snapshot {
    uint64_t generation = 0, observedMs = 0;
    HostMetadata host;
    LifecycleMetadata game;
    OptionalSources sources;
    std::vector<PluginMetadata> plugins;
    std::vector<ServiceMetadata> services;
    std::vector<ResolutionMetadata> resolutions;
    bool pluginsTruncated = false, servicesTruncated = false, resolutionsTruncated = false;
    uint64_t pluginRedactions = 0, serviceRedactions = 0, resolutionRedactions = 0;
};

enum class ListKind { Plugins, Services, Resolutions };
struct Page {
    std::string status = "unavailable";
    std::optional<std::string> error, nextCursor;
    std::shared_ptr<const Snapshot> snapshot;
    std::vector<size_t> indices; // local indices into the immutable copy, never peer-decoded
    bool truncated = false;
    uint64_t redactedCount = 0;
};

class SnapshotStore {
public:
    // Writer game-thread only. list must outlive construction and retain its size/order;
    // mutations on that same thread are allowed between ticks (an interval observation).
    // No callbacks, table reads, SDK methods or resolution runs. Sources/labels are host facts.
    bool Begin(const std::vector<plugins::Plugin>& list, HostMetadata host,
               LifecycleMetadata game, uint64_t nowMs, OptionalSources sources = {});
    // <=4 work units and a soft 1ms budget/call; one registry copy is a work unit.
    // Returns true only when complete (or no work); over-budget keeps last completed copy.
    bool Advance(uint64_t nowMs);
    void Reset(); // game thread: invalidate tokens/generations before teardown or new session
    std::shared_ptr<const Snapshot> Latest() const; // any thread; copies only
    Page ReadPage(ListKind tool, std::string_view filter, std::string_view cursor,
                  size_t limit, uint64_t nowMs); // any thread; 64 tokens, 60s, two generations
private:
    struct Token {
        std::string value, filter;
        ListKind tool;
        uint64_t generation, expires;
        size_t next;
    };
    mutable std::mutex lock_;
    std::deque<std::shared_ptr<const Snapshot>> completed_;
    std::vector<Token> tokens_;
    std::shared_ptr<Snapshot> pending_;
    const std::vector<plugins::Plugin>* list_ = nullptr; // writer-only
    size_t pluginCount_ = 0, rowCount_ = 0, position_ = 0, phase_ = 0;
    uint64_t generation_ = 0, token_ = 0, lastBegin_ = 0;
    bool haveBegin_ = false, exhausted_ = false;
};

enum class HostEvent { Off, Disabled, Refused, Ready, Loaded, Crashed, Unloaded, Faulted };
enum class LogLevel { Info, Warning, Error };
struct LogEntry {
    uint64_t sequence, monotonicMs;
    std::string pluginId, level, origin, message;
    bool untrustedContent = true;
};
struct LogTail {
    std::optional<std::string> error;
    std::vector<LogEntry> entries;
    bool hasMore = false, freeFormEnabled = false;
    std::optional<uint64_t> nextSequence, firstAvailableSequence;
    uint64_t droppedCount = 0, redactedCount = 0, captureStartedMs = 0;
};
class Capture {
public:
    // Internal host/test seam only; future policy must establish consent before Start.
    // Not installed in any product by step 1. No file sink/history/tail or external channel.
    bool Start(uint64_t nowMs, bool freeFormConsent = false);
    void Stop();
    bool AssignKey(const void* key, std::string_view hostAssignedId); // <=1024 keys
    void RecordHost(const void* key, HostEvent event, uint64_t nowMs);
    void RecordPlugin(const void* key, LogLevel level, std::string_view text, uint64_t nowMs);
    LogTail Tail(std::string_view id, uint64_t afterSequence, size_t limit) const;
private:
    struct Key { const void* key; std::string id; };
    struct Record { uint64_t sequence, ms; std::string id, level, origin; std::optional<std::string> text; };
    void Append(const void* key, LogLevel level, const char* origin,
                std::optional<std::string> text, uint64_t nowMs);
    mutable std::mutex lock_;
    std::vector<Key> keys_;
    std::deque<Record> records_;
    uint64_t sequence_ = 0, started_ = 0, dropped_ = 0, sessionFloor_ = 0;
    size_t bytes_ = 0;
    bool active_ = false, freeForm_ = false, exhausted_ = false;
};

} // namespace sco::diagnostics
