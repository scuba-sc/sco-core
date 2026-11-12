// SPDX-License-Identifier: GPL-3.0-only
// Renderer-neutral retained widgets for plugins. Header-only over sco_widgets.h.
#ifndef SCOSDK_WIDGETS_HPP
#define SCOSDK_WIDGETS_HPP
#include "plugin.hpp"
#include "sco_widgets.h"
#include <cstddef>

namespace sco::sdk {
namespace detail {
template <class T>
void WidgetEvent(const char* panel_id, const sco_widget_event* event, void* ctx) noexcept {
    try {
        static_cast<T*>(ctx)->OnEvent(panel_id, *event);
    } catch (...) {
        // Exceptions never cross the plugin C ABI.
    }
}
} // namespace detail
class Widgets {
public:
    sco_result Open(const Plugin& plugin) noexcept { return Open(plugin.Api(), plugin.Self()); }
    sco_result Open(const sco_api* api, sco_plugin* self) noexcept {
        table_ = nullptr;
        api_ = api;
        self_ = self;
        if (!api || !self) return SCO_BAD_ARG;
        if (api->size < offsetof(sco_api, query_service) + sizeof(api->query_service) ||
            !api->query_service) return SCO_UNAVAILABLE;
        const void* out = nullptr;
        const sco_result result = api->query_service(SCO_WIDGETS_NAME, SCO_WIDGETS_VERSION_1_0, &out);
        if (result != SCO_OK) return result;
        if (!out) return SCO_UNAVAILABLE;
        table_ = static_cast<const sco_widgets_v1*>(out);
        return SCO_OK;
    }
    explicit operator bool() const noexcept { return table_ != nullptr; }
    const sco_widgets_v1* Table() const noexcept { return table_; }

