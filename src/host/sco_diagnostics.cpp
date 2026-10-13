// Host-only immutable observations; never an SDK consumer or a service-table probe.
#include "sco/diagnostics.h"
#include "sco/caps.h"
#include "sco/signatures.h"
#include <algorithm>
#include <chrono>
#include <limits>
#include <atomic>
#ifdef _WIN32
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#endif

namespace sco::diagnostics {
namespace { std::atomic<Capture*> logCapture{nullptr}; }
void SetLogCapture(Capture* capture){logCapture.store(capture,std::memory_order_release);}
void CapturePluginLog(const void* key,LogLevel level,std::string_view text){
    auto* capture=logCapture.load(std::memory_order_acquire);if(!capture)return;
#ifdef _WIN32
    const uint64_t now=GetTickCount64();
#else
    const auto now=uint64_t(std::chrono::duration_cast<std::chrono::milliseconds>(
        std::chrono::steady_clock::now().time_since_epoch()).count());
#endif
    capture->RecordPlugin(key,level,text,now);
}
namespace {
bool Id(std::string_view s, size_t max, bool dotted = false) {
    if (s.empty() || s.size() > max) return false;
    char previous = '.';
    for (char c : s) {
        if (c == '.' && dotted) { if (previous == '.') return false; }
        else if (!((c >= 'a' && c <= 'z') || (c >= '0' && c <= '9') || c == '_')) return false;
        previous = c;
    }
    return previous != '.';
}
std::optional<std::string> Label(const std::optional<std::string>& s, size_t max) {
    if (!s || s->empty() || s->size() > max) return {};
    // Deliberately narrow: no prose, personal identifiers, paths, addresses or controls.
    if (s->find("0x") != std::string::npos || s->find("sk-") != std::string::npos ||
        s->find("ghp_") != std::string::npos || s->find("github_pat_") != std::string::npos) return {};
    for (char c : *s)
        if (!((c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9') ||
              c == '.' || c == '_' || c == '-' || c == '+')) return {};
    return s;
}
const char* State(plugins::State s) {
    switch (s) {
    case plugins::State::Off: return "off";
    case plugins::State::Disabled: return "disabled";
    case plugins::State::Refused: return "refused";
    case plugins::State::Ready: return "ready";
    case plugins::State::Loaded: return "loaded";
    case plugins::State::Crashed: return "crashed";
    case plugins::State::Unloaded: return "unloaded";
    case plugins::State::Faulted: return "faulted";
    }
    return "unknown";
}
const char* Kind(plugins::Kind k) {
    switch (k) {
    case plugins::Kind::Builtin: return "builtin";
    case plugins::Kind::Native: return "native";
    case plugins::Kind::Lua: return "lua";
    case plugins::Kind::Data: return "data";
    }
    return "unknown";
}
const char* Resolution(SigState s) {
    switch (s) {
    case SigState::NotRun: return "not_run";
    case SigState::Ok: return "ok";
    case SigState::Missing: return "missing";
    case SigState::Ambiguous: return "ambiguous";
    case SigState::Failed: return "failed";
    case SigState::Blocked: return "blocked";
    }
    return "unknown";
}
std::optional<std::string> PluginReason(plugins::State s, bool valid) {
    if (!valid) return "invalid_manifest";
    switch (s) {
    case plugins::State::Off: return "plugins_off";
    case plugins::State::Disabled: return "plugin_disabled";
    case plugins::State::Refused: return "plugin_refused";
    case plugins::State::Crashed: return "plugin_crashed";
    case plugins::State::Faulted: return "provider_fault_restart_required";
    default: return {};
    }
}
}

bool SnapshotStore::Begin(const std::vector<plugins::Plugin>& list, HostMetadata host,
                          LifecycleMetadata game, uint64_t now, OptionalSources sources) {
    if (!OnGameThread() || pending_ || exhausted_ || now > kMaxMonotonicMs ||
        (haveBegin_ && (now < lastBegin_ || now - lastBegin_ < 1000))) return false;
    if (generation_ == (std::numeric_limits<uint64_t>::max)()) { exhausted_ = true; return false; }
    auto next = std::make_shared<Snapshot>();
    host.hostVersion = Label(host.hostVersion, 128);
    host.sdkVersion = Label(host.sdkVersion, 128);
    host.gamePackVersion = Label(host.gamePackVersion, 128);
    host.hostBuild = Label(host.hostBuild, 128);
    if (host.sourceSha && (host.sourceSha->size() != 40 ||
        host.sourceSha->find_first_not_of("0123456789abcdef") != std::string::npos)) host.sourceSha.reset();
    if (host.readiness != "starting" && host.readiness != "ready" && host.readiness != "stopping") host.readiness = "unknown";
    if (game.lifecycle != "starting" && game.lifecycle != "ready" && game.lifecycle != "stopping") game.lifecycle = "unknown";
    if (game.offlineAdmission != "authorized" && game.offlineAdmission != "denied") game.offlineAdmission = "unknown";
    if (game.lastTickMs && (*game.lastTickMs > now || *game.lastTickMs > kMaxMonotonicMs)) game.lastTickMs.reset();
    // Capability observations come from the host registry, not caller-provided prose.
    game.capabilities.clear();
    if (sources.gamePack) {
        sources.gamePack->version = Label(sources.gamePack->version, 128);
        sources.gamePack->gameBuild = Label(sources.gamePack->gameBuild, 128);
        if (sources.gamePack->observedMs && *sources.gamePack->observedMs > now) sources.gamePack.reset();
        else host.gamePackVersion = sources.gamePack->version;
    }
    if (sources.diagnosticsPlugin) {
        const auto& source = *sources.diagnosticsPlugin;
        if (!source.available || (source.observedMs && *source.observedMs > now) ||
            (source.health != SCO_DIAGNOSTICS_HEALTH_UNKNOWN && source.health != SCO_DIAGNOSTICS_HEALTH_READY &&
             source.health != SCO_DIAGNOSTICS_HEALTH_DEGRADED)) sources.diagnosticsPlugin.reset();
    }
    next->host = std::move(host); next->game = std::move(game); next->sources = std::move(sources);
    next->generation = generation_ + 1;
    pluginCount_ = std::min(list.size(), kInventoryLimit);
    rowCount_ = std::min(SignatureCount(), kInventoryLimit);
    next->pluginsTruncated = list.size() > pluginCount_;
    next->resolutionsTruncated = SignatureCount() > rowCount_;
    list_ = &list; pending_ = std::move(next); phase_ = position_ = 0;
    lastBegin_ = now; haveBegin_ = true;
    return true;
}

bool SnapshotStore::Advance(uint64_t now) {
    if (!OnGameThread()) return false;
    if (!pending_) return true;
    if (now > kMaxMonotonicMs || now < lastBegin_ || list_->size() < pluginCount_) {
        pending_.reset(); list_ = nullptr; return false;
    }
    const auto start = std::chrono::steady_clock::now();
    for (size_t unit = 0; unit < 4; ++unit) {
        if (std::chrono::steady_clock::now() - start >= std::chrono::milliseconds(1)) break;
        if (phase_ == 0) {
            if (position_ == pluginCount_) { phase_ = 1; position_ = 0; continue; }
            const size_t ordinal = position_++;
            const auto& p = (*list_)[ordinal];
            PluginMetadata out;
            out.manifestValid = p.manifestOk && Id(p.manifest.id, 31);
            // Safe aliases are deterministic for this stable host list; skip all real IDs.
            if (out.manifestValid) out.pluginId = p.manifest.id;
            else {
                size_t alias = ordinal;
                for (;;) {
                    out.pluginId = "unknown_" + std::to_string(alias);
                    bool collision = false;
                    for (size_t i = 0; i < pluginCount_; ++i)
                        if ((*list_)[i].manifestOk && (*list_)[i].manifest.id == out.pluginId) collision = true;
                    if (!collision) break;
                    alias += kInventoryLimit;
                }
                ++pending_->pluginRedactions;
            }
            out.version = out.manifestValid ? Label(p.manifest.version, 128) : std::nullopt;
            if (out.manifestValid && !out.version && !p.manifest.version.empty()) ++pending_->pluginRedactions;
            out.kind = out.manifestValid ? Kind(p.manifest.kind) : "unknown";
            out.state = State(p.state);
            out.reason = PluginReason(p.state, out.manifestValid);
            if (!p.reason.empty()) ++pending_->pluginRedactions; // raw reason withheld, even if benign
            out.restartRequired = p.state == plugins::State::Faulted;
            // The loader exposes no load-generation counter: keep it unknown, not loadOrder.
            pending_->host.restartRequired |= out.restartRequired;
            pending_->game.restartRequired |= out.restartRequired;
            pending_->plugins.push_back(std::move(out));
        } else if (phase_ == 1) {
            auto inventory = CopyServiceMetadata(); // lock order: no snapshot/capture lock held
            pending_->services = std::move(inventory.items);
            pending_->servicesTruncated = inventory.truncated;
            phase_ = 2;
        } else if (phase_ == 2) {
            if (position_ == rowCount_) { phase_ = 3; position_ = 0; continue; }
            const size_t i = position_++;
            const auto* def = SignatureDef(i);
            const auto& result = SignatureResult(i); // existing result only; no address/pattern copy
            if (!def || !def->id || !Id(def->id, 128, true)) { ++pending_->resolutionRedactions; continue; }
            ResolutionMetadata out;
            out.rowId = def->id; out.state = Resolution(result.state);
            if (result.state != SigState::NotRun && result.matches >= 0) out.matchCount = static_cast<uint32_t>(result.matches);
            // Snapshot generations do not certify a signature-resolution generation.
            if (result.why) { ++pending_->resolutionRedactions; out.reason = "resolution_failed"; }
            if (pending_->sources.gamePack) out.gameBuild = pending_->sources.gamePack->gameBuild;
            pending_->resolutions.push_back(std::move(out));
        } else if (phase_ == 3) {
            caps::Entry entries[50]{};
            const size_t count = caps::List(entries, 50);
            pending_->game.capabilitiesTruncated = count > 50;
            for (size_t i = 0; i < std::min(count, size_t{50}); ++i) {
                if (!Id(entries[i].name, 128, true)) continue;
                pending_->game.capabilities.push_back({ entries[i].name, entries[i].ready,
                    entries[i].ready ? std::nullopt : std::optional<std::string>{"capability_disabled"} });
            }
            phase_ = 4;
        } else {
            std::stable_sort(pending_->plugins.begin(), pending_->plugins.end(), [](const auto& a, const auto& b) { return a.pluginId < b.pluginId; });
            std::stable_sort(pending_->services.begin(), pending_->services.end(), [](const auto& a, const auto& b) { return a.service < b.service; });
            std::stable_sort(pending_->resolutions.begin(), pending_->resolutions.end(), [](const auto& a, const auto& b) { return a.rowId < b.rowId; });
            pending_->observedMs = lastBegin_; // conservative freshness for a chunked observation
            std::lock_guard<std::mutex> hold(lock_);
            completed_.push_back(pending_);
            if (completed_.size() > 2) completed_.pop_front();
            generation_ = pending_->generation;
            pending_.reset(); list_ = nullptr;
            return true;
        }
    }
    return false;
}

void SnapshotStore::Reset() {
    if (!OnGameThread()) return;
    pending_.reset(); list_ = nullptr; haveBegin_ = false;
    std::lock_guard<std::mutex> hold(lock_);
    completed_.clear(); tokens_.clear(); // counters never reused, even across resets
}
std::shared_ptr<const Snapshot> SnapshotStore::Latest() const {
    std::lock_guard<std::mutex> hold(lock_);
    return completed_.empty() ? nullptr : completed_.back();
}

Page SnapshotStore::ReadPage(ListKind tool, std::string_view filter, std::string_view cursor,
                             size_t limit, uint64_t now) {
    Page page;
    if (!limit || limit > 50 || now > kMaxMonotonicMs || filter.size() > 128 || cursor.size() > 128 ||
        (tool != ListKind::Plugins && tool != ListKind::Services && tool != ListKind::Resolutions)) {
        page.error = "invalid_request"; return page;
    }
    if (tool == ListKind::Plugins && !filter.empty() && filter != "off" && filter != "disabled" &&
        filter != "refused" && filter != "ready" && filter != "loaded" && filter != "crashed" &&
        filter != "unloaded" && filter != "faulted" && filter != "unknown") {
        page.error = "invalid_request"; return page;
    }
    if (tool != ListKind::Plugins && !filter.empty() && !Id(filter, tool == ListKind::Services ? 63 : 128, true)) {
        page.error = "invalid_request"; return page;
    }
    std::lock_guard<std::mutex> hold(lock_);
    if (completed_.empty()) { page.error = cursor.empty() ? "unknown_metadata" : "snapshot_changed"; return page; }
    size_t next = 0;
    auto snapshot = completed_.back();
    if (!cursor.empty()) {
        const auto token = std::find_if(tokens_.begin(), tokens_.end(), [&](const Token& t) { return t.value == cursor; });
        if (token == tokens_.end() || now >= token->expires) { page.error = "snapshot_changed"; return page; }
        if (token->tool != tool || token->filter != filter) { page.error = "invalid_request"; return page; }
        const auto found = std::find_if(completed_.begin(), completed_.end(), [&](const auto& s) { return s->generation == token->generation; });
        if (found == completed_.end()) { page.error = "snapshot_changed"; return page; }
        snapshot = *found; next = token->next;
    }
    if (now < snapshot->observedMs) { page.error = "invalid_request"; return page; }
    const size_t count = tool == ListKind::Plugins ? snapshot->plugins.size() :
                         tool == ListKind::Services ? snapshot->services.size() : snapshot->resolutions.size();
    std::vector<size_t> matches;
    for (size_t i = 0; i < count; ++i) {
        const auto& id = tool == ListKind::Plugins ? snapshot->plugins[i].state :
                         tool == ListKind::Services ? snapshot->services[i].service : snapshot->resolutions[i].rowId;
        if (filter.empty() || id == filter) matches.push_back(i);
    }
    if (!filter.empty() && tool != ListKind::Plugins && matches.empty()) { page.error = "not_found"; return page; }
    const size_t end = std::min(matches.size(), next + limit);
    if (next > matches.size()) { page.error = "invalid_request"; return page; }
    page.indices.assign(matches.begin() + static_cast<std::ptrdiff_t>(next), matches.begin() + static_cast<std::ptrdiff_t>(end));
    if (end < matches.size()) {
        tokens_.erase(std::remove_if(tokens_.begin(), tokens_.end(), [&](const Token& t) { return now >= t.expires; }), tokens_.end());
        if (tokens_.size() == 64 || token_ == (std::numeric_limits<uint64_t>::max)()) { page.indices.clear(); page.error = "snapshot_busy"; return page; }
        std::string value = "c_" + std::to_string(++token_);
        tokens_.push_back({value, std::string(filter), tool, snapshot->generation, now + 60000, end});
        page.nextCursor = std::move(value);
    }
    page.truncated = tool == ListKind::Plugins ? snapshot->pluginsTruncated : tool == ListKind::Services ? snapshot->servicesTruncated : snapshot->resolutionsTruncated;
    page.redactedCount = tool == ListKind::Plugins ? snapshot->pluginRedactions : tool == ListKind::Services ? snapshot->serviceRedactions : snapshot->resolutionRedactions;
    page.status = now - snapshot->observedMs > 1000 ? "stale" : snapshot->host.restartRequired ? "restart_required" : "ok";
    page.snapshot = std::move(snapshot);
    return page;
}

bool Capture::Start(uint64_t now, bool consent) {
    std::lock_guard<std::mutex> hold(lock_);
    if (active_ || exhausted_ || now > kMaxMonotonicMs) return false;
    keys_.clear(); records_.clear(); bytes_ = 0; dropped_ = 0; started_ = now;
    freeForm_ = consent; active_ = true; sessionFloor_ = sequence_;
    return true;
}
void Capture::Stop() {
    std::lock_guard<std::mutex> hold(lock_);
    active_ = false; keys_.clear(); records_.clear(); bytes_ = 0;
}
bool Capture::AssignKey(const void* key, std::string_view id) {
    if (!key || !Id(id, 31)) return false;
    std::lock_guard<std::mutex> hold(lock_);
    if (!active_) return false;
    for (const auto& known : keys_) if (known.key == key) return known.id == id; // never relabel an owner
    if (keys_.size() == kInventoryLimit) return false;
    keys_.push_back({key, std::string(id)}); return true;
}
void Capture::Append(const void* key, LogLevel level, const char* origin,
                     std::optional<std::string> text, uint64_t now) {
    // Caller holds capture lock. No callbacks, filesystem access, formatting from plugin prefixes.
    if (!active_ || now < started_ || now > kMaxMonotonicMs) return;
    const auto known = std::find_if(keys_.begin(), keys_.end(), [&](const auto& k) { return k.key == key; });
    if (known == keys_.end()) return;
    if (sequence_ == (std::numeric_limits<uint64_t>::max)()) { active_ = false; exhausted_ = true; records_.clear(); return; }
    const char* name = level == LogLevel::Warning ? "warning" : level == LogLevel::Error ? "error" : "info";
    records_.push_back({ ++sequence_, now, known->id, name, origin, std::move(text) });
    bytes_ += sizeof(Record) + 64 + records_.back().id.size() + (records_.back().text ? records_.back().text->size() : 0);
    while (bytes_ > 1024 * 1024 - kInventoryLimit * (sizeof(Key) + 64)) {
        bytes_ -= sizeof(Record) + 64 + records_.front().id.size() + (records_.front().text ? records_.front().text->size() : 0);
        records_.pop_front(); ++dropped_;
    }
}
void Capture::RecordHost(const void* key, HostEvent event, uint64_t now) {
    static const char* const events[]{"plugin_off", "plugin_disabled", "plugin_refused", "plugin_ready",
        "plugin_loaded", "plugin_crashed", "plugin_unloaded", "provider_fault_restart_required"};
    const auto index = static_cast<size_t>(event);
    if (index >= std::size(events)) return;
    std::lock_guard<std::mutex> hold(lock_);
    Append(key, event == HostEvent::Faulted || event == HostEvent::Crashed ? LogLevel::Error : LogLevel::Info, "host", std::string(events[index]), now);
}
void Capture::RecordPlugin(const void* key, LogLevel level, std::string_view text, uint64_t now) {
    std::lock_guard<std::mutex> hold(lock_);
    // v1 conservatively accepts only fixed benign tokens. Even consent never licenses uncertain
    // prose, paths, roots, credentials or instructions. Everything else is a counted withheld row.
    std::optional<std::string> safe;
    if (freeForm_ && (text == "ready" || text == "loaded" || text == "stopped")) safe = std::string(text);
    Append(key, level, "plugin", std::move(safe), now);
}
LogTail Capture::Tail(std::string_view id, uint64_t after, size_t limit) const {
    LogTail out;
    std::lock_guard<std::mutex> hold(lock_);
    if (!active_) { out.error = "runtime_disabled"; return out; }
    if (!Id(id, 31) || !limit || limit > 100 || after > sequence_) { out.error = "invalid_request"; return out; }
    if (after && after <= sessionFloor_) { out.error = "session_changed"; return out; }
    if (std::none_of(keys_.begin(), keys_.end(), [&](const Key& k) { return k.id == id; })) {
        out.error = "not_found"; return out;
    }
    out.freeFormEnabled = freeForm_; out.captureStartedMs = started_; out.droppedCount = dropped_;
    if (!records_.empty()) out.firstAvailableSequence = records_.front().sequence;
    size_t payloadBytes = 0;
    for (const auto& row : records_) {
        if (row.sequence <= after) continue;
        if (row.id != id) { out.nextSequence = row.sequence; continue; }
        const size_t cost = 256 + row.id.size() + (row.text ? row.text->size() : 0);
        if (row.text && (out.entries.size() == limit || payloadBytes + cost > 32768)) { out.hasMore = true; break; }
        out.nextSequence = row.sequence; // includes withheld/nonmatching considered records
        if (!row.text) { ++out.redactedCount; continue; }
        payloadBytes += cost;
        out.entries.push_back({row.sequence, row.ms, row.id, row.level, row.origin, *row.text, true});
    }
    return out;
}
} // namespace sco::diagnostics
