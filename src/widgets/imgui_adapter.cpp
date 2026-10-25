// SPDX-License-Identifier: GPL-3.0-only
#include "sco/widgets_imgui.h"
#include "imgui.h"
#include <algorithm>
#include <cstring>
#include <string>
#include <string_view>
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
size_t Rows(const Node& node) {
    if (node.kind == SCO_WIDGET_TABLE) return node.columns ? node.items.size() / node.columns : 0;
    return node.items.size();
}
// Rows [first, last) shown for a paged LIST/TABLE; every row without paging.
void Window(const Node& node, size_t& first, size_t& last) {
    const size_t rows = Rows(node);
    first = node.visible_rows ? std::min<size_t>(node.first_row, rows) : 0;
    last = node.visible_rows ? std::min<size_t>(rows, first + node.visible_rows) : rows;
}
// Previous/Next controls; at most one page request per node and pass.
bool Pager(const Node& node, int32_t& row) {
    const size_t rows = Rows(node);
    if (!node.visible_rows || rows <= node.visible_rows) return false;
    size_t first, last;
    Window(node, first, last);
    Plain("Rows " + std::to_string(first + 1) + "-" + std::to_string(last) + " of " + std::to_string(rows));
    bool pressed = false;
    {
        ImGui::BeginDisabled(first == 0);
        EndScope enabled{ImGui::EndDisabled};
        if (ImGui::Button("Previous")) {
            row = static_cast<int32_t>(first > node.visible_rows ? first - node.visible_rows : 0);
            pressed = true;
        }
    }
    ImGui::SameLine();
    {
        ImGui::BeginDisabled(last >= rows);
        EndScope enabled{ImGui::EndDisabled};
        if (ImGui::Button("Next") && !pressed) {
            row = static_cast<int32_t>(last);
            pressed = true;
        }
    }
    return pressed;
}
// Severity is spelled out as text as well as colored, so it never relies on color alone.
void Label(const Node& node, bool withLabel = true) {
    const char* name = nullptr;
    ImVec4 color;
    switch (node.severity) {
    case SCO_WIDGET_SEVERITY_INFO: name = "Info:"; color = ImVec4(0.55f, 0.75f, 1.0f, 1.0f); break;
    case SCO_WIDGET_SEVERITY_WARNING: name = "Warning:"; color = ImVec4(1.0f, 0.8f, 0.3f, 1.0f); break;
    case SCO_WIDGET_SEVERITY_ERROR: name = "Error:"; color = ImVec4(1.0f, 0.45f, 0.45f, 1.0f); break;
    default: if (withLabel) Plain(node.label); return;
    }
    ImGui::PushStyleColor(ImGuiCol_Text, color);
    EndScope style{[] { ImGui::PopStyleColor(); }};
    ImGui::TextUnformatted(name);
    if (withLabel) { ImGui::SameLine(); Plain(node.label); }
}
int32_t Options(const Node& node) {
    int32_t index = node.index;
    size_t first, last;
    Window(node, first, last);
    for (size_t i = first; i < last; ++i) {
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
    const bool disabled = inherited_disabled || (node.flags & SCO_WIDGET_DISABLED) != 0;
    Event event;
    event.node_id = node.id;
    event.kind = node.kind;
    Event page;
    page.node_id = node.id;
    page.kind = SCO_WIDGET_EVENT_PAGE;
    bool changed = false, paged = false;
    {
        ImGui::BeginGroup();
        EndScope group{ImGui::EndGroup};
        ImGui::BeginDisabled(disabled);
        EndScope enabled{ImGui::EndDisabled};
        Label(node, node.kind != SCO_WIDGET_BUTTON);
        switch (node.kind) {
        case SCO_WIDGET_LABEL:
            break;
        case SCO_WIDGET_BUTTON: {
            const std::string_view caption = node.label.empty() ? std::string_view("Activate") : std::string_view(node.label);
            const ImGuiStyle& style = ImGui::GetStyle();
            const ImVec2 pos = ImGui::GetCursorScreenPos();
            // Natural size, as ImGui::Button computes it. Never derived from the available width:
            // auto-resizing windows and popups would feed that back and shrink the button to 1 px.
            const ImVec2 text = ImGui::CalcTextSize(caption.data(), caption.data() + caption.size(), false);
            const float width = text.x + 2 * style.FramePadding.x;
            const float height = text.y + 2 * style.FramePadding.y;
            // The node ID remains the identity; plugin text never enters ImGui's ##/### parser.
            changed = ImGui::Button("##action", ImVec2(width, height));
            const ImVec2 textPos(pos.x + style.FramePadding.x, pos.y + style.FramePadding.y);
            const ImVec4 clip(pos.x, pos.y, pos.x + width, pos.y + height);
            ImGui::GetWindowDrawList()->AddText(nullptr, ImGui::GetFontSize(), textPos,
                ImGui::GetColorU32(ImGuiCol_Text), caption.data(), caption.data() + caption.size(), 0.0f, &clip);
            break;
        }
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
            paged = Pager(node, page.row);
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
                size_t first, last;
                Window(node, first, last);
                for (size_t i = first * node.columns; i < last * node.columns; ++i) {
                    if (i % node.columns == 0) ImGui::TableNextRow();
                    ImGui::TableNextColumn();
                    Plain(node.items[i]);
                }
            }
            paged = Pager(node, page.row);
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
    }
    if (!node.tooltip.empty() && ImGui::BeginItemTooltip()) {
        EndScope tooltip{ImGui::EndTooltip};
        Plain(node.tooltip);
    }
    if (disabled) return;
    if (changed && events.size() < SCO_WIDGETS_MAX_EVENTS) events.push_back(std::move(event));
    if (paged && events.size() < SCO_WIDGETS_MAX_EVENTS) events.push_back(std::move(page));
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
