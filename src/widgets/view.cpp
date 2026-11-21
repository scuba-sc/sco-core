// SPDX-License-Identifier: GPL-3.0-only
// The sco.widgets.view table: a host-side flat reader of sco.widgets panels and an event queue
// for a menu renderer on its own thread. Not a plugin service. See include/sco/widgets_view.h
// and docs/widgets-view.md.
#include "view_internal.h"
#include "sco/runtime.h"
#include "../api/internal.h"
#include <algorithm>
#include <atomic>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <deque>
#include <mutex>
#include <string>
#include <thread>
#include <utility>
#include <vector>

struct sco_view_client {
    // The registered menu thread. Own small mutex, never held with the widget lock.
    std::mutex threadLock;
    bool hasThread = false;
    std::thread::id thread;
    // The event queue. Own small mutex, never held while a callback runs.
    std::mutex queueLock;
    struct Slot {
        std::string panel;
        uint32_t nodeId = 0, kind = 0;
        int32_t index = 0;
        double value = 0;
        std::string text;
        int32_t row = -1;
        uint64_t epoch = 0, key = 0, session = 0;
        uint64_t first = 0, last = 0; // sequence numbers this slot covers
    };
    std::deque<Slot> slots;
    uint64_t nextSeq = 1;
    std::atomic<uint64_t> eventsDone{0}, eventDrops{0};
};

