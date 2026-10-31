// SPDX-License-Identifier: GPL-3.0-only
#include "sco/widgets.h"
#include "sco/host.h"
#include "../api/internal.h"
#include "../api/owned_publication.h"
#include <algorithm>
#include <functional>
#include <iterator>
#include <memory>
#include <mutex>
#include <utility>
#include <cmath>
#include <cstring>
#include <limits>

namespace sco::widgets {
namespace {
struct Entry {
    Panel panel;
    sco_plugin* owner = nullptr;
    sco_widget_event_fn fn = nullptr;
    void* ctx = nullptr;
    uint64_t generation = 0;
    // Shared with in-flight render passes; rebuilt lazily by RenderPanel, at most
    // once per pass, only after a mutation marked the panel dirty (coalescing).
    std::shared_ptr<const Panel> snapshot;
    bool dirty = true;
    bool extended = false; // holds a 1.1-sized node: events are sco_widget_event_1_1
};
std::mutex g_lock;
bool g_started = false;
uint64_t g_generation = 0;
uint64_t g_snapshot_builds = 0;
std::vector<Entry> g_panels;
constexpr uint32_t kFeatures = SCO_WIDGETS_FEATURE_UPDATE_NODES | SCO_WIDGETS_FEATURE_REMOVE_NODES |
    SCO_WIDGETS_FEATURE_SEVERITY | SCO_WIDGETS_FEATURE_TOOLTIP | SCO_WIDGETS_FEATURE_PAGING |
    SCO_WIDGETS_FEATURE_EVENT_ROW | SCO_WIDGETS_FEATURE_COALESCED_RENDER | SCO_WIDGETS_FEATURE_STABLE_TAGS |
    SCO_WIDGETS_FEATURE_NUMERIC_PRESENTATION;
auto Find(const std::string& id) {
    return std::find_if(g_panels.begin(), g_panels.end(), [&](const Entry& e) { return e.panel.id == id; });
}
Result Live(sco_plugin* self) {
    if (!g_started || detail::RuntimeStopping()) return Result::Unavailable;
    if (!host::PluginId(self) || detail::Released(self)) return Result::BadArg;
    return Result::Ok;
}
template<class F> sco_result Guard(F&& fn) noexcept {
    try { return static_cast<sco_result>(fn()); } catch (...) { return SCO_TOO_MANY; }
}
// Reject overlong encodings, surrogates and code points outside Unicode.
bool Utf8(const std::string& s) {
    for (size_t i = 0; i < s.size();) {
        uint32_t c = static_cast<unsigned char>(s[i++]);
        if (c < 0x80) continue;
        unsigned count; uint32_t minimum;
        if (c >= 0xc2 && c <= 0xdf) { count = 1; minimum = 0x80; c &= 0x1f; }
        else if (c >= 0xe0 && c <= 0xef) { count = 2; minimum = 0x800; c &= 0x0f; }
        else if (c >= 0xf0 && c <= 0xf4) { count = 3; minimum = 0x10000; c &= 7; }
        else return false;
        if (s.size() - i < count) return false;
        while (count--) {
            const auto next = static_cast<unsigned char>(s[i++]);
            if ((next & 0xc0) != 0x80) return false;
            c = (c << 6) | (next & 0x3f);
        }
        if (c < minimum || c > 0x10ffff || (c >= 0xd800 && c <= 0xdfff)) return false;
    }
    return true;
}
Result Copy(const char* s, size_t limit, std::string& out, bool optional = false) {
    if (!s) return optional ? Result::Ok : Result::BadArg;
    const size_t n = strnlen(s, limit + 1);
    if (n > limit) return Result::TooMany;
    out.assign(s, n);
    return Utf8(out) ? Result::Ok : Result::BadArg;
}
bool OwnId(const std::string& id, const char* owner) {
    const size_t n = std::strlen(owner);
    if (id.size() <= n + 1 || id.compare(0, n, owner) || id[n] != '.' || id.back() == '.') return false;
    for (size_t i = 0; i < id.size(); ++i) {
        const char c = id[i];
        if (c == '.') { if (!i || id[i - 1] == '.') return false; }
        else if (!(c >= 'a' && c <= 'z') && !(c >= '0' && c <= '9') && c != '_') return false;
    }
    return true;
}
// LIST rows are items; TABLE rows are complete row-major cell rows.
size_t Rows(const Node& n) {
    if (n.kind == SCO_WIDGET_TABLE) return n.columns ? n.items.size() / n.columns : 0;
    return n.kind == SCO_WIDGET_LIST ? n.items.size() : 0;
}
Result CopyNode(const sco_widget_node& n, Node& out) {
    if (n.kind < SCO_WIDGET_LABEL || n.kind > SCO_WIDGET_COLUMN || !n.id || (n.flags & ~(SCO_WIDGET_DISABLED | SCO_WIDGET_PASSWORD)) ||
        ((n.flags & SCO_WIDGET_PASSWORD) && n.kind != SCO_WIDGET_TEXT_INPUT)) return Result::BadArg;
    if (!std::isfinite(n.value) || !std::isfinite(n.minimum) || !std::isfinite(n.maximum)) return Result::BadArg;
    const bool numeric = n.kind == SCO_WIDGET_TOGGLE || n.kind == SCO_WIDGET_SLIDER;
    const bool selection = n.kind == SCO_WIDGET_COMBO || n.kind == SCO_WIDGET_LIST;
    const bool items = selection || n.kind == SCO_WIDGET_TABLE;
    if ((!numeric && n.value != 0) || (n.kind != SCO_WIDGET_SLIDER && (n.minimum != 0 || n.maximum != 0)) ||
        (!selection && n.index != 0 && n.index != -1) || (n.kind != SCO_WIDGET_TEXT_INPUT && n.text) ||
        (!items && (n.items || n.item_count)) || (n.kind != SCO_WIDGET_TABLE && n.columns)) return Result::BadArg;
    if (n.kind == SCO_WIDGET_TOGGLE && n.value != 0 && n.value != 1) return Result::BadArg;
    if (n.kind == SCO_WIDGET_SLIDER && (n.minimum > n.maximum || n.value < n.minimum || n.value > n.maximum)) return Result::BadArg;
    if (n.item_count > SCO_WIDGETS_MAX_ITEMS || n.columns > SCO_WIDGETS_MAX_COLUMNS) return Result::TooMany;
    if ((n.item_count && !n.items) || (!n.item_count && n.items)) return Result::BadArg;
    if (selection && (n.index < -1 || (n.index >= 0 && static_cast<uint32_t>(n.index) >= n.item_count))) return Result::BadArg;
    if (n.kind == SCO_WIDGET_TABLE && (!n.columns || n.item_count % n.columns)) return Result::BadArg;
    if (auto r = Copy(n.label, SCO_WIDGETS_MAX_TEXT, out.label, true); r != Result::Ok) return r;
    if (n.kind >= SCO_WIDGET_BUTTON && n.kind <= SCO_WIDGET_TEXT_INPUT && out.label.empty()) return Result::BadArg;
    if (auto r = Copy(n.text, SCO_WIDGETS_MAX_TEXT, out.text, true); r != Result::Ok) return r;
    out.kind = n.kind; out.id = n.id; out.parent = n.parent; out.flags = n.flags; out.index = n.index;
    out.value = n.value; out.minimum = n.minimum; out.maximum = n.maximum; out.columns = n.columns;
    out.items.reserve(n.item_count);
    for (uint32_t i = 0; i < n.item_count; ++i) {
        std::string text;
        if (auto r = Copy(n.items[i], SCO_WIDGETS_MAX_TEXT, text); r != Result::Ok) return r;
        out.items.push_back(std::move(text));
    }
    return Result::Ok;
}
// 1.1 node tail, after CopyNode filled the 1.0 part of out.
Result CopyTail(const sco_widget_node_1_1& x, Node& out) {
    if (x.severity > SCO_WIDGET_SEVERITY_ERROR || x._reserved) return Result::BadArg;
    const bool pageable = out.kind == SCO_WIDGET_LIST || out.kind == SCO_WIDGET_TABLE;
    if ((!pageable && (x.first_row || x.visible_rows)) || (!x.visible_rows && x.first_row)) return Result::BadArg;
    if (x.visible_rows > SCO_WIDGETS_MAX_ITEMS) return Result::TooMany;
    if (x.first_row && x.first_row >= Rows(out)) return Result::BadArg;
    if (auto r = Copy(x.tooltip, SCO_WIDGETS_MAX_TEXT, out.tooltip, true); r != Result::Ok) return r;
    out.severity = x.severity; out.first_row = x.first_row; out.visible_rows = x.visible_rows;
    out.extended = true;
    return Result::Ok;
}
// 1.2 node tail, after CopyTail filled the 1.1 part of out.
Result CopyTail12(const sco_widget_node_1_2& x, Node& out) {
    for (const auto pad : x._reserved) if (pad) return Result::BadArg;
    out.tag.clear();
    if (x.tag && *x.tag) {
        const std::string_view tag(x.tag);
        if (tag.size() > SCO_WIDGETS_MAX_TAG) return Result::BadArg;
        for (const unsigned char c : tag)
            if (!((c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9') ||
                  c == '.' || c == '_' || c == '-')) return Result::BadArg;
        out.tag = tag;
    }
    return Result::Ok;
}
Result CopyTail13(const sco_widget_node_1_3& x, Node& out) {
    for (const auto pad : x._reserved) if (pad) return Result::BadArg;
    if (x.presentation > SCO_WIDGET_NUMERIC_INTEGER_PERCENT ||
        (out.kind != SCO_WIDGET_SLIDER && x.presentation != SCO_WIDGET_NUMERIC_NUMBER))
        return Result::BadArg;
    if (x.presentation != SCO_WIDGET_NUMERIC_NUMBER) {
        const auto integer = [](double value) {
            return value >= std::numeric_limits<int32_t>::min() &&
                   value <= std::numeric_limits<int32_t>::max() && std::trunc(value) == value;
        };
        if (!integer(out.minimum) || !integer(out.maximum) || !integer(out.value)) return Result::BadArg;
    }
    out.presentation = x.presentation;
    return Result::Ok;
}
// True when every non-empty tag appears once. tree keeps [0, begin) and [end, size);
// fragment supplies the rest (Set passes the whole tree with begin == end == 0).
bool TagsUnique(const std::vector<Node>& tree, size_t begin, size_t end, const std::vector<Node>& fragment) {
    std::vector<std::string> tags;
    for (size_t i = 0; i < tree.size(); ++i) {
        if (i >= begin && i < end) continue;
        if (!tree[i].tag.empty()) tags.push_back(tree[i].tag);
    }
    for (const auto& node : fragment) if (!node.tag.empty()) tags.push_back(node.tag);
    std::sort(tags.begin(), tags.end());
    return std::adjacent_find(tags.begin(), tags.end()) == tags.end();
}
Result CheckArray(const sco_widget_node* nodes, uint32_t count, uint32_t stride) {
    if (!nodes || !count || stride < sizeof(sco_widget_node) || stride % alignof(sco_widget_node) ||
        reinterpret_cast<uintptr_t>(nodes) % alignof(sco_widget_node) ||
        count > std::numeric_limits<size_t>::max() / stride) return Result::BadArg;
    if (count > SCO_WIDGETS_MAX_NODES) return Result::TooMany;
    return Result::Ok;
}
// Copies count preorder nodes. ancestors holds the parents a node may name (open
// ROW/COLUMN chain; front = the fragment's own parent). taken() rejects IDs used
// outside the fragment. single_root: only nodes[0] may name ancestors.front().
Result ParseNodes(const sco_widget_node* nodes, uint32_t count, uint32_t stride, std::vector<uint32_t> ancestors,
                  bool single_root, const std::function<bool(uint32_t)>& taken, std::vector<Node>& out) {
    out.reserve(count);
    for (uint32_t i = 0; i < count; ++i) {
        const char* p = reinterpret_cast<const char*>(nodes) + static_cast<size_t>(i) * stride;
        uint32_t size; std::memcpy(&size, p, sizeof(size));
        if (size < sizeof(sco_widget_node) || size > stride) return Result::BadArg;
        sco_widget_node n; std::memcpy(&n, p, sizeof(n));
        if (std::any_of(out.begin(), out.end(), [&](const Node& x) { return x.id == n.id; }) || (taken && taken(n.id)))
            return Result::BadArg;
        const auto parent = std::find(ancestors.begin(), ancestors.end(), n.parent);
        if (parent == ancestors.end() || (single_root && i && parent == ancestors.begin())) return Result::BadArg;
        ancestors.erase(parent + 1, ancestors.end());
        Node node;
        if (auto r = CopyNode(n, node); r != Result::Ok) return r;
        if (size >= sizeof(sco_widget_node_1_1)) {
            sco_widget_node_1_1 x; std::memcpy(&x, p, sizeof(x));
            if (auto r = CopyTail(x, node); r != Result::Ok) return r;
            if (size >= sizeof(sco_widget_node_1_2)) {
                sco_widget_node_1_2 y; std::memcpy(&y, p, sizeof(y));
                if (auto r = CopyTail12(y, node); r != Result::Ok) return r;
                if (size >= sizeof(sco_widget_node_1_3)) {
                    sco_widget_node_1_3 z; std::memcpy(&z, p, sizeof(z));
                    if (auto r = CopyTail13(z, node); r != Result::Ok) return r;
                }
            }
        }
        if (n.kind == SCO_WIDGET_ROW || n.kind == SCO_WIDGET_COLUMN) ancestors.push_back(n.id);
        out.push_back(std::move(node));
    }
    return Result::Ok;
}
// End of the contiguous preorder subtree rooted at nodes[index].
size_t SubtreeEnd(const std::vector<Node>& nodes, size_t index) {
    std::vector<uint32_t> open{nodes[index].id};
    size_t end = index + 1;
    for (; end < nodes.size(); ++end) {
        const auto parent = std::find(open.begin(), open.end(), nodes[end].parent);
        if (parent == open.end()) break;
        open.erase(parent + 1, open.end());
        open.push_back(nodes[end].id);
    }
    return end;
}
// A structural change: new generation (drops the current pass's remaining events),
// new render snapshot next pass, event shape recomputed.
void Changed(Entry& e) {
    e.generation = ++g_generation;
    e.dirty = true;
    e.extended = std::any_of(e.panel.nodes.begin(), e.panel.nodes.end(), [](const Node& n) { return n.extended; });
}
Result OwnPanel(sco_plugin* self, const char* id, std::vector<Entry>::iterator& out) {
    std::string key;
    if (Copy(id, SCO_WIDGETS_MAX_ID, key) != Result::Ok || !OwnId(key, host::PluginId(self))) return Result::BadArg;
    out = Find(key);
    if (out == g_panels.end() || out->owner != self) return Result::NotFound;
    return Result::Ok;
}
sco_result Set(sco_plugin* self, const char* id, const char* title, const sco_widget_node* nodes,
               uint32_t count, uint32_t stride, sco_widget_event_fn fn, void* ctx) {
    return Guard([&] {
        if (!OnGameThread()) return Result::WrongThread;
        std::lock_guard<std::mutex> hold(g_lock);
        if (auto r = Live(self); r != Result::Ok) return r;
        Entry e;
        const char* pid = host::PluginId(self);
        if (Copy(id, SCO_WIDGETS_MAX_ID, e.panel.id) != Result::Ok || !OwnId(e.panel.id, pid) ||
            Copy(title, SCO_WIDGETS_MAX_TITLE, e.panel.title) != Result::Ok || e.panel.title.empty()) return Result::BadArg;
        if (auto r = CheckArray(nodes, count, stride); r != Result::Ok) return r;
        const auto existing = Find(e.panel.id);
        if (existing != g_panels.end() && existing->owner != self) return Result::BadArg;
        if (existing == g_panels.end() && g_panels.size() >= SCO_WIDGETS_MAX_PANELS) return Result::TooMany;
        size_t total = count;
        for (const auto& panel : g_panels) if (panel.panel.id != e.panel.id) total += panel.panel.nodes.size();
        if (total > SCO_WIDGETS_MAX_TOTAL_NODES) return Result::TooMany;
        e.panel.owner = pid; e.owner = self; e.fn = fn; e.ctx = ctx;
        if (auto r = ParseNodes(nodes, count, stride, {0}, false, {}, e.panel.nodes); r != Result::Ok) return r;
        if (!TagsUnique(e.panel.nodes, 0, 0, {})) return Result::BadArg;
        Changed(e);
        auto it = Find(e.panel.id);
        if (it == g_panels.end()) g_panels.push_back(std::move(e)); else *it = std::move(e);
        return Result::Ok;
    });
}
sco_result Remove(sco_plugin* self, const char* id) {
    return Guard([&] {
        if (!OnGameThread()) return Result::WrongThread;
        std::lock_guard<std::mutex> hold(g_lock);
        if (auto r = Live(self); r != Result::Ok) return r;
        std::string key;
        if (Copy(id, SCO_WIDGETS_MAX_ID, key) != Result::Ok || !OwnId(key, host::PluginId(self))) return Result::BadArg;
        auto it = Find(key);
        if (it == g_panels.end() || it->owner != self) return Result::NotFound;
        g_panels.erase(it); return Result::Ok;
    });
}
sco_result UpdateNodes(sco_plugin* self, const char* id, const sco_widget_node* nodes, uint32_t count, uint32_t stride) {
    return Guard([&] {
        if (!OnGameThread()) return Result::WrongThread;
        std::lock_guard<std::mutex> hold(g_lock);
        if (auto r = Live(self); r != Result::Ok) return r;
        std::vector<Entry>::iterator it;
        if (auto r = OwnPanel(self, id, it); r != Result::Ok) return r;
        if (auto r = CheckArray(nodes, count, stride); r != Result::Ok) return r;
        uint32_t headSize; std::memcpy(&headSize, nodes, sizeof(headSize));
        if (headSize < sizeof(sco_widget_node) || headSize > stride) return Result::BadArg;
        sco_widget_node head; std::memcpy(&head, nodes, sizeof(head));
        auto& tree = it->panel.nodes;
        const auto byId = [&](uint32_t node) {
            return std::find_if(tree.begin(), tree.end(), [&](const Node& n) { return n.id == node; });
        };
        size_t begin = tree.size(), end = tree.size();
        if (const auto old = byId(head.id); head.id && old != tree.end()) {
            if (old->parent != head.parent) return Result::BadArg;
            begin = static_cast<size_t>(old - tree.begin());
            end = SubtreeEnd(tree, begin);
        } else if (head.parent) {
            const auto parent = byId(head.parent);
            if (parent == tree.end() || (parent->kind != SCO_WIDGET_ROW && parent->kind != SCO_WIDGET_COLUMN)) return Result::BadArg;
            begin = end = SubtreeEnd(tree, static_cast<size_t>(parent - tree.begin()));
        }
        const size_t total = tree.size() - (end - begin) + count;
        if (total > SCO_WIDGETS_MAX_NODES) return Result::TooMany;
        size_t live = total;
        for (const auto& panel : g_panels) if (&panel != &*it) live += panel.panel.nodes.size();
        if (live > SCO_WIDGETS_MAX_TOTAL_NODES) return Result::TooMany;
        const auto taken = [&](uint32_t node) {
            for (size_t i = 0; i < tree.size(); ++i) if ((i < begin || i >= end) && tree[i].id == node) return true;
            return false;
        };
        std::vector<Node> fragment;
        if (auto r = ParseNodes(nodes, count, stride, {head.parent}, true, taken, fragment); r != Result::Ok) return r;
        if (!TagsUnique(tree, begin, end, fragment)) return Result::BadArg;
        // Capacity first: the splice below then only moves nodes and cannot throw.
        tree.reserve(total);
        tree.erase(tree.begin() + static_cast<std::ptrdiff_t>(begin), tree.begin() + static_cast<std::ptrdiff_t>(end));
        tree.insert(tree.begin() + static_cast<std::ptrdiff_t>(begin),
                    std::make_move_iterator(fragment.begin()), std::make_move_iterator(fragment.end()));
        Changed(*it);
        return Result::Ok;
    });
}
sco_result RemoveNodes(sco_plugin* self, const char* id, const uint32_t* ids, uint32_t count) {
    return Guard([&] {
        if (!OnGameThread()) return Result::WrongThread;
        std::lock_guard<std::mutex> hold(g_lock);
        if (auto r = Live(self); r != Result::Ok) return r;
        std::vector<Entry>::iterator it;
        if (auto r = OwnPanel(self, id, it); r != Result::Ok) return r;
        if (!ids || !count) return Result::BadArg;
        if (count > SCO_WIDGETS_MAX_NODES) return Result::TooMany;
        auto& tree = it->panel.nodes;
        std::vector<char> drop(tree.size(), 0);
        for (uint32_t i = 0; i < count; ++i) if (!ids[i]) return Result::BadArg;
        for (uint32_t i = 0; i < count; ++i) {
            const auto node = std::find_if(tree.begin(), tree.end(), [&](const Node& n) { return n.id == ids[i]; });
            if (node == tree.end()) return Result::NotFound;
            const size_t begin = static_cast<size_t>(node - tree.begin());
            std::fill(drop.begin() + static_cast<std::ptrdiff_t>(begin),
                      drop.begin() + static_cast<std::ptrdiff_t>(SubtreeEnd(tree, begin)), char{1});
        }
        if (std::all_of(drop.begin(), drop.end(), [](char d) { return d != 0; })) return Result::BadArg;
        size_t kept = 0;
        for (size_t i = 0; i < tree.size(); ++i)
            if (!drop[i]) { if (kept != i) tree[kept] = std::move(tree[i]); ++kept; }
        tree.erase(tree.begin() + static_cast<std::ptrdiff_t>(kept), tree.end());
        Changed(*it);
        return Result::Ok;
    });
}
sco_result Capabilities(sco_plugin* self, uint32_t* caps) {
    return Guard([&] {
        if (!OnGameThread()) return Result::WrongThread;
        std::lock_guard<std::mutex> hold(g_lock);
        if (auto r = Live(self); r != Result::Ok) return r;
        if (!caps || caps[SCO_WIDGETS_CAP_COUNT] < 2) return Result::BadArg;
        const uint32_t known[SCO_WIDGETS_CAPS_1_2]{
            SCO_WIDGETS_CAPS_1_2, kFeatures, SCO_WIDGETS_MAX_PANELS, SCO_WIDGETS_MAX_NODES,
            SCO_WIDGETS_MAX_TOTAL_NODES, SCO_WIDGETS_MAX_ITEMS, SCO_WIDGETS_MAX_COLUMNS,
            SCO_WIDGETS_MAX_TEXT, SCO_WIDGETS_MAX_EVENTS, SCO_WIDGETS_MAX_TAG};
        const uint32_t n = std::min(caps[SCO_WIDGETS_CAP_COUNT], SCO_WIDGETS_CAPS_1_2);
        for (uint32_t i = 1; i < n; ++i) caps[i] = known[i];
        caps[SCO_WIDGETS_CAP_COUNT] = n;
        return Result::Ok;
    });
}
const sco_widgets_v1 kTable{sizeof(sco_widgets_v1), 0, Set, Remove, UpdateNodes, RemoveNodes, Capabilities};
void OnRelease(const void* owner) {
    std::lock_guard<std::mutex> hold(g_lock);
    g_panels.erase(std::remove_if(g_panels.begin(), g_panels.end(), [&](const Entry& e) { return e.owner == owner; }), g_panels.end());
}
struct EventCall {
    sco_widget_event_fn fn;
    const char* panel;
    const sco_widget_event* event;
    void* ctx;
};
void EventThunk(void* p) {
    const auto& c = *static_cast<EventCall*>(p);
    c.fn(c.panel, c.event, c.ctx);
}
bool Enabled(const Panel& panel, const Node& node) {
    const Node* current = &node;
    for (;;) {
        if (current->flags & SCO_WIDGET_DISABLED) return false;
        if (!current->parent) return true;
        const auto parent = std::find_if(panel.nodes.begin(), panel.nodes.end(), [&](const Node& n) { return n.id == current->parent; });
        if (parent == panel.nodes.end()) return false; // snapshots always hold a validated tree
        current = &*parent;
    }
}
bool ValidEvent(const Node& n, const Event& e) {
    if ((n.flags & SCO_WIDGET_DISABLED) || !std::isfinite(e.value)) return false;
    if (e.kind == SCO_WIDGET_EVENT_PAGE)
        return n.visible_rows && e.index == 0 && e.value == 0 && e.text.empty() &&
               e.row >= 0 && static_cast<size_t>(e.row) < Rows(n);
    if (e.kind != n.kind) return false;
    const bool numeric = e.kind == SCO_WIDGET_TOGGLE || e.kind == SCO_WIDGET_SLIDER;
    const bool selection = e.kind == SCO_WIDGET_COMBO || e.kind == SCO_WIDGET_LIST;
    if ((!numeric && e.value != 0) || (!selection && e.index != 0) ||
        (e.kind != SCO_WIDGET_TEXT_INPUT && !e.text.empty())) return false;
    switch (e.kind) {
        case SCO_WIDGET_BUTTON: return true;
        case SCO_WIDGET_TOGGLE: return e.value == 0 || e.value == 1;
        case SCO_WIDGET_SLIDER:
            return e.value >= n.minimum && e.value <= n.maximum &&
                   (n.presentation == SCO_WIDGET_NUMERIC_NUMBER || std::trunc(e.value) == e.value);
        case SCO_WIDGET_COMBO:
        case SCO_WIDGET_LIST: return e.index == -1 || (e.index >= 0 && static_cast<size_t>(e.index) < n.items.size());
        case SCO_WIDGET_TEXT_INPUT:
            return e.text.size() <= SCO_WIDGETS_MAX_TEXT && e.text.find('\0') == std::string::npos && Utf8(e.text);
        default: return false;
    }
}
} // namespace
static Result StartImpl(const sco::detail::StartupPublish* cap) {
    if (cap && !cap->valid()) return Result::Unavailable;
    if (!OnGameThread()) return Result::WrongThread;
    std::lock_guard<std::mutex> hold(g_lock);
    if (g_started) return Result::BadArg;
    if (detail::RuntimeStopping() && !(cap && cap->valid())) return Result::Unavailable;
    auto r = sco::detail::AddReleaseHookOwned(cap, OnRelease);
    if (r != Result::Ok) return r;
    r = sco::detail::PublishHostOwned(cap, SCO_WIDGETS_NAME, SCO_WIDGETS_VERSION_1_3, &kTable);
    if (r != Result::Ok) {
        if (!cap) sco::detail::RemoveReleaseHookOwned(cap, OnRelease);
        return r;
    }
    g_started = true; return Result::Ok;
}

Result Start() {
    sco::detail::RuntimeLease admission;
    if (!admission || admission.owned()) return Result::Unavailable;
    return StartImpl(nullptr);
}
namespace owned {
Result Start(const sco::detail::StartupPublish& cap) {
    try {
        const auto result = StartImpl(&cap);
        if (result != Result::Ok && cap.owner()) cap.owner()->fault();
        return result;
    } catch (...) {
        if (cap.owner()) cap.owner()->fault();
        return Result::Failed;
    }
}
}
void Stop() {
    sco::detail::RuntimeLease admission;
    if (!admission || admission.owned()) return;
    if (!OnGameThread()) return;
    std::lock_guard<std::mutex> hold(g_lock);
    if (!g_started) return;
    g_started = false; g_panels.clear();
    host::WithdrawHostService(SCO_WIDGETS_NAME); RemoveReleaseHook(OnRelease);
}
bool Started() { std::lock_guard<std::mutex> hold(g_lock); return g_started; }
const sco_widgets_v1* Table() {
    sco::detail::RuntimeLease admission;
    if (!admission || admission.startup() || (admission.owned() && sco::detail::RuntimeStopping())) return nullptr;
    return &kTable;
}
std::vector<Panel> Panels() {
    if (!OnGameThread()) return {};
    std::lock_guard<std::mutex> hold(g_lock);
    std::vector<Panel> out;
    if (g_started && !detail::RuntimeStopping())
        for (const auto& e : g_panels) if (!detail::Released(e.owner)) out.push_back(e.panel);
    return out;
}
bool OwnedBy(const sco_plugin* owner, const char* id) {
    if (!OnGameThread() || !owner || !id || !host::PluginId(owner)) return false;
    std::lock_guard<std::mutex> hold(g_lock);
    if (!g_started || detail::RuntimeStopping() || detail::Released(owner)) return false;
    const auto it = std::find_if(g_panels.begin(), g_panels.end(),
                                 [&](const Entry& e) { return e.panel.id == id; });
    return it != g_panels.end() && it->owner == owner;
}
uint64_t SnapshotBuilds() { std::lock_guard<std::mutex> hold(g_lock); return g_snapshot_builds; }
Result RenderPanel(const char* id, Renderer& renderer) {
    sco::detail::RuntimeLease admission;
    if (!admission || admission.startup() || (admission.owned() && sco::detail::RuntimeStopping())) return Result::Unavailable;
    if (!OnGameThread()) return Result::WrongThread;
    std::shared_ptr<const Panel> snapshot;
    sco_plugin* owner = nullptr;
    sco_widget_event_fn fn = nullptr;
    void* ctx = nullptr;
    uint64_t generation = 0;
    {
        std::lock_guard<std::mutex> hold(g_lock);
        if (!g_started || detail::RuntimeStopping()) return Result::Unavailable;
        if (!id) return Result::BadArg;
        auto it = Find(id); if (it == g_panels.end()) return Result::NotFound;
        if (detail::Released(it->owner)) return Result::Crashed;
        if (it->dirty || !it->snapshot) {
            it->snapshot = std::make_shared<const Panel>(it->panel);
            it->dirty = false;
            ++g_snapshot_builds;
        }
        snapshot = it->snapshot; owner = it->owner; fn = it->fn; ctx = it->ctx; generation = it->generation;
    }
    const std::string& panelId = snapshot->id;
    std::vector<Event> events;
    try { renderer.Render(*snapshot, events); } catch (...) { return Result::Failed; }
    const size_t count = std::min(events.size(), static_cast<size_t>(SCO_WIDGETS_MAX_EVENTS));
    for (size_t i = 0; i < count; ++i) {
        const Event& event = events[i];
        bool extended = false;
        int32_t row = -1;
        {
            std::lock_guard<std::mutex> hold(g_lock);
            if (!g_started || detail::RuntimeStopping()) return Result::Unavailable;
            if (detail::Released(owner)) return Result::Crashed;
            const auto live = Find(panelId);
            if (live == g_panels.end() || live->generation != generation) break;
            auto node = std::find_if(live->panel.nodes.begin(), live->panel.nodes.end(), [&](const Node& n) { return n.id == event.node_id; });
            if (node == live->panel.nodes.end() || !Enabled(live->panel, *node) || !ValidEvent(*node, event)) continue;
            if (event.kind == SCO_WIDGET_TOGGLE || event.kind == SCO_WIDGET_SLIDER) node->value = event.value;
            if (event.kind == SCO_WIDGET_COMBO || event.kind == SCO_WIDGET_LIST) node->index = event.index;
            if (event.kind == SCO_WIDGET_TEXT_INPUT) node->text = event.text;
            if (event.kind == SCO_WIDGET_EVENT_PAGE) { node->first_row = static_cast<uint32_t>(event.row); row = event.row; }
            if (event.kind == SCO_WIDGET_LIST) row = event.index;
            if (event.kind != SCO_WIDGET_BUTTON) live->dirty = true;
            extended = live->extended;
        }
        if (!fn) continue;
        sco_widget_event_1_1 wire{};
        wire.base = {sizeof(sco_widget_event), event.node_id, event.kind, event.index, event.value,
                     event.kind == SCO_WIDGET_TEXT_INPUT ? event.text.c_str() : nullptr};
        if (extended) { wire.base.size = sizeof(wire); wire.row = row; }
        EventCall call{fn, panelId.c_str(), &wire.base, ctx};
        if (!detail::Callout(owner, panelId.c_str(), EventThunk, &call)) return Result::Crashed;
    }
    return Result::Ok;
}
} // namespace sco::widgets
