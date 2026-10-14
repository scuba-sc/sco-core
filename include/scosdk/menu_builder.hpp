// SPDX-License-Identifier: GPL-3.0-only
// scosdk/menu_builder.hpp: declarative panels for the host service "sco.widgets" 1.0/1.1
// (sco_widgets.h). Header-only C++20, over scosdk/widgets.hpp.
//
//   static bool  g_god = false;
//   static float g_dist = 10.f;
//   static scosdk::PanelTree g_tree;
//
//   scosdk::PanelBuilder panel;
//   panel.Column([&] {
//       panel.Row([&] {
//           panel.Toggle("God", &g_god);
//           panel.Slider("Dist", &g_dist, 1.f, 50.f);
//           panel.Button("Spawn", [] { /* invoke a command */ });
//       });
//   });
//   g_tree = panel.Build();                       // g_tree.Result(), g_tree.Error()
//   if (g_tree) (void)g_tree.Submit(widgets, "my_plugin.main", "My plugin");
//
// Build() emits the flat preorder sco_widget_node array set_panel wants: ids are the 1-based
// preorder position, parent is the enclosing Row/Column (0 at the root), and the result is
// checked against every sco_widgets.h rule the host enforces (ValidateWidgetNodes), with the
// host's result code: SCO_BAD_ARG for malformed data, SCO_TOO_MANY for limits. A failed
// Build() returns an empty PanelTree carrying the first problem; nothing is thrown.
//
// Events dispatch back to the bindings: a toggle writes its bool, a slider its float/double, a
// combo/list its int index, a text input its std::string, then the optional callback runs. The
// tree's own nodes follow the event, so resubmitting the same tree shows the current values.
// Bound variables and the PanelTree must outlive the registered panel (until successful
// replacement/removal or unload), like any set_panel ctx. Moving a PanelTree keeps the
// registered context valid; a callback may replace, remove or destroy the tree. Everything is
// game-thread only; exceptions from callbacks are caught and dropped (they never cross the C
// ABI). Extended attributes opt into 1.1 nodes; Submit drops them on a 1.0 host.
// Reference: docs/ui.md. GPL-3.0, like sco-core.
#ifndef SCOSDK_MENU_BUILDER_HPP
#define SCOSDK_MENU_BUILDER_HPP

#include "widgets.hpp"

#include <concepts>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <functional>
#include <initializer_list>
#include <memory>
#include <ranges>
#include <string>
#include <string_view>
#include <type_traits>
#include <utility>
#include <vector>

