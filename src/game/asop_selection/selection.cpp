// SPDX-License-Identifier: GPL-3.0-only
#include <sc_asop_selection.h>
#include <sco/game/asop_selection.h>
#include <sco/host.h>
#include <sco/net/sha2.h>
#include "../../api/internal.h"
#include "../services/spawn.h"
#include <algorithm>
#include <cctype>
#include <cstring>
#include <deque>
#include <limits>
#include <mutex>
#include <utility>

namespace sco::game::asop_selection {
namespace {
struct Receipt {
    uint64_t id = 0, key = 0, lease = 0, session = 0, file_revision = 0, committed_revision = 0;
    uint32_t phase = SC_ASOP_SELECTION_EDIT_ACCEPTED, reason = 0;
    std::vector<std::string> classes;
    std::array<uint8_t, 32> digest{};
};
struct Model {
    bool started = false, busy = false, known = false;
    uint32_t reason = SC_ASOP_SELECTION_REASON_NONE;
    uint64_t session = 0, file_revision = 0, semantic_revision = 0;
    const void* editor = nullptr;
    uint64_t lease = 0, key_highwater = 0;
    std::deque<Receipt> receipts;
    uint64_t pending = 0;
    const void* last_editor = nullptr;
    uint64_t last_lease = 0;
    FileAdapter file{};
    void* busy_context = nullptr;
    bool (*class_busy)(void*, const char*) = nullptr;
    Document document{};
    std::vector<std::string> classes;
    std::vector<std::string> catalogue;
};
Model g;
std::mutex g_lock;
uint64_t next_session = 0, next_lease = 0, next_request = 0;
struct BusyGuard { ~BusyGuard() { std::lock_guard<std::mutex> lock(g_lock); g.busy = false; } };
void RevokeEditor(const void* owner) {
    if (owner && g.editor == owner) {
        for (auto& receipt : g.receipts) if (receipt.id == g.pending && (receipt.phase == SC_ASOP_SELECTION_EDIT_ACCEPTED || receipt.phase == SC_ASOP_SELECTION_FILE_PREPARING)) {
            receipt.phase = SC_ASOP_SELECTION_EDIT_REVOKED; g.pending = 0; break;
        }
        g.last_editor = owner; g.last_lease = g.lease; g.editor = nullptr; g.lease = 0;
    }
}
void OwnerReleased(const void* owner) {
    std::lock_guard<std::mutex> lock(g_lock);
    RevokeEditor(owner);
}
bool Space(char c) { return c == ' ' || c == '\t' || c == '\r'; }
char Fold(char c) { return c >= 'A' && c <= 'Z' ? static_cast<char>(c + ('a' - 'A')) : c; }
bool EqualFold(std::string_view a, std::string_view b) {
    if (a.size() != b.size()) return false;
    for (size_t i = 0; i < a.size(); ++i) if (Fold(a[i]) != Fold(b[i])) return false;
    return true;
}
bool Parse(const Document& d, std::vector<std::string>& out) {
    if (!d.present) return d.bytes.empty();
    if (d.bytes.size() > SC_ASOP_SELECTION_MAX_FILE_BYTES || d.bytes.find('\0') != std::string::npos) return false;
    size_t pos = 0;
    while (pos < d.bytes.size()) {
        const size_t e = d.bytes.find('\n', pos);
        std::string_view line(d.bytes.data() + pos, (e == std::string::npos ? d.bytes.size() : e) - pos);
        const size_t comment = line.find('#');
        if (comment != std::string_view::npos) line = line.substr(0, comment);
        while (!line.empty() && Space(line.front())) line.remove_prefix(1);
        while (!line.empty() && Space(line.back())) line.remove_suffix(1);
        if (!line.empty()) {
            if (line.size() > SC_ASOP_SELECTION_MAX_CLASS_BYTES) return false;
            for (char c : line) if (!((c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z') ||
                                       (c >= '0' && c <= '9') || c == '_' || c == '-' || c == '.')) return false;
            if (std::none_of(out.begin(), out.end(), [&](const std::string& x) { return EqualFold(x, line); })) {
                if (out.size() == SC_ASOP_SELECTION_MAX_CLASSES) return false;
                out.emplace_back(line);
            }
        }
        if (e == std::string::npos) break;
        pos = e + 1;
    }
    return true;
}
sco_result State(sco_plugin* self, sc_asop_selection_state* out) {
    if (!OnGameThread()) return SCO_WRONG_THREAD;
    if (!self || !host::PluginId(self) || detail::Released(self) || !out || out->size < sizeof(*out)) return SCO_BAD_ARG;
    std::lock_guard<std::mutex> lock(g_lock);
    if (!g.started || g.busy) return SCO_UNAVAILABLE;
    sc_asop_selection_state value{};
    value.size = sizeof(value); value.session = g.session;
    value.file_revision = g.file_revision; value.catalogue_generation = 1; value.semantic_revision = g.semantic_revision;
    value.file_count = g.known ? static_cast<uint32_t>(g.classes.size()) : 0;
    value.reason = g.reason;
    if (g.reason != SC_ASOP_SELECTION_REASON_NONE) value.flags |= SC_ASOP_SELECTION_FILE_ERROR;
    if (g.known) {
        value.flags |= SC_ASOP_SELECTION_FILE_KNOWN;
        sco_sha256_digest(g.document.bytes.data(), g.document.bytes.size(), value.file_digest);
    }
    if (g.known && g.document.present) value.flags |= SC_ASOP_SELECTION_FILE_PRESENT;
    *out = value;
    return SCO_OK;
}
bool SemanticEqual(const std::vector<std::string>& a, const std::vector<std::string>& b) {
    if (a.size() != b.size()) return false;
    for (size_t i = 0; i < a.size(); ++i) if (!EqualFold(a[i], b[i])) return false;
    return true;
}
sco_result Refresh(sco_plugin* self) {
    if (!OnGameThread()) return SCO_WRONG_THREAD;
    if (!self || !host::PluginId(self) || detail::Released(self)) return SCO_BAD_ARG;
    FileAdapter file;
    {
        std::lock_guard<std::mutex> lock(g_lock);
        if (!g.started || g.busy) return SCO_UNAVAILABLE;
        g.busy = true; file = g.file;
    }
    BusyGuard guard;
    try {
        Document current;
        const FileResult r = file.read(file.context, current);
        if (r == FileResult::NotFound) current = {};
        else if (r != FileResult::Ok) {
            std::lock_guard<std::mutex> lock(g_lock);
            g.known = false; g.reason = r == FileResult::Invalid ? SC_ASOP_SELECTION_REASON_INVALID : SC_ASOP_SELECTION_REASON_IO;
            return r == FileResult::Invalid ? SCO_BAD_ARG : SCO_FAILED;
        }
        std::vector<std::string> parsed;
        if (!Parse(current, parsed)) {
            std::lock_guard<std::mutex> lock(g_lock);
            g.known = false; g.reason = SC_ASOP_SELECTION_REASON_INVALID; return SCO_BAD_ARG;
        }
        std::lock_guard<std::mutex> lock(g_lock);
        const bool file_changed = current.present != g.document.present || current.bytes != g.document.bytes || current.identity != g.document.identity;
        const bool semantic_changed = !SemanticEqual(g.classes, parsed);
        if ((file_changed && g.file_revision == std::numeric_limits<uint64_t>::max()) ||
            (semantic_changed && g.semantic_revision == std::numeric_limits<uint64_t>::max())) return SCO_FAILED;
        if (file_changed) ++g.file_revision;
        if (semantic_changed) ++g.semantic_revision;
        g.document = std::move(current); g.classes = std::move(parsed);
        g.known = true; g.reason = SC_ASOP_SELECTION_REASON_NONE;
        return SCO_OK;
    } catch (...) {
        std::lock_guard<std::mutex> lock(g_lock);
        g.known = false; g.reason = SC_ASOP_SELECTION_REASON_IO; return SCO_FAILED;
    }
}
sco_result Rows(sco_plugin* self, const sc_asop_selection_read* read, sc_asop_selection_row* out,
                uint32_t stride, uint32_t capacity, uint32_t* count, uint32_t* more) {
    if (!OnGameThread()) return SCO_WRONG_THREAD;
    if (!self || !host::PluginId(self) || detail::Released(self) || !read || read->size < sizeof(*read) ||
        read->reserved || !count || !more || count == more || capacity > SC_ASOP_SELECTION_MAX_PAGE ||
        (capacity && (!out || stride < sizeof(sc_asop_selection_row) || stride > SC_ASOP_SELECTION_MAX_STRIDE ||
                      stride % alignof(sc_asop_selection_row) || reinterpret_cast<uintptr_t>(out) % alignof(sc_asop_selection_row)))) return SCO_BAD_ARG;
    std::lock_guard<std::mutex> lock(g_lock);
    if (!g.started || g.busy) return SCO_UNAVAILABLE;
    const std::vector<std::string>* values = nullptr;
    uint64_t revision = 0;
    if (read->kind == SC_ASOP_SELECTION_ROWS_FILE) {
        if (!g.known) return SCO_UNAVAILABLE;
        values = &g.classes; revision = g.file_revision;
    } else if (read->kind == SC_ASOP_SELECTION_ROWS_CATALOGUE) { values = &g.catalogue; revision = 1; }
    else return SCO_BAD_ARG;
    if (!read->session || read->session != g.session || !read->revision || read->revision != revision) return SCO_NOT_FOUND;
    if (read->offset > values->size()) return SCO_BAD_ARG;
    const auto available = static_cast<uint32_t>(values->size() - read->offset);
    const uint32_t n = std::min(capacity, available);
    auto* bytes = reinterpret_cast<unsigned char*>(out);
    for (uint32_t i = 0; i < n; ++i) {
        uint32_t declared = 0; std::memcpy(&declared, bytes + static_cast<size_t>(i) * stride, sizeof(declared));
        if (declared < sizeof(sc_asop_selection_row)) return SCO_BAD_ARG;
    }
    for (uint32_t i = 0; i < n; ++i) {
        sc_asop_selection_row row{}; row.size = sizeof(row);
        const std::string& value = (*values)[read->offset + i];
        if (g.class_busy && g.class_busy(g.busy_context, value.c_str())) row.flags |= SC_ASOP_SELECTION_ROW_BUSY;
        std::memcpy(row.class_name, value.c_str(), value.size() + 1);
        std::memcpy(bytes + static_cast<size_t>(i) * stride, &row, sizeof(row));
    }
    *count = n; *more = available - n;
    return SCO_OK;
}
sco_result Acquire(sco_plugin* self, uint64_t session, uint64_t* out) {
    if (!OnGameThread()) return SCO_WRONG_THREAD;
    if (!self || !host::PluginId(self) || detail::Released(self) || !out) return SCO_BAD_ARG;
    std::lock_guard<std::mutex> lock(g_lock);
    if (!g.started || g.busy) return SCO_UNAVAILABLE;
    if (!session || session != g.session) return SCO_NOT_FOUND;
    if (detail::Released(self)) return SCO_BAD_ARG;
    if (g.editor && detail::Released(g.editor)) RevokeEditor(g.editor);
    if (g.editor && g.editor != self) return SCO_TOO_MANY;
    if (!g.editor) {
        if (next_lease == std::numeric_limits<uint64_t>::max()) return SCO_FAILED;
        g.editor = self; g.lease = ++next_lease; g.key_highwater = 0; g.receipts.clear(); g.pending = 0;
    }
    *out = g.lease;
    return SCO_OK;
}
sco_result Release(sco_plugin* self, uint64_t lease) {
    if (!OnGameThread()) return SCO_WRONG_THREAD;
    if (!self || !host::PluginId(self) || detail::Released(self) || !lease) return SCO_BAD_ARG;
    std::lock_guard<std::mutex> lock(g_lock);
    if (!g.started) return SCO_UNAVAILABLE;
    if (g.editor == self && g.lease == lease) {
        for (auto& receipt : g.receipts) if (receipt.id == g.pending && (receipt.phase == SC_ASOP_SELECTION_EDIT_ACCEPTED || receipt.phase == SC_ASOP_SELECTION_FILE_PREPARING)) {
            receipt.phase = SC_ASOP_SELECTION_EDIT_REVOKED; g.pending = 0; break;
        }
        g.last_editor = self; g.last_lease = lease; g.editor = nullptr; g.lease = 0; return SCO_OK;
    }
    return g.last_editor == self && g.last_lease == lease ? SCO_OK : SCO_NOT_FOUND;
}
sco_result Replace(sco_plugin* self, uint64_t lease, const sc_asop_selection_request* request, uint64_t* out) {
    if (!OnGameThread()) return SCO_WRONG_THREAD;
    if (!self || !host::PluginId(self) || detail::Released(self) || !out || !request || request->size < sizeof(*request) ||
        request->flags || !lease || !request->client_key || request->count > SC_ASOP_SELECTION_MAX_CLASSES ||
        (request->count && (!request->classes || request->class_stride < sizeof(sc_asop_selection_class) ||
                            request->class_stride > SC_ASOP_SELECTION_MAX_STRIDE ||
                            request->class_stride % alignof(sc_asop_selection_class) ||
                            reinterpret_cast<uintptr_t>(request->classes) % alignof(sc_asop_selection_class)))) return SCO_BAD_ARG;
    try {
        std::vector<std::string> selected;
        const auto* bytes = reinterpret_cast<const unsigned char*>(request->classes);
        for (uint32_t i = 0; i < request->count; ++i) {
            sc_asop_selection_class item{};
            std::memcpy(&item, bytes + static_cast<size_t>(i) * request->class_stride, sizeof(item));
            const char* end = static_cast<const char*>(std::memchr(item.class_name, 0, sizeof(item.class_name)));
            if (item.size < sizeof(item) || item.flags || !end || end == item.class_name) return SCO_BAD_ARG;
            Document d; d.present = true; d.bytes.assign(item.class_name, static_cast<size_t>(end - item.class_name));
            std::vector<std::string> one;
            if (!Parse(d, one) || one.size() != 1 || one[0] != d.bytes ||
                std::any_of(selected.begin(), selected.end(), [&](const std::string& x) { return EqualFold(x, one[0]); })) return SCO_BAD_ARG;
            selected.push_back(std::move(one[0]));
        }
        std::lock_guard<std::mutex> lock(g_lock);
        if (!g.started || g.busy) return SCO_UNAVAILABLE;
        if (g.editor != self || g.lease != lease) return SCO_NOT_FOUND;
        for (const auto& saved : g.receipts) if (saved.key == request->client_key) {
            if (saved.session != request->session || saved.file_revision != request->file_revision ||
                std::memcmp(saved.digest.data(), request->file_digest, saved.digest.size()) != 0 || saved.classes != selected) return SCO_BAD_ARG;
            *out = saved.id; return SCO_OK;
        }
        if (request->client_key <= g.key_highwater) return SCO_NOT_FOUND;
        if (!g.known) return SCO_UNAVAILABLE;
        uint8_t digest[32]; sco_sha256_digest(g.document.bytes.data(), g.document.bytes.size(), digest);
        if (request->session != g.session || request->file_revision != g.file_revision ||
            std::memcmp(digest, request->file_digest, sizeof(digest)) != 0) return SCO_NOT_FOUND;
        if (g.pending) return SCO_TOO_MANY;
        if (next_request == std::numeric_limits<uint64_t>::max()) return SCO_FAILED;
        Receipt item; item.id = next_request + 1; item.key = request->client_key; item.lease = lease;
        item.session = request->session; item.file_revision = request->file_revision; item.classes = std::move(selected);
        std::copy_n(request->file_digest, item.digest.size(), item.digest.begin());
        g.receipts.push_back(std::move(item));
        if (g.receipts.size() > 32) g.receipts.pop_front();
        ++next_request; g.pending = next_request; g.key_highwater = request->client_key; *out = next_request;
        return SCO_OK;
    } catch (...) { return SCO_FAILED; }
}
sco_result Status(sco_plugin* self, uint64_t lease, uint64_t request_id, sc_asop_selection_receipt* out) {
    if (!OnGameThread()) return SCO_WRONG_THREAD;
    if (!self || !host::PluginId(self) || detail::Released(self) || !lease || !request_id || !out || out->size < sizeof(*out)) return SCO_BAD_ARG;
    std::lock_guard<std::mutex> lock(g_lock);
    if (!g.started || g.busy) return SCO_UNAVAILABLE;
    if (g.editor != self || g.lease != lease) return SCO_NOT_FOUND;
    for (const auto& r : g.receipts) if (r.id == request_id && r.lease == lease) {
        sc_asop_selection_receipt value{}; value.size=sizeof(value); value.phase=r.phase; value.session=r.session;
        value.request_id=r.id; value.client_key=r.key; value.file_revision=r.committed_revision ? r.committed_revision : r.file_revision; value.reason=r.reason;
        *out=value; return SCO_OK;
    }
    return SCO_NOT_FOUND;
}
bool BeginCommit(void* context) {
    const uint64_t id = *static_cast<const uint64_t*>(context);
    std::lock_guard<std::mutex> lock(g_lock);
    if (!g.started || !g.busy || g.pending != id || !g.editor) return false;
    if (detail::Released(g.editor)) { RevokeEditor(g.editor); return false; }
    for (auto& r : g.receipts) if (r.id == id && r.phase == SC_ASOP_SELECTION_FILE_PREPARING) {
        // Last gate before the irreversible write: a class whose ship is in play this
        // session cannot be removed. Keeps the current list; the caller keeps its draft.
        if (g.class_busy && g.known) {
            for (const auto& saved : g.classes) {
                bool removed = true;
                for (const auto& next : r.classes) if (EqualFold(saved, next)) { removed = false; break; }
                if (!removed) continue;
                bool busy = false;
                try { busy = g.class_busy(g.busy_context, saved.c_str()); } catch (...) { busy = true; }
                if (busy) {
                    r.phase = SC_ASOP_SELECTION_EDIT_CONFLICT; r.reason = SC_ASOP_SELECTION_REASON_BUSY;
                    g.pending = 0; return false;
                }
            }
        }
        r.phase = SC_ASOP_SELECTION_FILE_COMMITTING; return true;
    }
    return false;
}
const sc_asop_selection_v1 table{sizeof(table), 0, State, Refresh, Rows, Acquire, Release, Replace, Status};
}
Result Start(const Options& options) {
    if (!OnGameThread()) return Result::WrongThread;
    if (!options.file.read || !options.file.replace) return Result::BadArg;
    {
        std::lock_guard<std::mutex> lock(g_lock);
        if (g.started || g.busy || next_session == std::numeric_limits<uint64_t>::max()) return Result::Failed;
        g.busy = true;
    }
    BusyGuard guard;
    try {
        Document d;
        FileResult r = FileResult::Error;
        try { r = options.file.read(options.file.context, d); } catch (...) {}
        if (r == FileResult::NotFound) d = {};
        std::vector<std::string> parsed;
        const bool readable = r == FileResult::Ok || r == FileResult::NotFound;
        const bool valid = readable && Parse(d, parsed);
        Model next;
        next.file = options.file;
        next.busy_context = options.busy_context;
        next.class_busy = options.class_busy;
        if (valid) { next.document = std::move(d); next.classes = std::move(parsed); }
        next.reason = valid ? SC_ASOP_SELECTION_REASON_NONE :
            (readable || r == FileResult::Invalid ? SC_ASOP_SELECTION_REASON_INVALID : SC_ASOP_SELECTION_REASON_IO);
        for (const auto& name : options.catalogue) {
            Document entry; entry.present = true; entry.bytes = name;
            std::vector<std::string> one;
            if (!Parse(entry, one) || one.size() != 1 || one[0] != name) return Result::BadArg;
            if (std::none_of(next.catalogue.begin(), next.catalogue.end(), [&](const std::string& x) { return EqualFold(x, name); })) {
                if (next.catalogue.size() == SC_ASOP_SELECTION_MAX_CLASSES) return Result::BadArg;
                next.catalogue.push_back(name);
            }
        }
        next.file_revision = 1; next.semantic_revision = 1; next.known = valid; next.started = true;
        Result result = services::AddGameReleaseHook(OwnerReleased);
        if (result != Result::Ok) return result;
        result = host::ProvideGameService(SC_ASOP_SELECTION_NAME, SC_ASOP_SELECTION_VERSION, &table);
        if (result != Result::Ok) { services::RemoveGameReleaseHook(OwnerReleased); return result; }
        {
            std::lock_guard<std::mutex> lock(g_lock);
            next.session = ++next_session; g = std::move(next);
        }
        return Result::Ok;
    } catch (...) { return Result::Failed; }
}
Result Pump() {
    if (!OnGameThread()) return Result::WrongThread;
    FileAdapter file;
    Document expected;
    std::vector<std::string> selected;
    std::string bytes = "# ASOP ship selection, one class per line.\n";
    uint64_t request_id = 0;
    {
        std::lock_guard<std::mutex> lock(g_lock);
        if (!g.started || g.busy) return Result::Unavailable;
        if (g.editor && detail::Released(g.editor)) RevokeEditor(g.editor);
        if (!g.pending) return Result::Ok;
        try {
            auto it = std::find_if(g.receipts.begin(), g.receipts.end(), [&](const Receipt& r) { return r.id == g.pending; });
            if (it == g.receipts.end() || it->phase != SC_ASOP_SELECTION_EDIT_ACCEPTED) return Result::Failed;
            if (!g.known || it->session != g.session || it->file_revision != g.file_revision) {
                it->phase = SC_ASOP_SELECTION_EDIT_CONFLICT; it->reason = SC_ASOP_SELECTION_REASON_CONFLICT;
                g.pending = 0; return Result::Ok;
            }
            for (const auto& name : it->classes) { bytes += name; bytes += '\n'; }
            if (bytes.size() > SC_ASOP_SELECTION_MAX_FILE_BYTES) return Result::BadArg;
            file = g.file; expected = g.document; selected = it->classes; request_id = it->id;
            it->phase = SC_ASOP_SELECTION_FILE_PREPARING; g.busy = true;
        } catch (...) { return Result::Failed; }
    }
    BusyGuard guard;
    Document committed;
    FileResult result = FileResult::OutcomeUnknown;
    const CommitPermit permit{&request_id, BeginCommit};
    try { result = file.replace(file.context, expected, bytes, committed, permit); } catch (...) {}
    {
        std::lock_guard<std::mutex> lock(g_lock);
        auto it = std::find_if(g.receipts.begin(), g.receipts.end(), [&](const Receipt& r) { return r.id == request_id; });
        if (it == g.receipts.end()) return Result::Failed;
        if (it->phase == SC_ASOP_SELECTION_FILE_PREPARING && result == FileResult::Cancelled) {
            it->phase = SC_ASOP_SELECTION_EDIT_REVOKED;
            g.pending = 0; return Result::Ok;
        }
        if (it->phase == SC_ASOP_SELECTION_FILE_PREPARING && result == FileResult::Busy) {
            it->phase = SC_ASOP_SELECTION_EDIT_CONFLICT;
            it->reason = SC_ASOP_SELECTION_REASON_BUSY;
            g.pending = 0; return Result::Ok;
        }
        if (it->phase == SC_ASOP_SELECTION_EDIT_REVOKED && result == FileResult::Cancelled) {
            g.pending = 0; return Result::Ok;
        }
        // A BeginCommit busy refusal already resolved the receipt; the adapter's Cancelled
        // is the expected echo, and the committed document stays known and unchanged.
        if (it->phase == SC_ASOP_SELECTION_EDIT_CONFLICT && it->reason == SC_ASOP_SELECTION_REASON_BUSY &&
            result == FileResult::Cancelled) {
            g.pending = 0; return Result::Ok;
        }
        if (result == FileResult::Ok && it->phase == SC_ASOP_SELECTION_FILE_COMMITTING && committed.present && committed.bytes == bytes &&
            g.file_revision != std::numeric_limits<uint64_t>::max() && g.semantic_revision != std::numeric_limits<uint64_t>::max()) {
            const bool semantic_changed = !SemanticEqual(g.classes, selected);
            ++g.file_revision; if (semantic_changed) ++g.semantic_revision;
            g.document = std::move(committed); g.classes = std::move(selected); g.known = true; g.reason = 0;
            it->phase = SC_ASOP_SELECTION_FILE_COMMITTED; it->committed_revision = g.file_revision;
        } else {
            g.known = false;
            const bool conflict = result == FileResult::Conflict;
            g.reason = conflict ? SC_ASOP_SELECTION_REASON_CONFLICT : SC_ASOP_SELECTION_REASON_OUTCOME_UNKNOWN;
            it->phase = conflict ? SC_ASOP_SELECTION_EDIT_CONFLICT : SC_ASOP_SELECTION_EDIT_UNKNOWN;
            it->reason = g.reason;
        }
        g.pending = 0;
    }
    return Result::Ok;
}
Result Stop() {
    if (!OnGameThread()) return Result::WrongThread;
    {
        std::lock_guard<std::mutex> lock(g_lock);
        if (g.busy) return Result::Failed;
        if (!g.started) return Result::Ok;
        g.busy = true;
    }
    BusyGuard guard;
    const Result r = host::WithdrawGameService(SC_ASOP_SELECTION_NAME);
    if (r != Result::Ok) return r;
    services::RemoveGameReleaseHook(OwnerReleased);
    { std::lock_guard<std::mutex> lock(g_lock); g = {}; }
    return Result::Ok;
}
} // namespace sco::game::asop_selection
