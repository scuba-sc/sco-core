// SPDX-License-Identifier: GPL-3.0-only
#include "sco/widgets.h"
#include "sco/host.h"
#include "../api/internal.h"
#include <algorithm>
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
};
std::mutex g_lock;
bool g_started = false;
uint64_t g_generation = 0;
std::vector<Entry> g_panels;
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
Result CopyNode(const sco_widget_node& n, Node& out) {
    if (n.kind < SCO_WIDGET_LABEL || n.kind > SCO_WIDGET_COLUMN || !n.id || (n.flags & ~SCO_WIDGET_DISABLED)) return Result::BadArg;
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
        if (!nodes || !count || stride < sizeof(sco_widget_node) || stride % alignof(sco_widget_node) ||
            reinterpret_cast<uintptr_t>(nodes) % alignof(sco_widget_node) ||
            count > std::numeric_limits<size_t>::max() / stride) return Result::BadArg;
        if (count > SCO_WIDGETS_MAX_NODES) return Result::TooMany;
        const auto existing = Find(e.panel.id);
        if (existing != g_panels.end() && existing->owner != self) return Result::BadArg;
        if (existing == g_panels.end() && g_panels.size() >= SCO_WIDGETS_MAX_PANELS) return Result::TooMany;
        size_t total = count;
        for (const auto& panel : g_panels) if (panel.panel.id != e.panel.id) total += panel.panel.nodes.size();
        if (total > SCO_WIDGETS_MAX_TOTAL_NODES) return Result::TooMany;
        e.panel.owner = pid; e.owner = self; e.fn = fn; e.ctx = ctx;
        std::vector<uint32_t> ancestors{0};
        e.panel.nodes.reserve(count);
        for (uint32_t i = 0; i < count; ++i) {
            const char* p = reinterpret_cast<const char*>(nodes) + static_cast<size_t>(i) * stride;
            uint32_t size; std::memcpy(&size, p, sizeof(size));
            if (size < sizeof(sco_widget_node) || size > stride) return Result::BadArg;
            sco_widget_node n; std::memcpy(&n, p, sizeof(n));
            if (std::any_of(e.panel.nodes.begin(), e.panel.nodes.end(), [&](const Node& x) { return x.id == n.id; })) return Result::BadArg;
            const auto parent = std::find(ancestors.begin(), ancestors.end(), n.parent);
            if (parent == ancestors.end()) return Result::BadArg;
            ancestors.erase(parent + 1, ancestors.end());
            Node out;
            if (auto r = CopyNode(n, out); r != Result::Ok) return r;
            if (n.kind == SCO_WIDGET_ROW || n.kind == SCO_WIDGET_COLUMN) ancestors.push_back(n.id);
            e.panel.nodes.push_back(std::move(out));
        }
        e.generation = ++g_generation;
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
const sco_widgets_v1 kTable{sizeof(sco_widgets_v1), 0, Set, Remove};
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
    if (e.kind != n.kind || (n.flags & SCO_WIDGET_DISABLED) || !std::isfinite(e.value)) return false;
    const bool numeric = e.kind == SCO_WIDGET_TOGGLE || e.kind == SCO_WIDGET_SLIDER;
    const bool selection = e.kind == SCO_WIDGET_COMBO || e.kind == SCO_WIDGET_LIST;
    if ((!numeric && e.value != 0) || (!selection && e.index != 0) ||
        (e.kind != SCO_WIDGET_TEXT_INPUT && !e.text.empty())) return false;
    switch (e.kind) {
        case SCO_WIDGET_BUTTON: return true;
        case SCO_WIDGET_TOGGLE: return e.value == 0 || e.value == 1;
        case SCO_WIDGET_SLIDER: return e.value >= n.minimum && e.value <= n.maximum;
        case SCO_WIDGET_COMBO:
        case SCO_WIDGET_LIST: return e.index == -1 || (e.index >= 0 && static_cast<size_t>(e.index) < n.items.size());
        case SCO_WIDGET_TEXT_INPUT:
            return e.text.size() <= SCO_WIDGETS_MAX_TEXT && e.text.find('\0') == std::string::npos && Utf8(e.text);
        default: return false;
    }
}
} // namespace
Result Start() {
    if (!OnGameThread()) return Result::WrongThread;
    std::lock_guard<std::mutex> hold(g_lock);
    if (g_started) return Result::BadArg;
    if (detail::RuntimeStopping()) return Result::Unavailable;
    auto r = AddReleaseHook(OnRelease);
    if (r != Result::Ok) return r;
    r = host::ProvideHostService(SCO_WIDGETS_NAME, SCO_WIDGETS_VERSION_1_0, &kTable);
    if (r != Result::Ok) { RemoveReleaseHook(OnRelease); return r; }
    g_started = true; return Result::Ok;
}
void Stop() {
    if (!OnGameThread()) return;
    std::lock_guard<std::mutex> hold(g_lock);
    if (!g_started) return;
    g_started = false; g_panels.clear();
    host::WithdrawHostService(SCO_WIDGETS_NAME); RemoveReleaseHook(OnRelease);
}
bool Started() { std::lock_guard<std::mutex> hold(g_lock); return g_started; }
const sco_widgets_v1* Table() { return &kTable; }
std::vector<Panel> Panels() {
    if (!OnGameThread()) return {};
    std::lock_guard<std::mutex> hold(g_lock);
    std::vector<Panel> out;
    if (g_started && !detail::RuntimeStopping())
        for (const auto& e : g_panels) if (!detail::Released(e.owner)) out.push_back(e.panel);
    return out;
}
Result RenderPanel(const char* id, Renderer& renderer) {
    if (!OnGameThread()) return Result::WrongThread;
    Entry e;
    {
        std::lock_guard<std::mutex> hold(g_lock);
        if (!g_started || detail::RuntimeStopping()) return Result::Unavailable;
        if (!id) return Result::BadArg;
        auto it = Find(id); if (it == g_panels.end()) return Result::NotFound;
        if (detail::Released(it->owner)) return Result::Crashed;
        e = *it;
    }
    std::vector<Event> events;
    try { renderer.Render(e.panel, events); } catch (...) { return Result::Failed; }
    const size_t count = std::min(events.size(), static_cast<size_t>(SCO_WIDGETS_MAX_EVENTS));
    for (size_t i = 0; i < count; ++i) {
        const Event& event = events[i];
        {
            std::lock_guard<std::mutex> hold(g_lock);
            if (!g_started || detail::RuntimeStopping()) return Result::Unavailable;
            if (detail::Released(e.owner)) return Result::Crashed;
            const auto live = Find(e.panel.id);
            if (live == g_panels.end() || live->generation != e.generation) break;
            auto node = std::find_if(live->panel.nodes.begin(), live->panel.nodes.end(), [&](const Node& n) { return n.id == event.node_id; });
            if (node == live->panel.nodes.end() || !Enabled(live->panel, *node) || !ValidEvent(*node, event)) continue;
            if (event.kind == SCO_WIDGET_TOGGLE || event.kind == SCO_WIDGET_SLIDER) node->value = event.value;
            if (event.kind == SCO_WIDGET_COMBO || event.kind == SCO_WIDGET_LIST) node->index = event.index;
            if (event.kind == SCO_WIDGET_TEXT_INPUT) node->text = event.text;
        }
        if (!e.fn) continue;
        const sco_widget_event wire{sizeof(sco_widget_event), event.node_id, event.kind, event.index, event.value,
                                    event.kind == SCO_WIDGET_TEXT_INPUT ? event.text.c_str() : nullptr};
        EventCall call{e.fn, e.panel.id.c_str(), &wire, e.ctx};
        if (!detail::Callout(e.owner, e.panel.id.c_str(), EventThunk, &call)) return Result::Crashed;
    }
    return Result::Ok;
}
} // namespace sco::widgets