namespace scosdk {

namespace detail {

// The host's rule: no overlong forms, surrogates or code points past U+10FFFF.
inline bool WidgetUtf8(std::string_view s) noexcept {
    for (std::size_t i = 0; i < s.size();) {
        uint32_t c = static_cast<unsigned char>(s[i++]);
        if (c < 0x80) continue;
        unsigned count;
        uint32_t minimum;
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

inline sco_result WidgetString(const char* s, bool optional, std::size_t& length) noexcept {
    length = 0;
    if (!s) return optional ? SCO_OK : SCO_BAD_ARG;
    while (length <= SCO_WIDGETS_MAX_TEXT && s[length]) ++length;
    if (length > SCO_WIDGETS_MAX_TEXT) return SCO_TOO_MANY;
    return WidgetUtf8(std::string_view(s, length)) ? SCO_OK : SCO_BAD_ARG;
}

inline bool WidgetContainer(uint32_t kind) noexcept { return kind == SCO_WIDGET_ROW || kind == SCO_WIDGET_COLUMN; }

inline sco_result CheckWidgetTail(const sco_widget_node_1_1& n, const char*& why) noexcept {
    auto fail = [&](sco_result r, const char* text) { why = text; return r; };
    if (n.severity > SCO_WIDGET_SEVERITY_ERROR || n._reserved)
        return fail(SCO_BAD_ARG, "unknown severity or nonzero reserved field");
    const bool pageable = n.base.kind == SCO_WIDGET_LIST || n.base.kind == SCO_WIDGET_TABLE;
    if ((!pageable && (n.first_row || n.visible_rows)) || (!n.visible_rows && n.first_row))
        return fail(SCO_BAD_ARG, "paging on another kind or first_row without visible_rows");
    if (n.visible_rows > SCO_WIDGETS_MAX_ITEMS) return fail(SCO_TOO_MANY, "more than 256 visible rows");
    const uint32_t rows = n.base.kind == SCO_WIDGET_TABLE ? n.base.item_count / n.base.columns : n.base.item_count;
    if (n.first_row && n.first_row >= rows) return fail(SCO_BAD_ARG, "first_row outside rows");
    std::size_t length = 0;
    if (const sco_result r = WidgetString(n.tooltip, true, length); r != SCO_OK)
        return fail(r, r == SCO_TOO_MANY ? "tooltip longer than 255 bytes" : "tooltip is not valid UTF-8");
    return SCO_OK;
}

// One node, in the order the host checks it, so the result codes agree.
inline sco_result CheckWidgetNode(const sco_widget_node& n, const char*& why) noexcept {
    auto fail = [&](sco_result r, const char* text) { why = text; return r; };
    if (n.kind < SCO_WIDGET_LABEL || n.kind > SCO_WIDGET_COLUMN) return fail(SCO_BAD_ARG, "unknown kind");
    if (!n.id) return fail(SCO_BAD_ARG, "id 0");
    if (n.flags & ~(SCO_WIDGET_DISABLED | SCO_WIDGET_PASSWORD)) return fail(SCO_BAD_ARG, "unknown flag");
    if ((n.flags & SCO_WIDGET_PASSWORD) && n.kind != SCO_WIDGET_TEXT_INPUT)
        return fail(SCO_BAD_ARG, "PASSWORD on a node that is not a text input");
    if (!std::isfinite(n.value) || !std::isfinite(n.minimum) || !std::isfinite(n.maximum))
        return fail(SCO_BAD_ARG, "value, minimum or maximum is not finite");
    const bool numeric = n.kind == SCO_WIDGET_TOGGLE || n.kind == SCO_WIDGET_SLIDER;
    const bool selection = n.kind == SCO_WIDGET_COMBO || n.kind == SCO_WIDGET_LIST;
    const bool items = selection || n.kind == SCO_WIDGET_TABLE;
    if ((!numeric && n.value != 0) || (n.kind != SCO_WIDGET_SLIDER && (n.minimum != 0 || n.maximum != 0)) ||
        (!selection && n.index != 0 && n.index != -1) || (n.kind != SCO_WIDGET_TEXT_INPUT && n.text) ||
        (!items && (n.items || n.item_count)) || (n.kind != SCO_WIDGET_TABLE && n.columns))
        return fail(SCO_BAD_ARG, "a field this kind does not use is not zero/NULL");
    if (n.kind == SCO_WIDGET_TOGGLE && n.value != 0 && n.value != 1) return fail(SCO_BAD_ARG, "toggle value is not 0 or 1");
    if (n.kind == SCO_WIDGET_SLIDER && (n.minimum > n.maximum || n.value < n.minimum || n.value > n.maximum))
        return fail(SCO_BAD_ARG, "slider value outside [minimum, maximum]");
    if (n.item_count > SCO_WIDGETS_MAX_ITEMS) return fail(SCO_TOO_MANY, "more than 256 items");
    if (n.columns > SCO_WIDGETS_MAX_COLUMNS) return fail(SCO_TOO_MANY, "more than 16 table columns");
    if ((n.item_count && !n.items) || (!n.item_count && n.items)) return fail(SCO_BAD_ARG, "items and item_count disagree");
    if (selection && (n.index < -1 || (n.index >= 0 && static_cast<uint32_t>(n.index) >= n.item_count)))
        return fail(SCO_BAD_ARG, "selection index is not -1 or a valid item");
    if (n.kind == SCO_WIDGET_TABLE && (!n.columns || n.item_count % n.columns))
        return fail(SCO_BAD_ARG, "table cells are not a whole number of rows");
    std::size_t length = 0;
    if (const sco_result r = WidgetString(n.label, true, length); r != SCO_OK)
        return fail(r, r == SCO_TOO_MANY ? "label longer than 255 bytes" : "label is not valid UTF-8");
    if (n.kind >= SCO_WIDGET_BUTTON && n.kind <= SCO_WIDGET_TEXT_INPUT && !length)
        return fail(SCO_BAD_ARG, "interactive node without a label");
    if (const sco_result r = WidgetString(n.text, true, length); r != SCO_OK)
        return fail(r, r == SCO_TOO_MANY ? "text longer than 255 bytes" : "text is not valid UTF-8");
    for (uint32_t i = 0; i < n.item_count; ++i)
        if (const sco_result r = WidgetString(n.items[i], false, length); r != SCO_OK)
            return fail(r, r == SCO_TOO_MANY ? "item longer than 255 bytes" : "item is NULL or not valid UTF-8");
    return SCO_OK;
}

// at: 1-based position of the offending node, 0 for the array itself.
inline sco_result CheckWidgetTree(const sco_widget_node* nodes, uint32_t count, uint32_t stride,
                                  const char*& why, uint32_t& at) noexcept {
    at = 0;
    if (!nodes || !count || stride < sizeof(sco_widget_node) || stride % alignof(sco_widget_node) ||
        reinterpret_cast<std::uintptr_t>(nodes) % alignof(sco_widget_node)) {
        why = "no nodes, or a misaligned/short stride";
        return SCO_BAD_ARG;
    }
    if (count > SCO_WIDGETS_MAX_NODES) {
        why = "more than 256 nodes";
        return SCO_TOO_MANY;
    }
    uint32_t ids[SCO_WIDGETS_MAX_NODES];
    uint32_t open[SCO_WIDGETS_MAX_NODES + 1] = {0};   // open[0] = 0, the root
    uint32_t depth = 1;
    const auto* bytes = reinterpret_cast<const unsigned char*>(nodes);
    for (uint32_t i = 0; i < count; ++i) {
        at = i + 1;
        const unsigned char* p = bytes + static_cast<std::size_t>(i) * stride;
        uint32_t size = 0;
        std::memcpy(&size, p, sizeof(size));
        if (size < sizeof(sco_widget_node) || size > stride) {
            why = "size does not cover the v1 node or exceeds the stride";
            return SCO_BAD_ARG;
        }
        sco_widget_node n;
        std::memcpy(&n, p, sizeof(n));
        for (uint32_t j = 0; j < i; ++j)
            if (ids[j] == n.id) {
                why = "duplicate id";
                return SCO_BAD_ARG;
            }
        uint32_t k = 0;
        while (k < depth && open[k] != n.parent) ++k;
        if (k == depth) {
            why = "parent is not 0 or an enclosing ROW/COLUMN (preorder, contiguous subtrees)";
            return SCO_BAD_ARG;
        }
        depth = k + 1;
        if (const sco_result r = CheckWidgetNode(n, why); r != SCO_OK) return r;
        if (size >= sizeof(sco_widget_node_1_1)) {
            sco_widget_node_1_1 extended;
            std::memcpy(&extended, p, sizeof(extended));
            if (const sco_result r = CheckWidgetTail(extended, why); r != SCO_OK) return r;
        }
        ids[i] = n.id;
        if (WidgetContainer(n.kind)) open[depth++] = n.id;
    }
    at = 0;
    return SCO_OK;
}

inline void WidgetWhy(std::string* out, const char* why, uint32_t at) noexcept {
    if (!out) return;
    try {
        *out = at ? "node " + std::to_string(at) + ": " + why : std::string(why);
    } catch (...) {
    }
}

struct PanelState {
    std::vector<sco_widget_node> nodes;
    std::vector<sco_widget_node_1_1> extended;
    std::vector<std::string> labels, texts, tooltips;
    std::vector<std::vector<std::string>> items;
    std::vector<std::vector<const char*>> item_ptrs;
    std::vector<std::function<void(const sco_widget_event&)>> apply;
};

inline bool WidgetEventValid(const PanelState& s, const sco_widget_event& e) noexcept {
    if (e.size < sizeof(sco_widget_event) || !e.node_id || e.node_id > s.nodes.size()) return false;
    const sco_widget_node& n = s.nodes[e.node_id - 1];
    if (n.id != e.node_id || !std::isfinite(e.value)) return false;
    for (const sco_widget_node* cur = &n;;) {   // disabled ancestors disable the subtree
        if (cur->flags & SCO_WIDGET_DISABLED) return false;
        if (!cur->parent) break;
        cur = &s.nodes[cur->parent - 1];
    }
    if (e.kind == SCO_WIDGET_EVENT_PAGE) {
        if (s.extended.empty() || e.size < sizeof(sco_widget_event_1_1) ||
            e.index || e.value != 0 || (e.text && *e.text)) return false;
        sco_widget_event_1_1 page;
        std::memcpy(&page, &e, sizeof(page));
        const uint32_t rows = n.kind == SCO_WIDGET_TABLE ? n.item_count / n.columns : n.item_count;
        return s.extended[e.node_id - 1].visible_rows && page.row >= 0 &&
               static_cast<uint32_t>(page.row) < rows;
    }
    if (e.kind != n.kind) return false;
    const bool numeric = e.kind == SCO_WIDGET_TOGGLE || e.kind == SCO_WIDGET_SLIDER;
    const bool selection = e.kind == SCO_WIDGET_COMBO || e.kind == SCO_WIDGET_LIST;
    if ((!numeric && e.value != 0) || (!selection && e.index != 0) ||
        (e.kind != SCO_WIDGET_TEXT_INPUT && e.text && *e.text)) return false;
    std::size_t length = 0;
    switch (e.kind) {
        case SCO_WIDGET_BUTTON: return true;
        case SCO_WIDGET_TOGGLE: return e.value == 0 || e.value == 1;
        case SCO_WIDGET_SLIDER: return e.value >= n.minimum && e.value <= n.maximum;
        case SCO_WIDGET_COMBO:
        case SCO_WIDGET_LIST: return e.index == -1 || (e.index >= 0 && static_cast<uint32_t>(e.index) < n.item_count);
        case SCO_WIDGET_TEXT_INPUT: return WidgetString(e.text, false, length) == SCO_OK;
        default: return false;
    }
}

// The callback runs last, from a local copy: it may replace, remove or destroy the tree.
inline bool DispatchWidgetEvent(PanelState* s, const sco_widget_event* e) noexcept {
    if (!s || !e || !WidgetEventValid(*s, *e)) return false;
    const std::size_t i = e->node_id - 1;
    try {
        if (e->kind == SCO_WIDGET_EVENT_PAGE) {
            sco_widget_event_1_1 page;
            std::memcpy(&page, e, sizeof(page));
            s->extended[i].first_row = static_cast<uint32_t>(page.row);
            return true;
        }
        sco_widget_node& n = s->nodes[i];
        if (n.kind == SCO_WIDGET_TOGGLE || n.kind == SCO_WIDGET_SLIDER) n.value = e->value;
        else if (n.kind == SCO_WIDGET_COMBO || n.kind == SCO_WIDGET_LIST) n.index = e->index;
        else if (n.kind == SCO_WIDGET_TEXT_INPUT) {
            s->texts[i].assign(e->text);
            n.text = s->texts[i].c_str();
        }
        if (!s->extended.empty()) {
            s->extended[i].base = n;
            s->extended[i].base.size = sizeof(sco_widget_node_1_1);
        }
        const auto apply = s->apply[i];
        if (apply) apply(*e);
    } catch (...) {
        // Exceptions never cross the plugin C ABI.
    }
    return true;
}

template <class R>
concept WidgetStrings = std::ranges::input_range<R> &&
                        std::convertible_to<std::ranges::range_reference_t<R>, std::string_view>;

}  // namespace detail

// Checks a node array against every sco_widgets.h rule set_panel enforces (tree shape, ids,
// kinds, unused fields, values, limits, UTF-8) and answers the host's code: SCO_OK, SCO_BAD_ARG
// or SCO_TOO_MANY. why, when given, names the first problem ("node 3: duplicate id"). Panel
// id/title, ownership and live-node quotas are the host's to check.
inline sco_result ValidateWidgetNodes(const sco_widget_node* nodes, uint32_t count,
                                      uint32_t stride = sizeof(sco_widget_node),
                                      std::string* why = nullptr) noexcept {
    const char* text = "";
    uint32_t at = 0;
    const sco_result r = detail::CheckWidgetTree(nodes, count, stride, text, at);
    if (r != SCO_OK) detail::WidgetWhy(why, text, at);
    return r;
}

// A built, validated panel: the node array plus the storage its pointers borrow and the
// bindings its events reach. Move-only; moving it keeps the registered context valid.
class PanelTree {
public:
    PanelTree() = default;
    PanelTree(PanelTree&&) noexcept = default;
    PanelTree& operator=(PanelTree&&) noexcept = default;
    PanelTree(const PanelTree&) = delete;
    PanelTree& operator=(const PanelTree&) = delete;

    explicit operator bool() const noexcept { return result_ == SCO_OK && state_; }
    // SCO_OK, or why Build() failed (SCO_BAD_ARG / SCO_TOO_MANY); Error() names the problem.
    sco_result Result() const noexcept { return result_; }
    const std::string& Error() const noexcept { return error_; }

    const sco_widget_node* Nodes() const noexcept {
        if (!state_) return nullptr;
        return state_->extended.empty() ? state_->nodes.data() : &state_->extended[0].base;
    }
    uint32_t Stride() const noexcept {
        return static_cast<uint32_t>(state_ && !state_->extended.empty() ?
            sizeof(sco_widget_node_1_1) : sizeof(sco_widget_node));
    }
    uint32_t Count() const noexcept { return state_ ? static_cast<uint32_t>(state_->nodes.size()) : 0; }
    // The node with this id (ids are 1-based preorder positions), or nullptr.
    const sco_widget_node* Find(uint32_t id) const noexcept {
        if (!state_ || !id || id > state_->nodes.size()) return nullptr;
        return state_->extended.empty() ? &state_->nodes[id - 1] : &state_->extended[id - 1].base;
    }

    // Applies one host event to its binding and callback. false: no such interactive node, it
    // is disabled, or the payload breaks the sco_widgets.h event rules (nothing is changed).
    bool Dispatch(const sco_widget_event& event) noexcept { return detail::DispatchWidgetEvent(state_.get(), &event); }

    // The C callback and context to pass to set_panel yourself; Submit does it for you.
    static void OnEvent(const char* panel_id, const sco_widget_event* event, void* ctx) noexcept {
        (void)panel_id;
        (void)detail::DispatchWidgetEvent(static_cast<detail::PanelState*>(ctx), event);
    }
    void* Context() const noexcept { return state_.get(); }

    // set_panel through an opened Widgets; returns the host's result unchanged.
    sco_result Submit(sco::sdk::Widgets& widgets, const char* id, const char* title) noexcept {
        if (!*this) return result_ != SCO_OK ? result_ : SCO_BAD_ARG;
        const sco_widgets_v1* table = widgets.Table();
        const bool extended = !state_->extended.empty() && table &&
            table->size >= offsetof(sco_widgets_v1, capabilities) + sizeof(table->capabilities) &&
            table->capabilities;
        return widgets.SetPanel(id, title, extended ? Nodes() : state_->nodes.data(), Count(),
                                &PanelTree::OnEvent, Context(),
                                extended ? Stride() : static_cast<uint32_t>(sizeof(sco_widget_node)));
    }

private:
    friend class PanelBuilder;
    std::unique_ptr<detail::PanelState> state_;
    sco_result result_ = SCO_BAD_ARG;
    std::string error_ = "not built";
};

class PanelBuilder;

// The node a builder call added: chain flags or 1.1 attributes. Id() is its node id (0 when the
// call failed and the builder recorded an error).
class WidgetRef {
public:
    WidgetRef() = default;
    WidgetRef& Disabled(bool on = true) noexcept { return Flag(SCO_WIDGET_DISABLED, on); }
    // TEXT_INPUT only (display masking, not secret storage); elsewhere Build() fails BAD_ARG.
    WidgetRef& Password(bool on = true) noexcept { return Flag(SCO_WIDGET_PASSWORD, on); }
    WidgetRef& Tooltip(const char* text) noexcept;
    WidgetRef& Severity(uint32_t severity) noexcept;
    WidgetRef& Paging(uint32_t first_row, uint32_t visible_rows) noexcept;
    uint32_t Id() const noexcept { return id_; }

private:
    friend class PanelBuilder;
    WidgetRef(PanelBuilder* b, uint32_t id) noexcept : b_(b), id_(id) {}
    WidgetRef& Flag(uint32_t flag, bool on) noexcept;
    PanelBuilder* b_ = nullptr;
    uint32_t id_ = 0;
};

// Records nodes from nested calls; Build() turns them into a PanelTree and resets the builder.
// Strings and bound values are read when the call is made. The first problem is kept and
// reported by Build(); later calls are still accepted so nested bodies run normally.
class PanelBuilder {
public:
    template <std::invocable F>
    WidgetRef Row(F&& body) { return Container(SCO_WIDGET_ROW, std::forward<F>(body)); }
    template <std::invocable F>
    WidgetRef Column(F&& body) { return Container(SCO_WIDGET_COLUMN, std::forward<F>(body)); }

    WidgetRef Label(std::string_view text) noexcept { return Add(SCO_WIDGET_LABEL, text); }

    WidgetRef Button(std::string_view label, std::function<void()> on_click = {}) noexcept {
        WidgetRef ref = Add(SCO_WIDGET_BUTTON, label);
        if (on_click) Bind(ref, [f = std::move(on_click)](const sco_widget_event&) { f(); });
        return ref;
    }

    WidgetRef Toggle(std::string_view label, bool* value, std::function<void(bool)> on_change = {}) noexcept {
        WidgetRef ref = Add(SCO_WIDGET_TOGGLE, label);
        if (!Bound(ref, value, "toggle without a bound bool")) return ref;
        At(ref).node.value = *value ? 1 : 0;
        Bind(ref, [value, f = std::move(on_change)](const sco_widget_event& e) {
            *value = e.value != 0;
            if (f) f(*value);
        });
        return ref;
    }

    template <std::floating_point T>
    WidgetRef Slider(std::string_view label, T* value, std::type_identity_t<T> minimum,
                     std::type_identity_t<T> maximum,
                     std::type_identity_t<std::function<void(T)>> on_change = {}) noexcept {
        WidgetRef ref = Add(SCO_WIDGET_SLIDER, label);
        if (!Bound(ref, value, "slider without a bound value")) return ref;
        sco_widget_node& n = At(ref).node;
        n.value = static_cast<double>(*value);
        n.minimum = static_cast<double>(minimum);
        n.maximum = static_cast<double>(maximum);
        Bind(ref, [value, f = std::move(on_change)](const sco_widget_event& e) {
            *value = static_cast<T>(e.value);
            if (f) f(*value);
        });
        return ref;
    }

    // index: -1 for no selection, otherwise a zero-based item.
    WidgetRef Combo(std::string_view label, int* index, std::initializer_list<std::string_view> items,
                    std::function<void(int)> on_change = {}) noexcept {
        return Select(SCO_WIDGET_COMBO, label, index, items, std::move(on_change));
    }
    template <detail::WidgetStrings R>
    WidgetRef Combo(std::string_view label, int* index, const R& items, std::function<void(int)> on_change = {}) noexcept {
        return Select(SCO_WIDGET_COMBO, label, index, items, std::move(on_change));
    }
    WidgetRef List(std::string_view label, int* index, std::initializer_list<std::string_view> items,
                   std::function<void(int)> on_change = {}) noexcept {
        return Select(SCO_WIDGET_LIST, label, index, items, std::move(on_change));
    }
    template <detail::WidgetStrings R>
    WidgetRef List(std::string_view label, int* index, const R& items, std::function<void(int)> on_change = {}) noexcept {
        return Select(SCO_WIDGET_LIST, label, index, items, std::move(on_change));
    }

    WidgetRef TextInput(std::string_view label, std::string* text,
                        std::function<void(std::string_view)> on_change = {}) noexcept {
        WidgetRef ref = Add(SCO_WIDGET_TEXT_INPUT, label);
        if (!Bound(ref, text, "text input without a bound std::string")) return ref;
        if (!Text(At(ref).text, *text)) return ref;
        Bind(ref, [text, f = std::move(on_change)](const sco_widget_event& e) {
            text->assign(e.text);
            if (f) f(*text);
        });
        return ref;
    }

    // Row-major cells; their count must be a multiple of columns (1..16).
    WidgetRef Table(std::string_view label, uint32_t columns, std::initializer_list<std::string_view> cells) noexcept {
        return Cells(label, columns, cells);
    }
    template <detail::WidgetStrings R>
    WidgetRef Table(std::string_view label, uint32_t columns, const R& cells) noexcept {
        return Cells(label, columns, cells);
    }

    // The validated tree, or an empty one whose Result()/Error() say why. Resets the builder,
    // except when called inside a Row/Column body (SCO_BAD_ARG; the outer build continues).
    PanelTree Build() noexcept {
        PanelTree out;
        if (depth_) {
            Failed(out, SCO_BAD_ARG, "Build() called inside a Row/Column body");
            return out;
        }
        if (result_ == SCO_OK && nodes_.empty()) Fail(SCO_BAD_ARG, "empty panel: set_panel takes 1..256 nodes");
        if (result_ != SCO_OK) {
            out.result_ = result_;
            out.error_ = std::move(error_);
            Reset();
            return out;
        }
        try {
            auto s = std::make_unique<detail::PanelState>();
            const std::size_t count = nodes_.size();
            s->nodes.resize(count);
            s->labels.resize(count);
            s->texts.resize(count);
            s->items.resize(count);
            s->item_ptrs.resize(count);
            s->apply.resize(count);
            if (extended_) {
                s->extended.resize(count);
                s->tooltips.resize(count);
            }
            for (std::size_t i = 0; i < count; ++i) {
                Pending& p = nodes_[i];
                s->nodes[i] = p.node;
                s->labels[i] = std::move(p.label);
                s->texts[i] = std::move(p.text);
                s->items[i] = std::move(p.items);
                s->apply[i] = std::move(p.apply);
                if (extended_) {
                    s->extended[i].severity = p.severity;
                    s->extended[i].first_row = p.first_row;
                    s->extended[i].visible_rows = p.visible_rows;
                    s->tooltips[i] = std::move(p.tooltip);
                }
                s->item_ptrs[i].reserve(s->items[i].size());
                for (const std::string& item : s->items[i]) s->item_ptrs[i].push_back(item.c_str());
            }
            // Pointers are taken only now, when no vector will grow again.
            for (std::size_t i = 0; i < count; ++i) {
                sco_widget_node& n = s->nodes[i];
                n.label = s->labels[i].empty() ? nullptr : s->labels[i].c_str();
                n.text = n.kind == SCO_WIDGET_TEXT_INPUT ? s->texts[i].c_str() : nullptr;
                n.items = s->item_ptrs[i].empty() ? nullptr : s->item_ptrs[i].data();
                n.item_count = static_cast<uint32_t>(s->item_ptrs[i].size());
                if (extended_) {
                    s->extended[i].base = n;
                    s->extended[i].base.size = sizeof(sco_widget_node_1_1);
                    s->extended[i].tooltip = s->tooltips[i].empty() ? nullptr : s->tooltips[i].c_str();
                }
            }
            std::string why;
            const sco_result r = ValidateWidgetNodes(extended_ ? &s->extended[0].base : s->nodes.data(),
                static_cast<uint32_t>(count),
                static_cast<uint32_t>(extended_ ? sizeof(sco_widget_node_1_1) : sizeof(sco_widget_node)), &why);
            Reset();
            if (r != SCO_OK) {
                out.result_ = r;
                out.error_ = std::move(why);
                return out;
            }
            out.state_ = std::move(s);
            out.result_ = SCO_OK;
            out.error_.clear();
        } catch (...) {
            Reset();
            Failed(out, SCO_TOO_MANY, "allocation failed");
        }
        return out;
    }

    // Drops everything recorded so far.
    void Reset() noexcept {
        nodes_.clear();
        extended_ = false;
        result_ = SCO_OK;
        error_.clear();
    }

private:
    friend class WidgetRef;
    struct Pending {
        sco_widget_node node{};
        std::string label, text, tooltip;
        uint32_t severity = 0, first_row = 0, visible_rows = 0;
        std::vector<std::string> items;
        std::function<void(const sco_widget_event&)> apply;
    };

    template <class F>
    WidgetRef Container(uint32_t kind, F&& body) {
        const WidgetRef ref = Add(kind, {});
        const bool opened = ref.id_ != 0;
        if (opened) open_[depth_++] = ref.id_;
        struct Close {
            PanelBuilder* b;
            bool on;
            ~Close() { if (on) --b->depth_; }
        } close{this, opened};
        std::invoke(std::forward<F>(body));   // exceptions propagate; the nesting is restored
        return ref;
    }

    WidgetRef Add(uint32_t kind, std::string_view label) noexcept {
        if (nodes_.size() >= SCO_WIDGETS_MAX_NODES) {
            Fail(SCO_TOO_MANY, "more than 256 nodes");
            return {};
        }
        try {
            nodes_.emplace_back();
        } catch (...) {
            Fail(SCO_TOO_MANY, "allocation failed");
            return {};
        }
        Pending& p = nodes_.back();
        p.node.size = sizeof(sco_widget_node);
        p.node.kind = kind;
        p.node.id = static_cast<uint32_t>(nodes_.size());
        p.node.parent = depth_ ? open_[depth_ - 1] : 0;
        const WidgetRef ref(this, p.node.id);
        Text(p.label, label);
        return ref;
    }

    Pending& At(const WidgetRef& ref) noexcept { return nodes_[ref.id_ - 1]; }

    bool Text(std::string& out, std::string_view in) noexcept {
        if (in.find('\0') != std::string_view::npos) {
            Fail(SCO_BAD_ARG, "string with an embedded NUL");
            return false;
        }
        try {
            out.assign(in);
        } catch (...) {
            Fail(SCO_TOO_MANY, "allocation failed");
            return false;
        }
        return true;
    }

    template <class P>
    bool Bound(const WidgetRef& ref, P* p, const char* why) noexcept {
        if (!ref.id_) return false;
        if (!p) {
            Fail(SCO_BAD_ARG, why, ref.id_);
            return false;
        }
        return true;
    }

    template <class F>
    void Bind(const WidgetRef& ref, F&& apply) noexcept {
        if (!ref.id_) return;
        try {
            At(ref).apply = std::forward<F>(apply);
        } catch (...) {
            Fail(SCO_TOO_MANY, "allocation failed");
        }
    }

    template <class R>
    bool Strings(const WidgetRef& ref, const R& in) noexcept {
        if (!ref.id_) return false;
        std::vector<std::string>& out = At(ref).items;
        try {
            for (auto&& s : in) {
                if (out.size() >= SCO_WIDGETS_MAX_ITEMS) {
                    Fail(SCO_TOO_MANY, "more than 256 items", ref.id_);
                    return false;
                }
                const std::string_view v(s);
                out.emplace_back();
                if (!Text(out.back(), v)) return false;
            }
        } catch (...) {
            Fail(SCO_TOO_MANY, "allocation failed");
            return false;
        }
        return true;
    }

    template <class R>
    WidgetRef Select(uint32_t kind, std::string_view label, int* index, const R& items,
                     std::function<void(int)> on_change) noexcept {
        WidgetRef ref = Add(kind, label);
        if (!Bound(ref, index, "combo/list without a bound int index")) return ref;
        if (!Strings(ref, items)) return ref;
        At(ref).node.index = static_cast<int32_t>(*index);
        Bind(ref, [index, f = std::move(on_change)](const sco_widget_event& e) {
            *index = e.index;
            if (f) f(*index);
        });
        return ref;
    }

    template <class R>
    WidgetRef Cells(std::string_view label, uint32_t columns, const R& cells) noexcept {
        WidgetRef ref = Add(SCO_WIDGET_TABLE, label);
        if (!ref.id_) return ref;
        At(ref).node.columns = columns;
        Strings(ref, cells);
        return ref;
    }

    void Fail(sco_result r, const char* why, uint32_t at = 0) noexcept {
        if (result_ != SCO_OK) return;
        result_ = r;
        detail::WidgetWhy(&error_, why, at);
    }
    static void Failed(PanelTree& out, sco_result r, const char* why) noexcept {
        out.result_ = r;
        detail::WidgetWhy(&out.error_, why, 0);
    }

    std::vector<Pending> nodes_;
    bool extended_ = false;
    uint32_t open_[SCO_WIDGETS_MAX_NODES] = {};
    uint32_t depth_ = 0;
    sco_result result_ = SCO_OK;
    std::string error_;
};

inline WidgetRef& WidgetRef::Flag(uint32_t flag, bool on) noexcept {
    if (b_ && id_ && id_ <= b_->nodes_.size()) {
        uint32_t& flags = b_->nodes_[id_ - 1].node.flags;
        flags = on ? (flags | flag) : (flags & ~flag);
    }
    return *this;
}

inline WidgetRef& WidgetRef::Tooltip(const char* text) noexcept {
    if (b_ && id_ && id_ <= b_->nodes_.size()) {
        b_->extended_ = true;
        std::size_t length = 0;
        const sco_result r = detail::WidgetString(text, true, length);
        if (r != SCO_OK) b_->Fail(r, r == SCO_TOO_MANY ? "tooltip longer than 255 bytes" : "tooltip is not valid UTF-8", id_);
        else b_->Text(b_->nodes_[id_ - 1].tooltip, text ? std::string_view(text, length) : std::string_view{});
    }
    return *this;
}

inline WidgetRef& WidgetRef::Severity(uint32_t severity) noexcept {
    if (b_ && id_ && id_ <= b_->nodes_.size()) {
        b_->extended_ = true;
        b_->nodes_[id_ - 1].severity = severity;
    }
    return *this;
}

inline WidgetRef& WidgetRef::Paging(uint32_t first_row, uint32_t visible_rows) noexcept {
    if (b_ && id_ && id_ <= b_->nodes_.size()) {
        b_->extended_ = true;
        b_->nodes_[id_ - 1].first_row = first_row;
        b_->nodes_[id_ - 1].visible_rows = visible_rows;
    }
    return *this;
}

}  // namespace scosdk

#endif  // SCOSDK_MENU_BUILDER_HPP
