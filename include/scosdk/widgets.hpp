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
private:
    const sco_widgets_v1* table_ = nullptr;
    sco_plugin* self_ = nullptr;
};
} // namespace sco::sdk
#endif
