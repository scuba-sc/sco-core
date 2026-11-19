// SPDX-License-Identifier: GPL-3.0-only
#include "sco/widgets_imgui.h"
#include "imgui.h"
#include "imgui_layout.h"
#include <algorithm>
#include <cmath>
#include <cstring>
#include <string>
#include <string_view>
#include <unordered_map>
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
const char* SeverityName(uint32_t severity, ImVec4& color) {
    switch (severity) {
    case SCO_WIDGET_SEVERITY_INFO: color = ImVec4(0.55f, 0.75f, 1.0f, 1.0f); return "Info:";
    case SCO_WIDGET_SEVERITY_WARNING: color = ImVec4(1.0f, 0.8f, 0.3f, 1.0f); return "Warning:";
    case SCO_WIDGET_SEVERITY_ERROR: color = ImVec4(1.0f, 0.45f, 0.45f, 1.0f); return "Error:";
    default: return nullptr;
    }
}
// The line above a node: its severity (if any) and its label. An empty label draws no text, so an
// unlabelled ROW, COLUMN or TABLE gets no blank line above it. A LABEL node's label is its whole
// content, so an empty LABEL keeps its (blank) line.
void Label(const Node& node, bool withLabel = true) {
    withLabel = withLabel && (!node.label.empty() || node.kind == SCO_WIDGET_LABEL);
    ImVec4 color;
    const char* name = SeverityName(node.severity, color);
    if (!name) { if (withLabel) Plain(node.label); return; }
    ImGui::PushStyleColor(ImGuiCol_Text, color);
    EndScope style{[] { ImGui::PopStyleColor(); }};
    ImGui::TextUnformatted(name);
    if (withLabel) { ImGui::SameLine(); Plain(node.label); }
}
float TextWidth(const std::string& text) {
    return ImGui::CalcTextSize(text.data(), text.data() + text.size(), false).x;
}
// Kinds drawn as "label left, control right" so a panel reads as an aligned form.
bool IsForm(uint32_t kind) {
    return kind == SCO_WIDGET_COMBO || kind == SCO_WIDGET_SLIDER || kind == SCO_WIDGET_TEXT_INPUT ||
           kind == SCO_WIDGET_TOGGLE;
}
// Space a form label needs: severity prefix (when set) plus the label text.
float LabelWidth(const Node& node) {
    ImVec4 color;
    const char* name = SeverityName(node.severity, color);
    float width = TextWidth(node.label);
    if (name) width += ImGui::CalcTextSize(name, nullptr, false).x + ImGui::GetStyle().ItemSpacing.x;
    return width;
}
// Tooltip with the full text of a truncated row or label; wrapped, so it stays on screen.
void FullTextTooltip(const std::string& text) {
    if (!ImGui::BeginItemTooltip()) return;
    EndScope tooltip{ImGui::EndTooltip};
    ImGui::PushTextWrapPos(ImGui::GetFontSize() * (layout::kTooltipWrap / layout::kReferenceFontSize));
    EndScope wrap{ImGui::PopTextWrapPos};
    Plain(std::string(layout::TooltipText(text)));
}
// Label cell of a form row, inside the column width shared by the siblings. The control follows on
// the same line at the column edge.
void FormLabel(const Node& node, float column) {
    const ImGuiStyle& style = ImGui::GetStyle();
    ImGui::AlignTextToFramePadding();
    float room = column;
    ImVec4 color;
    if (const char* name = SeverityName(node.severity, color)) {
        {
            ImGui::PushStyleColor(ImGuiCol_Text, color);
            EndScope colored{[] { ImGui::PopStyleColor(); }};
            ImGui::TextUnformatted(name);
        }
        room -= ImGui::CalcTextSize(name, nullptr, false).x + style.ItemSpacing.x;
        ImGui::SameLine();
    }
    bool truncated = false;
    Plain(layout::Ellipsize(node.label, room, TextWidth, &truncated));
    if (truncated) FullTextTooltip(node.label);
    ImGui::SameLine(column + style.ItemSpacing.x);
}
// "Title" of a COLUMN/ROW group. SeparatorText hides everything after "##" like any ImGui label, so
// plugin text containing that falls back to plain text over a separator and stays literal.
void SectionHeader(const std::string& label) {
    if (label.find("##") == std::string::npos) ImGui::SeparatorText(label.c_str());
    else { Plain(label); ImGui::Separator(); }
}
// One full-width selectable row. The text is drawn over it (never parsed as an ImGui label, so ##
// and % stay literal), cut with an ellipsis to the row, with the full text in a tooltip.
bool OptionRow(const std::string& text, bool selected, float width) {
    const ImGuiStyle& style = ImGui::GetStyle();
    const ImVec2 pos = ImGui::GetCursorScreenPos();
    const float pad = style.FramePadding.x;
    // A solid header color makes the selected row stand out from the hover tint.
    if (selected) {
        ImVec4 color = ImGui::GetStyleColorVec4(ImGuiCol_Header);
        color.w = std::max(color.w, 0.75f);
        ImGui::PushStyleColor(ImGuiCol_Header, color);
    }
    const bool clicked = ImGui::Selectable("##row", selected, 0, ImVec2(width, 0));
    if (selected) ImGui::PopStyleColor();
    // Rows scrolled out of view need no text; skipping them keeps a long list cheap.
    if (!ImGui::IsRectVisible(pos, ImVec2(pos.x + width, pos.y + ImGui::GetFontSize()))) return clicked;
    bool truncated = false;
    const std::string shown = layout::Ellipsize(text, width - 2 * pad, TextWidth, &truncated);
    if (truncated) FullTextTooltip(text);
    // The overlay is one line of the same height as the row, so the cursor ends where Selectable left it.
    ImGui::SetCursorScreenPos(ImVec2(pos.x + pad, pos.y));
    Plain(shown);
    return clicked;
}
int32_t Options(const Node& node, float width) {
    int32_t index = node.index;
    size_t first, last;
    Window(node, first, last);
    for (size_t i = first; i < last; ++i) {
        ImGui::PushID(static_cast<int>(i));
        EndScope id{ImGui::PopID};
        if (OptionRow(node.items[i], node.index == static_cast<int32_t>(i), width))
            index = static_cast<int32_t>(i);
    }
    return index;
}
// What the renderer needs to know about a container before drawing its children: how many there
// are, how many are COLUMNs and the widest label among the form controls.
struct Group {
    float widestLabel = 0;
    uint32_t children = 0, columns = 0;
};
struct Context {
    std::vector<Event>& events;
    std::unordered_map<uint32_t, Group> groups;
};
void RenderNode(const Panel& panel, size_t& cursor, Context& ctx, bool inherited_disabled) {
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
        const ImGuiStyle& style = ImGui::GetStyle();
        const float fontSize = ImGui::GetFontSize();
        // Measured before the label moves the cursor: the container's content width. The label column
        // is a share of it and the control fills what is left of the row.
        const float available = ImGui::GetContentRegionAvail().x;
        // A labelled form control puts its label in the column shared with its siblings and the
        // control on the same line; everything else keeps its label on a line of its own.
        float column = 0;
        if (IsForm(node.kind) && !node.label.empty()) {
            const auto siblings = ctx.groups.find(node.parent);
            column = layout::LabelColumnWidth(siblings == ctx.groups.end() ? 0.0f : siblings->second.widestLabel, available, fontSize);
            FormLabel(node, column);
        } else if ((node.kind == SCO_WIDGET_ROW || node.kind == SCO_WIDGET_COLUMN) && !node.label.empty() &&
                   !node.severity) {
            SectionHeader(node.label);
        } else {
            Label(node, node.kind != SCO_WIDGET_BUTTON);
        }
        const float controlWidth = layout::ControlWidth(available, column, style.ItemSpacing.x, fontSize);
        switch (node.kind) {
        case SCO_WIDGET_LABEL:
            break;
        case SCO_WIDGET_BUTTON: {
            const std::string_view caption = node.label.empty() ? std::string_view("Activate") : std::string_view(node.label);
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
            ImGui::SetNextItemWidth(controlWidth);
            if (node.presentation == SCO_WIDGET_NUMERIC_NUMBER) {
                double value = node.value;
                // The slider shows the value with up to three decimals: "30", "0.5", "0.125". That
                // text holds no '%', so ImGui prints it unchanged and does not round input to it.
                // Input is rounded here instead, to three decimals exactly as ImGui rounded it to the
                // old "%.3f"; typed text (Ctrl+click) keeps its value, as it did then.
                const std::string shown = layout::NumberText(node.value);
                changed = ImGui::SliderScalar("##value", ImGuiDataType_Double, &value,
                                              &node.minimum, &node.maximum, shown.c_str(), ImGuiSliderFlags_AlwaysClamp);
                const auto& io = ImGui::GetIO();
                if (changed && !(io.WantTextInput && !io.MouseDown[0]))
                    value = layout::RoundDecimals(value, layout::kNumberDecimals);
                // Like ImGui: no event when the rounded value equals the current one.
                changed = changed && !(value == node.value);
                event.value = value;
            } else {
                double value = node.value;
                const char* format = node.presentation == SCO_WIDGET_NUMERIC_INTEGER_PERCENT ? "%.0f%%" : "%.0f";
                // Keep fractional input until after ImGui returns so ties round away from zero.
                changed = ImGui::SliderScalar("##value", ImGuiDataType_Double, &value,
                                              &node.minimum, &node.maximum, format,
                                              ImGuiSliderFlags_AlwaysClamp | ImGuiSliderFlags_NoRoundToFormat);
                if (!std::isfinite(value)) changed = false;
                else event.value = std::clamp(std::round(value), node.minimum, node.maximum);
                // ImGui scales navigation by the range for wide sliders; integer
                // controls use one-unit keyboard steps. Text editing keeps its arrows.
                const auto& io = ImGui::GetIO();
                if (changed && ImGui::IsItemActive() && !io.WantTextInput && !io.MouseDown[0] &&
                    std::isfinite(value) && value != node.value) {
                    const double step = value > node.value ? 1.0 : -1.0;
                    event.value = std::clamp(node.value + step, node.minimum, node.maximum);
                    changed = event.value != node.value;
                }
            }
            break;
        }
        case SCO_WIDGET_COMBO: {
            // The current value is the combo's preview. ImGui hides text after "##" in a preview, so
            // that is spelled out with a space; the option rows below keep plugin text literal.
            std::string preview;
            bool truncated = false;
            const bool current = node.index >= 0 && static_cast<size_t>(node.index) < node.items.size();
            if (current) {
                const float room = controlWidth - ImGui::GetFrameHeight() - style.FramePadding.x;
                preview = layout::NeutralizeHashes(
                    layout::Ellipsize(node.items[static_cast<size_t>(node.index)], room, TextWidth, &truncated));
            }
            const layout::PopupLimit limit = layout::ComboPopupLimit(fontSize, style.ItemSpacing.y, style.WindowPadding.y);
            ImGui::SetNextItemWidth(controlWidth);
            ImGui::SetNextWindowSizeConstraints(ImVec2(0, 0), ImVec2(limit.maxWidth, limit.maxHeight));
            if (ImGui::BeginCombo("##value", preview.c_str())) {
                EndScope combo{ImGui::EndCombo};
                event.index = Options(node, layout::ComboPopupRowWidth(controlWidth, fontSize, style.WindowPadding.x, style.ScrollbarSize));
                changed = event.index != node.index;
            } else if (truncated) {
                FullTextTooltip(node.items[static_cast<size_t>(node.index)]);
            }
            break;
        }
        case SCO_WIDGET_LIST: {
            const float width = layout::ControlWidth(available, 0.0f, style.ItemSpacing.x, fontSize, layout::kListMax);
            if (ImGui::BeginListBox("##value", ImVec2(width, 0))) {
                EndScope list{ImGui::EndListBox};
                // The list is a fixed-size child, so its own inner width is safe to fill.
                event.index = Options(node, ImGui::GetContentRegionAvail().x);
                changed = event.index != node.index;
            }
            paged = Pager(node, page.row);
            break;
        }
        case SCO_WIDGET_TEXT_INPUT: {
            // Never truncate a UTF-8 code point; the service already validates input.
            if (node.text.size() > SCO_WIDGETS_MAX_TEXT || !Utf8(node.text.c_str())) break;
            char buffer[SCO_WIDGETS_MAX_TEXT + 1]{};
            std::memcpy(buffer, node.text.data(), node.text.size());
            ImGui::SetNextItemWidth(controlWidth);
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
            // A ROW made only of COLUMNs shares its width equally between them, so two columns fit
            // side by side. Any other ROW keeps its children on one line.
            const auto found = ctx.groups.find(node.id);
            const Group children = found == ctx.groups.end() ? Group{} : found->second;
            if (node.kind == SCO_WIDGET_ROW && children.children >= 2 && children.columns == children.children &&
                children.children <= SCO_WIDGETS_MAX_COLUMNS &&
                ImGui::BeginTable("##columns", static_cast<int>(children.children),
                                  ImGuiTableFlags_SizingStretchSame | ImGuiTableFlags_NoSavedSettings)) {
                EndScope table{ImGui::EndTable};
                ImGui::TableNextRow();
                while (cursor < panel.nodes.size() && panel.nodes[cursor].parent == node.id) {
                    ImGui::TableNextColumn();
                    RenderNode(panel, cursor, ctx, disabled);
                }
                break;
            }
            bool first = true;
            while (cursor < panel.nodes.size() && panel.nodes[cursor].parent == node.id) {
                if (!first && node.kind == SCO_WIDGET_ROW) ImGui::SameLine();
                RenderNode(panel, cursor, ctx, disabled);
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
    if (changed && ctx.events.size() < SCO_WIDGETS_MAX_EVENTS) ctx.events.push_back(std::move(event));
    if (paged && ctx.events.size() < SCO_WIDGETS_MAX_EVENTS) ctx.events.push_back(std::move(page));
}
} // namespace
void ImGuiRenderer::Render(const Panel& panel, std::vector<Event>& events) {
    ImGui::PushID(panel.id.c_str());
    EndScope id{ImGui::PopID};
    if (showTitle_) Plain(panel.title);
    // The host provides a validated, contiguous preorder tree.
    Context ctx{events, {}};
    for (const Node& node : panel.nodes) {
        Group& group = ctx.groups[node.parent];
        ++group.children;
        if (node.kind == SCO_WIDGET_COLUMN) ++group.columns;
        if (IsForm(node.kind) && !node.label.empty()) group.widestLabel = std::max(group.widestLabel, LabelWidth(node));
    }
    size_t cursor = 0;
    while (cursor < panel.nodes.size()) RenderNode(panel, cursor, ctx, false);
}
} // namespace sco::widgets
#ifdef SCO_TEST_IMGUI_ADAPTER
// Link-time stand-ins for the ImGui calls the headless adapter tests do not record themselves.
// Only built into those tests (tests/test_widgets_sdk.cpp supplies the rest); the host links ImGui.
namespace ImGui {
void SeparatorText(const char* label) { TextUnformatted(label); }
void Separator() {}
void SetNextItemWidth(float) {}
void SetNextWindowSizeConstraints(const ImVec2&, const ImVec2&, ImGuiSizeCallback, void*) {}
void AlignTextToFramePadding() {}
void SetCursorScreenPos(const ImVec2&) {}
void PushTextWrapPos(float) {}
void PopTextWrapPos() {}
const ImVec4& GetStyleColorVec4(ImGuiCol) { static const ImVec4 color(0.26f, 0.59f, 0.98f, 0.31f); return color; }
bool IsRectVisible(const ImVec2&, const ImVec2&) { return true; }
} // namespace ImGui
#endif
