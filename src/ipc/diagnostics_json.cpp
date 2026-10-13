// SPDX-License-Identifier: GPL-3.0-only
#include "diagnostics_json.h"
#include <algorithm>
#include <initializer_list>
#include <limits>

namespace sco::diagnostics::json {
namespace {
constexpr size_t kRequestBytes = 3992;
bool OneOf(std::string_view value, std::initializer_list<std::string_view> choices) {
    return std::find(choices.begin(), choices.end(), value) != choices.end();
}
bool Utf8(std::string_view s) {
    for (size_t i = 0; i < s.size();) {
        const auto first = static_cast<unsigned char>(s[i++]);
        if (first < 0x80) continue;
        unsigned count; uint32_t value, minimum;
        if (first >= 0xc2 && first <= 0xdf) { count = 1; value = first & 31; minimum = 0x80; }
        else if (first >= 0xe0 && first <= 0xef) { count = 2; value = first & 15; minimum = 0x800; }
        else if (first >= 0xf0 && first <= 0xf4) { count = 3; value = first & 7; minimum = 0x10000; }
        else return false;
        if (s.size() - i < count) return false;
        while (count--) {
            auto c = static_cast<unsigned char>(s[i++]);
            if ((c & 0xc0) != 0x80) return false;
            value = (value << 6) | (c & 63);
        }
        if (value < minimum || value > 0x10ffff || (value >= 0xd800 && value <= 0xdfff)) return false;
    }
    return true;
}
bool Hex(std::string_view s, size_t length) {
    return s.size() == length && s.find_first_not_of("0123456789abcdef") == std::string_view::npos;
}
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
bool Label(std::string_view s, size_t max) {
    // Same fail-closed label policy as the host snapshot publisher, not prose redaction.
    if (s.empty() || s.size() > max || s.find("0x") != s.npos || s.find("sk-") != s.npos ||
        s.find("ghp_") != s.npos || s.find("github_pat_") != s.npos) return false;
    return s.find_first_not_of("abcdefghijklmnopqrstuvwxyzABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789._-+") == s.npos;
}
bool Cursor(std::string_view s) {
    return !s.empty() && s.size() <= 128 &&
        s.find_first_not_of("abcdefghijklmnopqrstuvwxyzABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789_-") == s.npos;
}
bool Decimal(std::string_view s, uint64_t& result) {
    if (s.empty() || s.size() > 20) return false;
    result = 0;
    for (char c : s) {
        if (c < '0' || c > '9') return false;
        const uint64_t digit = static_cast<uint64_t>(c - '0');
        if (result > (std::numeric_limits<uint64_t>::max() - digit) / 10) return false;
        result = result * 10 + digit;
    }
    return true;
}
bool PluginState(std::string_view s) {
    return OneOf(s, {"off", "disabled", "refused", "ready", "loaded", "crashed", "unloaded", "faulted", "unknown"});
}
class Parser {
public:
    explicit Parser(std::string_view text) : text_(text) {}
    bool Run(Object& out) {
        Space(); if (!Take('{')) return false; Space();
        if (Take('}')) { Space(); return position_ == text_.size(); }
        std::set<std::string> keys;
        for (;;) {
            std::string key, value;
            if (!String(key) || !keys.insert(key).second) return false;
            Space(); if (!Take(':')) return false; Space();
            if (position_ == text_.size()) return false;
            if (text_[position_] == '"') {
                if (!String(value)) return false;
                out.strings.emplace(std::move(key), std::move(value));
            } else if (text_.substr(position_, 4) == "null") {
                position_ += 4; out.nulls.insert(std::move(key));
            } else {
                const size_t begin = position_;
                while (position_ < text_.size() && text_[position_] >= '0' && text_[position_] <= '9') ++position_;
                uint64_t number;
                const auto token = text_.substr(begin, position_ - begin);
                if (!Decimal(token, number) || (token.size() > 1 && token.front() == '0')) return false;
                out.integers.emplace(std::move(key), number);
            }
            Space();
            if (Take('}')) { Space(); return position_ == text_.size(); }
            if (!Take(',')) return false;
            Space();
        }
    }
private:
    void Space() { while (position_ < text_.size() && OneOf(text_.substr(position_, 1), {" ", "\t", "\r", "\n"})) ++position_; }
    bool Take(char c) { if (position_ == text_.size() || text_[position_] != c) return false; ++position_; return true; }
    bool Quad(uint32_t& value) {
        if (text_.size() - position_ < 4) return false;
        value = 0;
        for (unsigned i = 0; i < 4; ++i) {
            char c = text_[position_++]; unsigned digit;
            if (c >= '0' && c <= '9') digit = c - '0';
            else if (c >= 'a' && c <= 'f') digit = c - 'a' + 10;
            else if (c >= 'A' && c <= 'F') digit = c - 'A' + 10;
            else return false;
            value = (value << 4) | digit;
        }
        return true;
    }
    static void Encode(uint32_t value, std::string& out) {
        if (value < 0x80) out += static_cast<char>(value);
        else if (value < 0x800) { out += static_cast<char>(0xc0 | (value >> 6)); out += static_cast<char>(0x80 | (value & 63)); }
        else if (value < 0x10000) { out += static_cast<char>(0xe0 | (value >> 12)); out += static_cast<char>(0x80 | ((value >> 6) & 63)); out += static_cast<char>(0x80 | (value & 63)); }
        else { out += static_cast<char>(0xf0 | (value >> 18)); out += static_cast<char>(0x80 | ((value >> 12) & 63)); out += static_cast<char>(0x80 | ((value >> 6) & 63)); out += static_cast<char>(0x80 | (value & 63)); }
    }
    bool String(std::string& out) {
        if (!Take('"')) return false;
        while (position_ < text_.size()) {
            const auto c = static_cast<unsigned char>(text_[position_++]);
            if (c == '"') return true;
            if (c < 0x20) return false;
            if (c != '\\') { out += static_cast<char>(c); continue; }
            if (position_ == text_.size()) return false;
            switch (text_[position_++]) {
            case '"': out += '"'; break;
            case '\\': out += '\\'; break;
            case '/': out += '/'; break;
            case 'b': out += '\b'; break;
            case 'f': out += '\f'; break;
            case 'n': out += '\n'; break;
            case 'r': out += '\r'; break;
            case 't': out += '\t'; break;
            case 'u': {
                uint32_t value; if (!Quad(value)) return false;
                if (value >= 0xd800 && value <= 0xdbff) {
                    uint32_t low;
                    if (!Take('\\') || !Take('u') || !Quad(low) || low < 0xdc00 || low > 0xdfff) return false;
                    value = 0x10000 + ((value - 0xd800) << 10) + low - 0xdc00;
                } else if (value >= 0xdc00 && value <= 0xdfff) return false;
                Encode(value, out); break;
            }
            default: return false;
            }
        }
        return false;
    }
    std::string_view text_;
    size_t position_ = 0;
};
bool Closed(const Object& o, std::initializer_list<std::string_view> allowed) {
    if (o.strings.size() + o.integers.size() + o.nulls.size() > allowed.size()) return false;
    for (const auto& entry : o.strings) {
        if (!OneOf(entry.first, allowed) || !Utf8(entry.first) || !Utf8(entry.second) ||
            o.integers.count(entry.first) || o.nulls.count(entry.first)) return false;
    }
    for (const auto& entry : o.integers) if (!OneOf(entry.first, allowed) || o.nulls.count(entry.first)) return false;
    for (const auto& key : o.nulls) if (!OneOf(key, allowed)) return false;
    return true;
}
const std::string* String(const Object& o, const char* key) {
    const auto it = o.strings.find(key); return it == o.strings.end() ? nullptr : &it->second;
}
bool Present(const Object& o, const char* key) { return o.strings.count(key) || o.integers.count(key) || o.nulls.count(key); }
bool Build(const Object& o, const char* key) {
    if (o.nulls.count(key)) return true;
    const auto* s = String(o, key); return s && Label(*s, 128);
}
bool Nonce(const Object& o, const char* key) { const auto* s = String(o, key); return s && Hex(*s, 64); }
}
bool Parse(std::string_view input, Object& out) {
    out = {};
    if (input.size() > kRequestBytes || !Utf8(input)) return false;
    try {
        Object parsed;
        if (!Parser(input).Run(parsed)) return false;
        out = std::move(parsed); return true;
    } catch (...) { out = {}; return false; }
}
bool Control(uint16_t opcode, const Object& o) {
    switch (opcode) {
    case 1: return Closed(o, {"client_nonce", "expected_host_build"}) && Nonce(o, "client_nonce") && Build(o, "expected_host_build");
    case 2: return Closed(o, {"client_nonce", "host_nonce", "host_build"}) && Nonce(o, "client_nonce") && Nonce(o, "host_nonce") && Build(o, "host_build");
    case 3: case 4: return Closed(o, {"client_nonce", "host_nonce"}) && Nonce(o, "client_nonce") && Nonce(o, "host_nonce");
    case 5: { const auto* s = String(o, "reason"); return Closed(o, {"reason"}) && s && OneOf(*s, {"stopping", "revoked", "deadline", "corrupt", "client_exit"}); }
    default: return false;
    }
}
bool Read(uint16_t opcode, const Object& o) {
    if (opcode == 0x100 || opcode == 0x105) return Closed(o, {});
    const char* filter;
    switch (opcode) {
    case 0x101: if (!Closed(o, {"state", "cursor", "limit"})) return false; filter = "state"; break;
    case 0x102: if (!Closed(o, {"plugin_id", "after_sequence", "limit"})) return false; filter = "plugin_id"; break;
    case 0x103: if (!Closed(o, {"service", "cursor", "limit"})) return false; filter = "service"; break;
    case 0x104: if (!Closed(o, {"row_id", "cursor", "limit"})) return false; filter = "row_id"; break;
    default: return false;
    }
    if (Present(o, "limit")) {
        auto it = o.integers.find("limit");
        if (it == o.integers.end() || !it->second || it->second > (opcode == 0x102 ? 100 : 50)) return false;
    }
    if (Present(o, "cursor")) { const auto* s = String(o, "cursor"); if (!s || !Cursor(*s)) return false; }
    if (Present(o, filter)) {
        const auto* s = String(o, filter); if (!s) return false;
        if (opcode == 0x101 ? !PluginState(*s) : !Id(*s, opcode == 0x102 ? 31 : opcode == 0x103 ? 63 : 128, opcode != 0x102)) return false;
    } else if (opcode == 0x102) return false;
    if (Present(o, "after_sequence")) {
        const auto* s = String(o, "after_sequence"); uint64_t value;
        if (!s || !Decimal(*s, value)) return false;
    }
    return true;
}
namespace {
constexpr size_t kResponseBytes = 65432;
struct Oversize {};
struct UnknownMetadata {};
void Require(bool condition) { if (!condition) throw UnknownMetadata{}; }
class Writer {
public:
    explicit Writer(size_t limit = kResponseBytes) : limit_(limit) {}
    void Add(std::string_view value) {
        if (value.size() > limit_ - text_.size()) throw Oversize{};
        text_.append(value.data(), value.size());
    }
    void Quote(std::string_view value) {
        Require(Utf8(value));
        Add("\"");
        const char hex[] = "0123456789abcdef";
        for (unsigned char c : value) {
            if (c == '\"') Add("\\\"");
            else if (c == '\\') Add("\\\\");
            else if (c < 0x20) {
                const char escape[]{'\\', 'u', '0', '0', hex[c >> 4], hex[c & 15]};
                Add(std::string_view(escape, sizeof(escape)));
            } else { const char byte = static_cast<char>(c); Add(std::string_view(&byte, 1)); }
        }
        Add("\"");
    }
    std::string Finish() { return std::move(text_); }
private:
    std::string text_;
    size_t limit_;
};
std::string Quoted(std::string_view s) { Writer writer; writer.Quote(s); return writer.Finish(); }
std::string Bool(bool value) { return value ? "true" : "false"; }
std::string Number(uint64_t value, uint64_t max = kMaxMonotonicMs) {
    Require(value <= max); return std::to_string(value);
}
std::string OptionalBool(const std::optional<bool>& value) { return value ? Bool(*value) : "null"; }
std::string OptionalNumber(const std::optional<uint32_t>& value, uint64_t max) { return value ? Number(*value, max) : "null"; }
std::string Generation(const std::optional<uint64_t>& value) {
    if (!value) return "null";
    Require(*value != 0); return Quoted(std::to_string(*value));
}
std::string Text(const std::optional<std::string>& value, size_t max = 128) {
    if (!value) return "null";
    // Snapshot copies already applied disclosure policy. Never introduce a looser redactor.
    Require(Label(*value, max)); return Quoted(*value);
}
class Fields {
public:
    explicit Fields(Writer& writer) : writer_(writer) { writer_.Add("{"); }
    void Add(std::string_view key, std::string_view encoded) {
        if (!first_) writer_.Add(",");
        first_ = false; writer_.Quote(key); writer_.Add(":"); writer_.Add(encoded);
    }
    void End() { writer_.Add("}"); }
private:
    Writer& writer_;
    bool first_ = true;
};
bool ErrorCode(std::string_view s) {
    return OneOf(s, {"host_unavailable", "runtime_disabled", "auth_failed", "layout_mismatch", "session_changed",
        "peer_busy", "snapshot_busy", "snapshot_changed", "not_found", "offline_denied", "audit_unavailable",
        "rate_limited", "invalid_request", "oversize", "unknown_metadata", "corrupt_frame", "deadline_expired"});
}
std::string Provenance(const Snapshot& s, std::string_view session, uint64_t epoch, uint64_t now) {
    Require(s.generation != 0 && s.observedMs <= now && s.observedMs <= kMaxMonotonicMs);
    Writer w; Fields f(w);
    f.Add("source", "\"runtime\""); f.Add("schema_version", "1");
    f.Add("session_id", Quoted(session)); f.Add("epoch", Quoted(std::to_string(epoch)));
    f.Add("generation", Quoted(std::to_string(s.generation)));
    f.Add("observed_ms", Number(s.observedMs)); f.Add("age_ms", Number(now - s.observedMs));
    f.Add("host_build", Text(s.host.hostBuild));
    if (s.host.sourceSha) Require(Hex(*s.host.sourceSha, 40));
    f.Add("source_sha", s.host.sourceSha ? Quoted(*s.host.sourceSha) : "null"); f.End();
    return w.Finish();
}
std::string Host(const Snapshot& s) {
    const auto& h = s.host;
    Require(h.diagnosticsVersion == 1 && OneOf(h.readiness, {"starting", "ready", "stopping", "unknown"}));
    Writer w; Fields f(w);
    f.Add("host_version", Text(h.hostVersion)); f.Add("abi_major", Number(h.abiMajor, 65535));
    f.Add("abi_minor", Number(h.abiMinor, 65535)); f.Add("api_table_size", Number(h.abiTableSize, 65536));
    f.Add("sdk_version", Text(h.sdkVersion)); f.Add("game_pack_version", Text(h.gamePackVersion));
    f.Add("game_build", s.sources.gamePack ? Text(s.sources.gamePack->gameBuild) : "null");
    f.Add("diagnostics_version", "1"); f.Add("readiness", Quoted(h.readiness));
    f.Add("restart_required", Bool(h.restartRequired)); f.End(); return w.Finish();
}
std::string Plugin(const PluginMetadata& p) {
    Require(Id(p.pluginId, 31) && PluginState(p.state) && OneOf(p.kind, {"builtin", "native", "lua", "data", "unknown"}));
    Writer w; Fields f(w);
    f.Add("plugin_id", Quoted(p.pluginId)); f.Add("version", Text(p.version)); f.Add("kind", Quoted(p.kind));
    f.Add("manifest_valid", Bool(p.manifestValid)); f.Add("state", Quoted(p.state));
    f.Add("reason", Text(p.reason, 512)); f.Add("load_generation", Generation(p.loadGeneration));
    f.Add("restart_required", Bool(p.restartRequired)); f.End(); return w.Finish();
}
std::string Service(const ServiceMetadata& s) {
    Require(Id(s.service, 63, true) && OneOf(s.ownerCategory, {"host", "game_pack", "plugin", "unknown"}) &&
        OneOf(s.status, {"published", "withdrawn", "admission_blocked", "unknown"}));
    if (s.ownerId) Require(Id(*s.ownerId, 31));
    Writer w; Fields f(w);
    f.Add("service", Quoted(s.service)); f.Add("packed_version", Number(s.packedVersion, 4294967295ULL));
    f.Add("owner_id", s.ownerId ? Quoted(*s.ownerId) : "null"); f.Add("owner_category", Quoted(s.ownerCategory));
    f.Add("status", Quoted(s.status)); f.Add("exposed", Bool(s.exposed));
    f.Add("table_size", OptionalNumber(s.tableSize, 65536)); f.Add("reason", Text(s.reason, 512));
    f.End(); return w.Finish();
}
std::string Resolution(const ResolutionMetadata& r) {
    Require(Id(r.rowId, 128, true) && OneOf(r.state, {"not_run", "ok", "missing", "ambiguous", "failed", "blocked", "unknown"}));
    Writer w; Fields f(w);
    f.Add("row_id", Quoted(r.rowId)); f.Add("state", Quoted(r.state));
    f.Add("match_count", OptionalNumber(r.matchCount, 4294967295ULL)); f.Add("reason", Text(r.reason, 512));
    f.Add("resolution_generation", Generation(r.resolutionGeneration)); f.Add("game_build", Text(r.gameBuild));
    f.End(); return w.Finish();
}
std::string Game(const Snapshot& s, uint64_t now) {
    const auto& g = s.game;
    Require(OneOf(g.lifecycle, {"starting", "ready", "stopping", "unknown"}) &&
        OneOf(g.offlineAdmission, {"authorized", "unknown", "denied"}) && g.capabilities.size() <= 50);
    Writer w; Fields f(w);
    f.Add("lifecycle", Quoted(g.lifecycle));
    if (g.lastTickMs) Require(*g.lastTickMs <= now);
    f.Add("last_tick_age_ms", g.lastTickMs ? Number(now - *g.lastTickMs) : "null");
    f.Add("offline_admission", Quoted(g.offlineAdmission));
    f.Add("game_services_requested", OptionalBool(g.gameServicesRequested));
    f.Add("game_services_built", OptionalBool(g.gameServicesBuilt));
    f.Add("game_services_published", OptionalBool(g.gameServicesPublished));
    Writer items; items.Add("["); bool first = true;
    for (const auto& c : g.capabilities) {
        Require(Id(c.id, 128, true));
        if (!first) items.Add(",");
        first = false;
        Fields item(items); item.Add("id", Quoted(c.id)); item.Add("enabled", OptionalBool(c.enabled));
        item.Add("reason", Text(c.reason, 512)); item.End();
    }
    items.Add("]"); f.Add("capabilities", items.Finish());
    f.Add("capabilities_truncated", Bool(g.capabilitiesTruncated)); f.Add("restart_required", Bool(g.restartRequired));
    f.End(); return w.Finish();
}
std::string Inventory(uint16_t opcode, const Page& page) {
    Require(page.snapshot && page.indices.size() <= 50);
    if (page.nextCursor) Require(Cursor(*page.nextCursor));
    const auto& s = *page.snapshot;
    Writer items; items.Add("["); bool first = true;
    for (const size_t index : page.indices) {
        if (!first) items.Add(",");
        first = false;
        if (opcode == 0x101) { Require(index < s.plugins.size()); items.Add(Plugin(s.plugins[index])); }
        else if (opcode == 0x103) { Require(index < s.services.size()); items.Add(Service(s.services[index])); }
        else { Require(index < s.resolutions.size()); items.Add(Resolution(s.resolutions[index])); }
    }
    items.Add("]"); Writer w; Fields f(w); f.Add("items", items.Finish());
    f.Add("has_more", Bool(page.nextCursor.has_value())); f.Add("next_cursor", page.nextCursor ? Quoted(*page.nextCursor) : "null");
    f.Add("truncated", Bool(page.truncated)); f.Add("redacted_count", Number(page.redactedCount)); f.End();
    return w.Finish();
}
std::string Logs(const LogTail& tail, uint64_t now) {
    Require(tail.entries.size() <= 100 && tail.captureStartedMs <= now);
    Writer items(32768); items.Add("["); bool first = true;
    for (const auto& e : tail.entries) {
        Require(e.sequence && e.monotonicMs <= now && Id(e.pluginId, 31) &&
            OneOf(e.level, {"info", "warning", "error"}) && OneOf(e.origin, {"host", "plugin"}) &&
            !e.message.empty() && e.message.size() <= 512 && Utf8(e.message) && e.untrustedContent);
        if (!first) items.Add(",");
        first = false;
        Fields f(items); f.Add("sequence", Quoted(std::to_string(e.sequence)));
        f.Add("monotonic_ms", Number(e.monotonicMs)); f.Add("plugin_id", Quoted(e.pluginId));
        f.Add("level", Quoted(e.level)); f.Add("origin", Quoted(e.origin)); f.Add("message", Quoted(e.message));
        f.Add("untrusted_content", "true"); f.End();
    }
    items.Add("]"); Writer w(32768); Fields f(w); f.Add("entries", items.Finish());
    f.Add("has_more", Bool(tail.hasMore)); f.Add("next_sequence", Generation(tail.nextSequence));
    f.Add("first_available_sequence", Generation(tail.firstAvailableSequence));
    f.Add("dropped_count", Number(tail.droppedCount)); f.Add("redacted_count", Number(tail.redactedCount));
    f.Add("capture_started_ms", Number(tail.captureStartedMs)); f.Add("free_form_enabled", Bool(tail.freeFormEnabled));
    f.End(); return w.Finish();
}
std::string_view Get(const Object& o, const char* key) { const auto* s = String(o, key); return s ? std::string_view(*s) : std::string_view{}; }
}
std::string Error(std::string_view code) {
    if (!ErrorCode(code)) code = "invalid_request";
    Writer w; Fields f(w); f.Add("status", code == "unknown_metadata" ? "\"unknown\"" : "\"unavailable\"");
    f.Add("provenance", "{\"source\":\"runtime\",\"schema_version\":1,\"session_id\":null,\"epoch\":null,\"generation\":null,\"observed_ms\":null,\"age_ms\":null,\"host_build\":null,\"source_sha\":null}");
    f.Add("data", "null");
    f.Add("error", "{\"code\":" + Quoted(code) + ",\"retry_after_ms\":null}"); f.End(); return w.Finish();
}
std::string Serialize(uint16_t opcode, const Object& o, SnapshotStore& snapshots, Capture& capture,
                      std::string_view session, uint64_t epoch, uint64_t now) {
    if (!Read(opcode, o) || !Hex(session, 32) || !epoch || now > kMaxMonotonicMs) return Error("invalid_request");
    try {
        std::shared_ptr<const Snapshot> snapshot;
        std::string data;
        const auto limit = o.integers.find("limit");
        if (opcode == 0x101 || opcode == 0x103 || opcode == 0x104) {
            const auto kind = opcode == 0x101 ? ListKind::Plugins : opcode == 0x103 ? ListKind::Services : ListKind::Resolutions;
            const char* filter = opcode == 0x101 ? "state" : opcode == 0x103 ? "service" : "row_id";
            const auto page = snapshots.ReadPage(kind, Get(o, filter), Get(o, "cursor"),
                limit == o.integers.end() ? 25 : static_cast<size_t>(limit->second), now);
            if (page.error) return Error(*page.error);
            snapshot = page.snapshot;
            if (!snapshot) return Error("unknown_metadata");
            data = Inventory(opcode, page);
        } else {
            snapshot = snapshots.Latest();
            if (!snapshot) return Error("unknown_metadata");
            if (snapshot->observedMs > now) return Error("invalid_request");
            if (opcode == 0x100) data = Host(*snapshot);
            else if (opcode == 0x105) data = Game(*snapshot, now);
            else {
                uint64_t after = 0;
                if (const auto* sequence = String(o, "after_sequence")) Require(Decimal(*sequence, after));
                const auto tail = capture.Tail(Get(o, "plugin_id"), after,
                    limit == o.integers.end() ? 50 : static_cast<size_t>(limit->second));
                if (tail.error) return Error(*tail.error);
                data = Logs(tail, now);
            }
        }
        const auto provenance = Provenance(*snapshot, session, epoch, now);
        const auto status = now - snapshot->observedMs > 1000 ? "stale" :
            (snapshot->host.restartRequired || snapshot->game.restartRequired) ? "restart_required" : "ok";
        Writer w; Fields f(w); f.Add("status", Quoted(status)); f.Add("provenance", provenance);
        f.Add("data", data); f.Add("error", "null"); f.End(); return w.Finish();
    } catch (const Oversize&) { return Error("oversize"); }
      catch (const UnknownMetadata&) { return Error("unknown_metadata"); }
      catch (...) { return Error("snapshot_busy"); }
}
} // namespace sco::diagnostics::json