    // Strings/arrays are copied on success; fn/ctx (or obj) remain borrowed until
    // successful replacement/removal or unload. Calls are game-thread only.
    sco_result SetPanel(const char* id, const char* title, const sco_widget_node* nodes,
                        uint32_t count, sco_widget_event_fn fn = nullptr, void* ctx = nullptr,
                        uint32_t stride = sizeof(sco_widget_node)) noexcept {
        if (!table_ || table_->size < offsetof(sco_widgets_v1, set_panel) + sizeof(table_->set_panel) ||
            !table_->set_panel) return SCO_UNAVAILABLE;
        return table_->set_panel(self_, id, title, nodes, count, stride, fn, ctx);
    }
    // obj.OnEvent(const char* panel_id, const sco_widget_event& event).
    template <class T>
    sco_result SetPanel(const char* id, const char* title, const sco_widget_node* nodes,
                        uint32_t count, T& obj, uint32_t stride = sizeof(sco_widget_node)) noexcept {
        return SetPanel(id, title, nodes, count, &detail::WidgetEvent<T>, &obj, stride);
    }
    sco_result RemovePanel(const char* id) noexcept {
        if (!table_ || table_->size < offsetof(sco_widgets_v1, remove_panel) + sizeof(table_->remove_panel) ||
            !table_->remove_panel) return SCO_UNAVAILABLE;
        return table_->remove_panel(self_, id);
    }
    sco_result UpdateNodes(const char* id, const sco_widget_node* nodes, uint32_t count,
                           uint32_t stride = sizeof(sco_widget_node)) noexcept {
        if (!table_ || table_->size < offsetof(sco_widgets_v1, update_nodes) + sizeof(table_->update_nodes) ||
            !table_->update_nodes) return SCO_UNAVAILABLE;
        return table_->update_nodes(self_, id, nodes, count, stride);
    }
    sco_result RemoveNodes(const char* id, const uint32_t* ids, uint32_t count) noexcept {
        if (!table_ || table_->size < offsetof(sco_widgets_v1, remove_nodes) + sizeof(table_->remove_nodes) ||
            !table_->remove_nodes) return SCO_UNAVAILABLE;
        return table_->remove_nodes(self_, id, ids, count);
    }
    // caps[0] is the allocated element count (at least 2), replaced by the written count.
    sco_result Capabilities(uint32_t* caps) noexcept {
        if (!table_ || table_->size < offsetof(sco_widgets_v1, capabilities) + sizeof(table_->capabilities) ||
            !table_->capabilities) return SCO_UNAVAILABLE;
        return table_->capabilities(self_, caps);
    }
    // Explicit 1.3 entry points refuse old/short providers before submitting a tail.
    sco_result SetPanel13(const char* id, const char* title, const sco_widget_node_1_3* nodes,
                          uint32_t count, sco_widget_event_fn fn = nullptr, void* ctx = nullptr) noexcept {
        const sco_result supported = NumericPresentation();
        if (supported != SCO_OK) return supported;
        return SetPanel(id, title, nodes ? &nodes->base.base.base : nullptr, count, fn, ctx,
                        sizeof(sco_widget_node_1_3));
    }
    sco_result UpdateNodes13(const char* id, const sco_widget_node_1_3* nodes, uint32_t count) noexcept {
        const sco_result supported = NumericPresentation();
        if (supported != SCO_OK) return supported;
        return UpdateNodes(id, nodes ? &nodes->base.base.base : nullptr, count, sizeof(sco_widget_node_1_3));
    }
    // Explicit tagged-event routes require the 1.4 guarantee, even though node tags use 1.2 records.
    sco_result SetPanelTagged(const char* id, const char* title, const sco_widget_node_1_2* nodes,
                             uint32_t count, sco_widget_event_fn fn = nullptr, void* ctx = nullptr) noexcept {
        const sco_widgets_v1* tagged = nullptr;
        const sco_result supported = TaggedEvents(nodes, count, tagged);
        if (supported != SCO_OK) return supported;
        if (tagged->size < offsetof(sco_widgets_v1, set_panel) + sizeof(tagged->set_panel) || !tagged->set_panel)
            return SCO_UNAVAILABLE;
        return tagged->set_panel(self_, id, title, &nodes->base.base, count, sizeof(*nodes), fn, ctx);
    }
    template <class T>
    sco_result SetPanelTagged(const char* id, const char* title, const sco_widget_node_1_2* nodes,
                             uint32_t count, T& obj) noexcept {
        return SetPanelTagged(id, title, nodes, count, &detail::WidgetEvent<T>, &obj);
    }
    sco_result UpdateNodesTagged(const char* id, const sco_widget_node_1_2* nodes, uint32_t count) noexcept {
        const sco_widgets_v1* tagged = nullptr;
        const sco_result supported = TaggedEvents(nodes, count, tagged);
        if (supported != SCO_OK) return supported;
        if (tagged->size < offsetof(sco_widgets_v1, update_nodes) + sizeof(tagged->update_nodes) || !tagged->update_nodes)
            return SCO_UNAVAILABLE;
        return tagged->update_nodes(self_, id, &nodes->base.base, count, sizeof(*nodes));
    }
private:
    sco_result TaggedEvents(const sco_widget_node_1_2* nodes, uint32_t count,
                            const sco_widgets_v1*& tagged) noexcept {
        tagged = nullptr;
        if (!table_ || !api_ || api_->size < offsetof(sco_api, query_service) + sizeof(api_->query_service) ||
            !api_->query_service) return SCO_UNAVAILABLE;
        const void* out = nullptr;
        const sco_result queried = api_->query_service(SCO_WIDGETS_NAME, SCO_WIDGETS_VERSION_1_4, &out);
        if (queried == SCO_NOT_FOUND || (queried == SCO_OK && !out)) return SCO_UNAVAILABLE;
        if (queried != SCO_OK) return queried;
        tagged = static_cast<const sco_widgets_v1*>(out);
        if (tagged->size < offsetof(sco_widgets_v1, capabilities) + sizeof(tagged->capabilities) || !tagged->capabilities)
            return SCO_UNAVAILABLE;
        uint32_t caps[SCO_WIDGETS_CAPS_1_2]{SCO_WIDGETS_CAPS_1_2};
        const sco_result r = tagged->capabilities(self_, caps);
        if (r != SCO_OK) return r;
        constexpr uint32_t required = SCO_WIDGETS_FEATURE_STABLE_TAGS | SCO_WIDGETS_FEATURE_EVENT_TAG;
        if (caps[0] != SCO_WIDGETS_CAPS_1_2 || (caps[SCO_WIDGETS_CAP_FEATURES] & required) != required ||
            !caps[SCO_WIDGETS_CAP_MAX_TAG]) return SCO_UNAVAILABLE;
        if (!nodes || !count) return SCO_BAD_ARG;
        if (count > SCO_WIDGETS_MAX_NODES) return SCO_TOO_MANY;
        for (uint32_t i = 0; i < count; ++i) {
            if (nodes[i].base.base.size != sizeof(*nodes)) return SCO_BAD_ARG;
            const char* tag = nodes[i].tag;
            uint32_t length = 0;
            if (tag) while (*tag) {
                const unsigned char c = static_cast<unsigned char>(*tag++);
                if (++length > SCO_WIDGETS_MAX_TAG || length > caps[SCO_WIDGETS_CAP_MAX_TAG]) return SCO_TOO_MANY;
                if (!((c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z') || (c >= '0' && c <= '9') ||
                    c == '.' || c == '_' || c == '-')) return SCO_BAD_ARG;
            }
        }
        return SCO_OK;
    }
    sco_result NumericPresentation() noexcept {
        uint32_t caps[SCO_WIDGETS_CAPS_1_2]{SCO_WIDGETS_CAPS_1_2};
        const sco_result result = Capabilities(caps);
        if (result != SCO_OK) return result;
        return caps[0] >= 2 && caps[0] <= SCO_WIDGETS_CAPS_1_2 &&
               (caps[SCO_WIDGETS_CAP_FEATURES] & SCO_WIDGETS_FEATURE_NUMERIC_PRESENTATION)
                   ? SCO_OK : SCO_UNAVAILABLE;
    }
    const sco_widgets_v1* table_ = nullptr;
    const sco_api* api_ = nullptr;
    sco_plugin* self_ = nullptr;
};
} // namespace sco::sdk
#endif
