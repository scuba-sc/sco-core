// SPDX-License-Identifier: GPL-3.0-only
#include "sco/widgets_imgui.h"
#include "imgui.h"
#include <cstring>
#include <utility>

namespace sco::widgets {
namespace {
// Keep ImGui's stacks balanced even if an event allocation throws.
struct EndScope {
    void (*end)();
    ~EndScope() { end(); }
};
void Plain(const std::string& text) {
    ImGui::TextUnformatted(text.data(), text.data() + text.size());
}
bool Utf8(const char* text) {
    const auto* p = reinterpret_cast<const unsigned char*>(text);
    while (*p) {
        uint32_t value = *p++;
        if (value < 0x80) continue;
        unsigned tail = 0;
        uint32_t minimum = 0;
        if (value >= 0xC2 && value <= 0xDF) { value &= 0x1F; tail = 1; minimum = 0x80; }
        else if (value >= 0xE0 && value <= 0xEF) { value &= 0x0F; tail = 2; minimum = 0x800; }
        else if (value >= 0xF0 && value <= 0xF4) { value &= 7; tail = 3; minimum = 0x10000; }
        else return false;
        while (tail--) {
            if ((*p & 0xC0) != 0x80) return false;
            value = (value << 6) | (*p++ & 0x3F);
        }
        if (value < minimum || value > 0x10FFFF || (value >= 0xD800 && value <= 0xDFFF)) return false;
    }
    return true;
}
int32_t Options(const Node& node) {
    int32_t index = node.index;
    for (size_t i = 0; i < node.items.size(); ++i) {
        ImGui::PushID(static_cast<int>(i));
        EndScope id{ImGui::PopID};
        ImGui::BeginGroup();
        EndScope group{ImGui::EndGroup};
        // User strings never become ImGui labels/IDs: ## and % remain literal.
        Plain(node.items[i]);
        ImGui::SameLine();
        if (ImGui::Selectable("Select", node.index == static_cast<int32_t>(i)))
            index = static_cast<int32_t>(i);
    }
    return index;
}
void RenderNode(const Panel& panel, size_t& cursor, std::vector<Event>& events, bool inherited_disabled) {
    const Node& node = panel.nodes[cursor++];
    ImGui::PushID(static_cast<int>(node.id));
    EndScope id{ImGui::PopID};
    ImGui::BeginGroup();
    EndScope group{ImGui::EndGroup};
    const bool disabled = inherited_disabled || (node.flags & SCO_WIDGET_DISABLED) != 0;
    ImGui::BeginDisabled(disabled);
    EndScope enabled{ImGui::EndDisabled};
    Plain(node.label);
    Event event;
    event.node_id = node.id;
    event.kind = node.kind;
    bool changed = false;
    switch (node.kind) {
    case SCO_WIDGET_LABEL:
        break;
    case SCO_WIDGET_BUTTON:
        changed = ImGui::Button("Activate");
        break;
    case SCO_WIDGET_TOGGLE: {
        bool value = node.value != 0;
        changed = ImGui::Checkbox("##value", &value);
        event.value = value ? 1 : 0;
        break;
    }
    case SCO_WIDGET_SLIDER: {
        double value = node.value;
        changed = ImGui::SliderScalar("##value", ImGuiDataType_Double, &value,
                                      &node.minimum, &node.maximum, "%.3f", ImGuiSliderFlags_AlwaysClamp);
        event.value = value;
        break;
    }
    case SCO_WIDGET_COMBO:
        if (node.index >= 0 && static_cast<size_t>(node.index) < node.items.size())
            Plain(node.items[static_cast<size_t>(node.index)]);
        if (ImGui::BeginCombo("##value", nullptr, ImGuiComboFlags_NoPreview)) {
            EndScope combo{ImGui::EndCombo};
            event.index = Options(node);
            changed = event.index != node.index;
        }
        break;
    case SCO_WIDGET_LIST:
        if (ImGui::BeginListBox("##value")) {
            EndScope list{ImGui::EndListBox};
            event.index = Options(node);
            changed = event.index != node.index;
        }
        break;
    case SCO_WIDGET_TEXT_INPUT: {
        // Never truncate a UTF-8 code point; the service already validates input.
        if (node.text.size() > SCO_WIDGETS_MAX_TEXT || !Utf8(node.text.c_str())) break;
        char buffer[SCO_WIDGETS_MAX_TEXT + 1]{};
        std::memcpy(buffer, node.text.data(), node.text.size());
        changed = ImGui::InputText("##value", buffer, sizeof(buffer),
            (node.flags & SCO_WIDGET_PASSWORD) ? ImGuiInputTextFlags_Password : 0);
        buffer[SCO_WIDGETS_MAX_TEXT] = '\0';
        if (changed && Utf8(buffer)) event.text = buffer;
        else changed = false;
        break;
    }
    case SCO_WIDGET_TABLE:
        if (node.columns && node.columns <= SCO_WIDGETS_MAX_COLUMNS &&
            ImGui::BeginTable("##value", static_cast<int>(node.columns), ImGuiTableFlags_Borders)) {
            EndScope table{ImGui::EndTable};
            for (size_t i = 0; i < node.items.size(); ++i) {
                if (i % node.columns == 0) ImGui::TableNextRow();
                ImGui::TableNextColumn();
                Plain(node.items[i]);
            }
        }
        break;
    case SCO_WIDGET_ROW:
    case SCO_WIDGET_COLUMN: {
        bool first = true;
        while (cursor < panel.nodes.size() && panel.nodes[cursor].parent == node.id) {
            if (!first && node.kind == SCO_WIDGET_ROW) ImGui::SameLine();
            RenderNode(panel, cursor, events, disabled);
            first = false;
        }
        break;
    }
    default:
        break;
    }
    if (changed && !disabled && events.size() < SCO_WIDGETS_MAX_EVENTS)
        events.push_back(std::move(event));
}
} // namespace
void ImGuiRenderer::Render(const Panel& panel, std::vector<Event>& events) {
    ImGui::PushID(panel.id.c_str());
    EndScope id{ImGui::PopID};
    Plain(panel.title);
    // The host provides a validated, contiguous preorder tree.
    size_t cursor = 0;
    while (cursor < panel.nodes.size()) RenderNode(panel, cursor, events, false);
}
} // namespace sco::widgets