namespace sco::widgets::viewimpl {
namespace {

constexpr uint32_t kQueueSlots = SCO_WIDGETS_MAX_EVENTS;
constexpr uint32_t kFeatures = SCO_VIEW_FEATURE_TAGS | SCO_VIEW_FEATURE_COALESCE | SCO_VIEW_FEATURE_HOST_PANELS;

// The client lives forever: a renderer thread may call after every static destructor ran.
sco_view_client& Client() {
    static sco_view_client* const client = new sco_view_client;
    return *client;
}

template<class F> sco_result Guard(F&& fn) noexcept {
    try { return static_cast<sco_result>(fn()); } catch (...) { return SCO_TOO_MANY; }
}

// The lease every call takes, as RenderPanel does.
struct Admission {
    sco::detail::RuntimeLease lease;
    bool ok() const {
        return lease && !lease.startup() && !(lease.owned() && sco::detail::RuntimeStopping()) && Running();
    }
};

bool IsMenuThread(sco_view_client& c) {
    std::lock_guard<std::mutex> hold(c.threadLock);
    return c.hasThread && c.thread == std::this_thread::get_id();
}

// ---- blob writer ----
template<class T> void Put(std::vector<unsigned char>& buffer, size_t at, const T& v) {
    std::memcpy(buffer.data() + at, &v, sizeof(T));
}
sco_view_str AddString(std::string& table, const std::string& s) {
    if (s.empty()) return {0, 0};
    const sco_view_str ref{static_cast<uint32_t>(table.size()), static_cast<uint32_t>(s.size())};
    table.append(s);
    table.push_back('\0');
    return ref;
}
size_t Align8(size_t n) { return (n + 7) & ~static_cast<size_t>(7); }

std::vector<unsigned char> Flatten(const Copied& copied) {
    const Panel& panel = *copied.panel;
    size_t itemTotal = 0;
    for (const auto& n : panel.nodes) itemTotal += n.items.size();
    std::string strings;
    std::vector<sco_view_str> items;
    items.reserve(itemTotal);
    std::vector<sco_view_node> nodes;
    nodes.reserve(panel.nodes.size());
    for (const auto& n : panel.nodes) {
        sco_view_node out{};
        out.size = sizeof(sco_view_node);
        out.kind = n.kind; out.id = n.id; out.parent = n.parent; out.flags = n.flags; out.index = n.index;
        out.severity = n.severity; out.first_row = n.first_row; out.visible_rows = n.visible_rows;
        out.presentation = n.presentation; out.columns = n.columns;
        out.item_count = static_cast<uint32_t>(n.items.size());
        out.first_item = static_cast<uint32_t>(items.size());
        out.value = n.value; out.minimum = n.minimum; out.maximum = n.maximum;
        out.label = AddString(strings, n.label);
        // A password value never enters a blob, in any shape.
        out.text = (n.flags & SCO_WIDGET_PASSWORD) ? sco_view_str{0, 0} : AddString(strings, n.text);
        out.tooltip = AddString(strings, n.tooltip);
        out.tag = AddString(strings, n.tag);
        out.key = NodeKey(n);
        for (const auto& item : n.items) items.push_back(AddString(strings, item));
        nodes.push_back(out);
    }
    sco_view_blob_header header{};
    header.size = sizeof(sco_view_blob_header);
    header.magic = SCO_VIEW_BLOB_MAGIC;
    header.version = SCO_WIDGETS_VIEW_VERSION_1_0;
    header.revision = copied.revision;
    header.epoch = copied.epoch;
    header.node_count = static_cast<uint32_t>(nodes.size());
    header.node_stride = sizeof(sco_view_node);
    header.nodes_offset = static_cast<uint32_t>(Align8(sizeof(sco_view_blob_header)));
    header.items_offset = header.nodes_offset + static_cast<uint32_t>(nodes.size() * sizeof(sco_view_node));
    header.item_total = static_cast<uint32_t>(items.size());
    header.strings_offset = header.items_offset + static_cast<uint32_t>(items.size() * sizeof(sco_view_str));
    header.strings_bytes = static_cast<uint32_t>(strings.size());
    header.flags = copied.flags;
    header.panel_id = AddString(strings, panel.id);
    header.title = AddString(strings, panel.title);
    header.owner = AddString(strings, panel.owner);
    header.strings_bytes = static_cast<uint32_t>(strings.size());
    header.total_bytes = header.strings_offset + header.strings_bytes;
    std::vector<unsigned char> blob(header.total_bytes, 0);
    Put(blob, 0, header);
    for (size_t i = 0; i < nodes.size(); ++i) Put(blob, header.nodes_offset + i * sizeof(sco_view_node), nodes[i]);
    for (size_t i = 0; i < items.size(); ++i) Put(blob, header.items_offset + i * sizeof(sco_view_str), items[i]);
    if (!strings.empty()) std::memcpy(blob.data() + header.strings_offset, strings.data(), strings.size());
    return blob;
}

void CopyField(char (&dst)[SCO_VIEW_ID_BYTES], const std::string& src) {
    std::memset(dst, 0, sizeof(dst));
    std::memcpy(dst, src.data(), std::min(src.size(), sizeof(dst) - 1));
}

bool Mergeable(uint32_t kind) { return kind == SCO_WIDGET_SLIDER || kind == SCO_WIDGET_TEXT_INPUT; }

// ---- the table ----
sco_result Capabilities(sco_view_client* client, uint32_t* caps) {
    return Guard([&] {
        if (!client || !caps || caps[SCO_VIEW_CAP_COUNT] < 2) return SCO_BAD_ARG;
        Admission admission;
        if (!admission.ok()) return SCO_UNAVAILABLE;
        const uint32_t known[SCO_VIEW_CAPS_1_0]{
            SCO_VIEW_CAPS_1_0, kFeatures, SCO_WIDGETS_MAX_PANELS, kQueueSlots,
            static_cast<uint32_t>(sizeof(sco_view_node)), static_cast<uint32_t>(sizeof(sco_view_panel_entry))};
        const uint32_t n = std::min(caps[SCO_VIEW_CAP_COUNT], SCO_VIEW_CAPS_1_0);
        for (uint32_t i = 1; i < n; ++i) caps[i] = known[i];
        caps[SCO_VIEW_CAP_COUNT] = n;
        return SCO_OK;
    });
}

sco_result Stamps(sco_view_client* client, uint64_t* global, uint64_t* list, uint64_t* drops, uint64_t* done) {
    if (!client) return SCO_BAD_ARG;
    Admission admission;
    if (!admission.ok()) return SCO_UNAVAILABLE;
    if (global) *global = GlobalRevision();
    if (list) *list = ListRevision();
    if (drops) *drops = client->eventDrops.load(std::memory_order_acquire);
    if (done) *done = client->eventsDone.load(std::memory_order_acquire);
    return SCO_OK;
}

sco_result MenuThreadEnter(sco_view_client* client) {
    if (!client) return SCO_BAD_ARG;
    Admission admission;
    if (!admission.ok()) return SCO_UNAVAILABLE;
    if (OnGameThread()) return SCO_WRONG_THREAD;
    std::lock_guard<std::mutex> hold(client->threadLock);
    const auto self = std::this_thread::get_id();
    if (client->hasThread) return client->thread == self ? SCO_OK : SCO_FAILED;
    client->hasThread = true;
    client->thread = self;
    return SCO_OK;
}

// Works after stop and never answers UNAVAILABLE: the thread must be able to leave.
sco_result MenuThreadLeave(sco_view_client* client) {
    if (!client) return SCO_BAD_ARG;
    std::lock_guard<std::mutex> hold(client->threadLock);
    if (!client->hasThread) return SCO_OK;
    if (client->thread != std::this_thread::get_id()) return SCO_WRONG_THREAD;
    client->hasThread = false;
    client->thread = std::thread::id();
    return SCO_OK;
}

sco_result ListPanels(sco_view_client* client, sco_view_panel_entry* out, uint32_t stride, uint32_t* inoutCount) {
    return Guard([&] {
        if (!client) return SCO_BAD_ARG;
        Admission admission;
        if (!admission.ok()) return SCO_UNAVAILABLE;
        if (!IsMenuThread(*client)) return SCO_WRONG_THREAD;
        if (!inoutCount || stride < sizeof(sco_view_panel_entry) || stride % 8 ||
            (out && reinterpret_cast<uintptr_t>(out) % 8)) return SCO_BAD_ARG;
        std::vector<ListRow> rows;
        if (const auto r = List(rows); r != Result::Ok) return static_cast<sco_result>(r);
        const uint32_t capacity = *inoutCount;
        *inoutCount = static_cast<uint32_t>(rows.size());
        if (rows.size() > capacity) return SCO_TOO_MANY;
        if (!rows.empty() && !out) return SCO_BAD_ARG;
        for (size_t i = 0; i < rows.size(); ++i) {
            unsigned char* at = reinterpret_cast<unsigned char*>(out) + i * stride;
            std::memset(at, 0, stride);
            sco_view_panel_entry entry{};
            entry.size = sizeof(sco_view_panel_entry);
            entry.flags = rows[i].flags;
            entry.revision = rows[i].revision;
            entry.epoch = rows[i].epoch;
            entry.node_count = rows[i].nodeCount;
            CopyField(entry.id, rows[i].id);
            CopyField(entry.title, rows[i].title);
            CopyField(entry.owner, rows[i].owner);
            std::memcpy(at, &entry, sizeof(entry));
        }
        return SCO_OK;
    });
}

sco_result CopyPanel(sco_view_client* client, const char* panelId, void* out, uint32_t* inoutBytes) {
    return Guard([&] {
        if (!client) return SCO_BAD_ARG;
        Admission admission;
        if (!admission.ok()) return SCO_UNAVAILABLE;
        if (!IsMenuThread(*client)) return SCO_WRONG_THREAD;
        if (!panelId || !inoutBytes || std::strlen(panelId) > SCO_WIDGETS_MAX_ID ||
            (out && reinterpret_cast<uintptr_t>(out) % 8)) return SCO_BAD_ARG;
        Copied copied;
        if (const auto r = Snapshot(panelId, copied); r != Result::Ok) return static_cast<sco_result>(r);
        const std::vector<unsigned char> blob = Flatten(copied); // outside the widget lock
        const uint32_t capacity = *inoutBytes;
        *inoutBytes = static_cast<uint32_t>(blob.size());
        if (blob.size() > capacity) return SCO_TOO_MANY;
        if (!out) return SCO_BAD_ARG;
        std::memcpy(out, blob.data(), blob.size());
        return SCO_OK;
    });
}

sco_result SubmitEvent(sco_view_client* client, const char* panelId, const sco_view_event* event, uint64_t* seq) {
    return Guard([&] {
        if (!client) return SCO_BAD_ARG;
        Admission admission;
        if (!admission.ok()) return SCO_UNAVAILABLE;
        if (!IsMenuThread(*client)) return SCO_WRONG_THREAD;
        if (!panelId || !event || std::strlen(panelId) > SCO_WIDGETS_MAX_ID) return SCO_BAD_ARG;
        if (event->size < sizeof(sco_view_event) || event->_reserved || !event->epoch || !event->node_key ||
            !std::isfinite(event->value)) return SCO_BAD_ARG;
        const uint32_t kind = event->kind;
        if (!((kind >= SCO_WIDGET_BUTTON && kind <= SCO_WIDGET_TEXT_INPUT) || kind == SCO_WIDGET_EVENT_PAGE))
            return SCO_BAD_ARG;
        std::string text;
        if (event->text) {
            const size_t length = strnlen(event->text, SCO_WIDGETS_MAX_TEXT + 1);
            if (length > SCO_WIDGETS_MAX_TEXT) return SCO_BAD_ARG;
            text.assign(event->text, length);
            if (!ValidUtf8(text)) return SCO_BAD_ARG;
        }
        // Read before Exists: a Stop or Start in between leaves the slot with an older session.
        const uint64_t session = Session();
        if (const auto r = Exists(panelId); r != Result::Ok) return static_cast<sco_result>(r);
        sco_view_client::Slot slot;
        slot.session = session;
        slot.panel = panelId;
        slot.nodeId = event->node_id; slot.kind = kind; slot.index = event->index; slot.value = event->value;
        slot.text = std::move(text); slot.row = event->row; slot.epoch = event->epoch; slot.key = event->node_key;
        std::lock_guard<std::mutex> hold(client->queueLock);
        const uint64_t number = client->nextSeq;
        if (Mergeable(kind) && !client->slots.empty()) {
            auto& tail = client->slots.back();
            if (Mergeable(tail.kind) && tail.kind == kind && tail.nodeId == slot.nodeId && tail.epoch == slot.epoch &&
                tail.key == slot.key && tail.panel == slot.panel && tail.session == slot.session) {
                tail.index = slot.index; tail.value = slot.value; tail.row = slot.row;
                tail.text = std::move(slot.text);
                tail.last = number;
                client->nextSeq = number + 1;
                if (seq) *seq = number;
                return SCO_OK;
            }
        }
        if (client->slots.size() >= kQueueSlots) return SCO_TOO_MANY;
        slot.first = slot.last = number;
        client->slots.push_back(std::move(slot));
        client->nextSeq = number + 1;
        if (seq) *seq = number;
        return SCO_OK;
    });
}

sco_result DispatchEvents(sco_view_client* client, uint32_t* dispatched, uint32_t* dropped) {
    return Guard([&] {
        if (!client) return SCO_BAD_ARG;
        Admission admission;
        if (!admission.ok()) return SCO_UNAVAILABLE;
        if (!OnGameThread()) return SCO_WRONG_THREAD;
        if (sco::detail::InCallout()) return SCO_FAILED;
        std::vector<sco_view_client::Slot> taken;
        {
            std::lock_guard<std::mutex> hold(client->queueLock);
            const size_t n = std::min<size_t>(client->slots.size(), SCO_WIDGETS_MAX_EVENTS);
            taken.reserve(n);
            for (size_t i = 0; i < n; ++i) {
                taken.push_back(std::move(client->slots.front()));
                client->slots.pop_front();
            }
        }
        uint32_t ok = 0, bad = 0;
        bool crashed = false, stopped = false;
        for (size_t at = 0; at < taken.size(); ++at) {
            auto& slot = taken[at];
            if (slot.session != Session()) {
                // Queued before a Stop or Start of sco.widgets: never delivered to a new panel.
                ++bad;
                client->eventDrops.fetch_add(1, std::memory_order_acq_rel);
                client->eventsDone.store(slot.last, std::memory_order_release);
                continue;
            }
            Event event;
            event.node_id = slot.nodeId; event.kind = slot.kind; event.index = slot.index;
            event.value = slot.value; event.text = slot.text; event.row = slot.row;
            const Step step = DispatchEvent(slot.panel, event, slot.epoch, slot.key);
            if (step == Step::Unavailable) {
                // Core stopped under the drain. The slot and the rest of the taken slots were
                // already removed from the queue and no callback ran for them. Count each as a
                // drop and move events_done past them, so a menu waiting on events_done does
                // not wait for ever and no event is lost without a trace.
                stopped = true;
                const size_t lost = taken.size() - at;
                bad += static_cast<uint32_t>(lost);
                client->eventDrops.fetch_add(lost, std::memory_order_acq_rel);
                client->eventsDone.store(taken.back().last, std::memory_order_release);
                break;
            }
            if (step == Step::Delivered) ++ok;
            else {
                ++bad;
                client->eventDrops.fetch_add(1, std::memory_order_acq_rel);
                if (step == Step::Crashed) crashed = true;
            }
            // After the slot's effect is in the panel revision, or after it was dropped.
            client->eventsDone.store(slot.last, std::memory_order_release);
        }
        if (dispatched) *dispatched = ok;
        if (dropped) *dropped = bad;
        if (stopped) return SCO_UNAVAILABLE;
        return crashed ? SCO_CRASHED : SCO_OK;
    });
}

const sco_widgets_view_v1 kTable{
    sizeof(sco_widgets_view_v1), SCO_WIDGETS_VIEW_VERSION_1_0, Capabilities, Stamps, MenuThreadEnter,
    MenuThreadLeave, ListPanels, CopyPanel, SubmitEvent, DispatchEvents};

} // namespace

const sco_widgets_view_v1* ViewTable() { return &kTable; }
sco_view_client* ViewClient() { return &Client(); }

bool ViewReleaseMenuThread() {
    auto& c = Client();
    std::lock_guard<std::mutex> hold(c.threadLock);
    if (!c.hasThread) return false;
    c.hasThread = false;
    c.thread = std::thread::id();
    return true;
}

} // namespace sco::widgets::viewimpl
