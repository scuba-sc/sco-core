// SPDX-License-Identifier: GPL-3.0-only
// Headless frame test of the real ImGui renderer (no GPU, no backend). Built only when the build is
// given ImGui sources (SCO_TEST_IMGUI_SOURCE_DIR); builds frames at 1000x800 and checks that panels
// stay inside the display, show no horizontal overflow and settle to stable window sizes.
#include "sco/widgets_imgui.h"
#include "imgui.h"
#include "imgui_internal.h"
#include "../src/widgets/imgui_layout.h"
#include <cmath>
#include <cstdio>
#include <map>
#include <string>
#include <vector>

static int pass = 0, fail = 0;
#define CHECK(...) do { if (__VA_ARGS__) ++pass; else { ++fail; std::printf("FAIL %d: %s\n", __LINE__, #__VA_ARGS__); } } while (0)

using namespace sco::widgets;

static Panel BuildPanel() {
    Panel panel;
    panel.id = "smoke.main";
    panel.title = "Smoke";
    uint32_t next = 1;
    auto add = [&](uint32_t kind, uint32_t parent, const char* label) -> Node& {
        panel.nodes.emplace_back();
        Node& node = panel.nodes.back();
        node.kind = kind; node.id = next++; node.parent = parent; node.label = label;
        return node;
    };
    // LIST of 300-character names, selected row in the middle.
    Node& list = add(SCO_WIDGET_LIST, 0, "Long names");
    for (int i = 0; i < 40; ++i) list.items.push_back(std::string(300, static_cast<char>('a' + i % 26)));
    list.index = 3;
    // COMBO with 256 options, some of them long.
    Node& combo = add(SCO_WIDGET_COMBO, 0, "Many options");
    for (int i = 0; i < 256; ++i)
        combo.items.push_back("Option " + std::to_string(i) + (i % 17 == 0 ? std::string(200, '.') : std::string()));
    combo.index = 17;
    // ROW of two COLUMNs, each COMBO + TEXT_INPUT.
    const uint32_t row = add(SCO_WIDGET_ROW, 0, "").id;
    for (const char* name : {"Left group", "Right group"}) {
        const uint32_t column = add(SCO_WIDGET_COLUMN, row, name).id;
        Node& inner = add(SCO_WIDGET_COMBO, column, "Choice");
        inner.items = {"alpha", "beta", "gamma"};
        inner.index = 1;
        add(SCO_WIDGET_TEXT_INPUT, column, "A fairly long label for input").text = "value";
    }
    return panel;
}

struct Frames {
    const Panel panel = BuildPanel();
    ImGuiID comboPopup = 0;
    // Window name -> size in the last frame.
    std::map<std::string, ImVec2> sizes;
    bool popupSeen = false;
    float popupWidth = 0, popupHeight = 0;
    int widerThanDisplay = 0, horizontalOverflow = 0, grew = 0;
    float rootScrollMaxX = 0;

    void Frame(int frame, bool autoResize) {
        ImGui::NewFrame();
        ImGui::SetNextWindowPos(ImVec2(0, 0));
        if (!autoResize) ImGui::SetNextWindowSize(ImVec2(1000, 800));
        ImGui::Begin("root", nullptr, autoResize ? ImGuiWindowFlags_AlwaysAutoResize : 0);
        if (frame == 3) {
            // Open the 256-option combo the way a click would: its popup id on this window's ID stack.
            ImGui::PushID(panel.id.c_str());
            ImGui::PushID(2);
            const ImGuiID combo = ImGui::GetID("##value");
            ImGui::PopID();
            ImGui::PopID();
            comboPopup = ImHashStr("##ComboPopup", 0, combo);
            ImGui::OpenPopup(comboPopup);
        }
        std::vector<Event> events;
        ImGuiRenderer().Render(panel, events);
        CHECK(events.empty());
        rootScrollMaxX = ImGui::GetScrollMaxX();
        ImGui::End();
        ImGui::EndFrame();
        ImGuiContext& g = *ImGui::GetCurrentContext();
        for (ImGuiWindow* window : g.Windows) {
            if (!window->Active || window->Hidden) continue;
            if (window->Size.x > 1000.0f + 0.5f) ++widerThanDisplay;
            if (frame >= 8 && window->ScrollMax.x > 0.0f) ++horizontalOverflow;
            const std::string name = window->Name;
            auto it = sizes.find(name);
            if (frame >= 8 && it != sizes.end() &&
                (std::fabs(it->second.x - window->Size.x) > 0.5f || std::fabs(it->second.y - window->Size.y) > 0.5f))
                ++grew;
            sizes[name] = window->Size;
            if (name.find("##Combo_") != std::string::npos) {
                popupSeen = true;
                popupWidth = window->Size.x; popupHeight = window->Size.y;
            }
        }
    }
};

static void Run(bool autoResize) {
    IMGUI_CHECKVERSION();
    ImGuiContext* context = ImGui::CreateContext();
    ImGuiIO& io = ImGui::GetIO();
    io.IniFilename = nullptr;
    io.LogFilename = nullptr;
    io.DisplaySize = ImVec2(1000, 800);
    io.DeltaTime = 1.0f / 60.0f;
    io.BackendFlags |= ImGuiBackendFlags_RendererHasTextures; // no renderer: textures are never uploaded
    io.Fonts->AddFontDefault();
    Frames frames;
    for (int frame = 0; frame < 40; ++frame) frames.Frame(frame, autoResize);
    CHECK(frames.popupSeen); // the combo really was open, so the checks below are not vacuous
    CHECK(frames.widerThanDisplay == 0);
    CHECK(frames.horizontalOverflow == 0);
    CHECK(frames.rootScrollMaxX == 0.0f);
    CHECK(frames.grew == 0);
    const float scale = ImGui::GetFontSize() / 16.0f;
    CHECK(frames.popupWidth <= 640.0f * scale + 0.5f);
    CHECK(frames.popupHeight <= 12.0f * (ImGui::GetFontSize() + ImGui::GetStyle().ItemSpacing.y) +
                                    2.0f * ImGui::GetStyle().WindowPadding.y + 0.5f);
    std::printf("%s: popup %.0fx%.0f, windows=%zu\n", autoResize ? "auto-resize root" : "fixed root",
                frames.popupWidth, frames.popupHeight, frames.sizes.size());
    ImGui::DestroyContext(context);
}

// Labelled controls share one label column: the width of a form row is the same whichever label
// is longer or comes last, and equals label column + gap + control width.
static float LastRowWidth(const char* first, const char* second) {
    ImGuiContext* context = ImGui::CreateContext();
    ImGuiIO& io = ImGui::GetIO();
    io.IniFilename = nullptr;
    io.DisplaySize = ImVec2(1000, 800);
    io.DeltaTime = 1.0f / 60.0f;
    io.BackendFlags |= ImGuiBackendFlags_RendererHasTextures;
    io.Fonts->AddFontDefault();
    Panel panel;
    panel.id = "align";
    for (const char* label : {first, second}) {
        panel.nodes.emplace_back();
        Node& node = panel.nodes.back();
        node.kind = SCO_WIDGET_TEXT_INPUT; node.id = static_cast<uint32_t>(panel.nodes.size()); node.label = label;
    }
    float width = 0;
    for (int frame = 0; frame < 5; ++frame) {
        ImGui::NewFrame();
        ImGui::SetNextWindowPos(ImVec2(0, 0));
        ImGui::SetNextWindowSize(ImVec2(1000, 800));
        ImGui::Begin("root");
        std::vector<Event> events;
        ImGuiRenderer(false).Render(panel, events);
        width = ImGui::GetItemRectSize().x;   // the last node's group
        ImGui::End();
        ImGui::EndFrame();
    }
    ImGui::DestroyContext(context);
    return width;
}

static void RunAlignment() {
    const float short_first = LastRowWidth("Id", "A considerably longer label");
    const float long_first = LastRowWidth("A considerably longer label", "Id");
    CHECK(std::fabs(short_first - long_first) < 0.5f);
    // Expected: label column (widest label, clamped) + gap + control width.
    ImGuiContext* context = ImGui::CreateContext();
    ImGui::GetIO().IniFilename = nullptr;
    ImGui::GetIO().LogFilename = nullptr;
    ImGui::GetIO().Fonts->AddFontDefault();
    ImGui::GetIO().BackendFlags |= ImGuiBackendFlags_RendererHasTextures;
    ImGui::GetIO().DisplaySize = ImVec2(1000, 800);
    ImGui::NewFrame();
    const float font = ImGui::GetFontSize();
    const float spacing = ImGui::GetStyle().ItemSpacing.x;
    const float padding = ImGui::GetStyle().WindowPadding.x;
    const float widest = ImGui::CalcTextSize("A considerably longer label").x;
    ImGui::EndFrame();
    ImGui::DestroyContext(context);
    const float available = 1000.0f - 2 * padding;   // the window's content width
    const float column = sco::widgets::layout::LabelColumnWidth(widest, available, font);
    const float control = sco::widgets::layout::ControlWidth(available, column, spacing, font);
    CHECK(std::fabs(short_first - (column + spacing + control)) < 1.5f);
}

// A two-column ROW of 8 labelled COMBOs per column: long labels (~30 characters) and long current
// values (~25) must leave each combo most of its column and show the value without an ellipsis.
// Combo widths are read from the frame rectangles the renderer drew (ImGuiCol_FrameBg, the combo
// minus its arrow button); column content widths from the ImGui table the ROW became.
static void RunTwoColumnCombos() {
    ImGuiContext* context = ImGui::CreateContext();
    ImGuiIO& io = ImGui::GetIO();
    io.IniFilename = nullptr;
    io.LogFilename = nullptr;
    io.DisplaySize = ImVec2(1000, 800);
    io.DeltaTime = 1.0f / 60.0f;
    io.BackendFlags |= ImGuiBackendFlags_RendererHasTextures;
    io.Fonts->AddFontDefault();
    const std::string value = "Unchanged (keeps current)";   // 25 characters
    Panel panel;
    panel.id = "form";
    uint32_t next = 1;
    const auto add = [&](uint32_t kind, uint32_t parent, const std::string& label) -> Node& {
        panel.nodes.emplace_back();
        Node& node = panel.nodes.back();
        node.kind = kind; node.id = next++; node.parent = parent; node.label = label;
        return node;
    };
    const uint32_t row = add(SCO_WIDGET_ROW, 0, "").id;
    for (int c = 0; c < 2; ++c) {
        const uint32_t column = add(SCO_WIDGET_COLUMN, row, "").id;
        for (int i = 0; i < 8; ++i) {
            Node& combo = add(SCO_WIDGET_COMBO, column, "Field " + std::to_string(c * 8 + i) + " with a long form label");   // ~30 characters
            combo.items = {value, "Set to the first value", "Set to the second value"};
            combo.index = 0;
        }
    }
    for (const Node& node : panel.nodes)
        if (node.kind == SCO_WIDGET_COMBO) CHECK(node.label.size() >= 28 && node.label.size() <= 34);
    CHECK(value.size() >= 24 && value.size() <= 26);

    std::vector<float> comboWidths, columnContent;
    float frameHeight = 0, framePadX = 0, valueWidth = 0, windowWidth = 0;
    for (int frame = 0; frame < 6; ++frame) {
        ImGui::NewFrame();
        ImGui::SetNextWindowPos(ImVec2(0, 0));
        ImGui::SetNextWindowSize(ImVec2(1000, 800));
        ImGui::Begin("root");
        std::vector<Event> events;
        ImGuiRenderer(false).Render(panel, events);
        if (frame == 5) {
            frameHeight = ImGui::GetFrameHeight();
            framePadX = ImGui::GetStyle().FramePadding.x;
            valueWidth = ImGui::CalcTextSize(value.c_str()).x;
            windowWidth = ImGui::GetWindowWidth();
            const ImU32 frameBg = ImGui::GetColorU32(ImGuiCol_FrameBg);
            const ImVector<ImDrawVert>& v = ImGui::GetWindowDrawList()->VtxBuffer;
            for (int i = 0; i + 3 < v.Size; ) {
                const bool rect = v[i].col == frameBg && v[i + 1].col == frameBg && v[i + 2].col == frameBg && v[i + 3].col == frameBg &&
                                  v[i].pos.y == v[i + 1].pos.y && v[i + 2].pos.y == v[i + 3].pos.y &&
                                  v[i].pos.x == v[i + 3].pos.x && v[i + 1].pos.x == v[i + 2].pos.x &&
                                  v[i + 1].pos.x > v[i].pos.x && v[i + 2].pos.y > v[i].pos.y;
                if (rect) { comboWidths.push_back(v[i + 1].pos.x - v[i].pos.x + frameHeight); i += 4; }   // + the arrow button
                else ++i;
            }
            ImGuiContext& g = *ImGui::GetCurrentContext();
            for (int n = 0; n < g.Tables.GetMapSize(); ++n) {
                const ImGuiTable* table = g.Tables.TryGetMapData(n);
                if (!table || table->ColumnsCount != 2) continue;
                for (int c = 0; c < 2; ++c) columnContent.push_back(table->Columns[c].WorkMaxX - table->Columns[c].WorkMinX);
            }
        }
        ImGui::End();
        ImGui::EndFrame();
    }
    CHECK(windowWidth == 1000.0f);
    CHECK(comboWidths.size() == 16);
    CHECK(columnContent.size() == 2);
    if (comboWidths.size() == 16 && columnContent.size() == 2) {
        float least = 1.0e9f;
        for (size_t i = 0; i < comboWidths.size(); ++i) {
            const float content = columnContent[i / 8];
            least = std::min(least, comboWidths[i] / content);
            CHECK(comboWidths[i] >= 0.55f * content);     // most of the column, not ~100 px
            CHECK(comboWidths[i] <= content + 0.5f);
            // The preview is laid out in what is left of the combo after its arrow and padding: the
            // whole current value must fit there, so it is shown without an ellipsis.
            CHECK(valueWidth <= comboWidths[i] - frameHeight - framePadX);
        }
        std::printf("two-column combos: column content %.0f / %.0f px, combo width %.0f px (%.0f%% of its column at the least), value %.0f px\n",
                    columnContent[0], columnContent[1], comboWidths[0], 100.0f * least, valueWidth);
    }
    ImGui::DestroyContext(context);
}

// What real ImGui draws for a NUMBER slider and an unlabelled ROW of buttons: whole values without
// decimals ("30", not "30.000"), others with up to three, and no blank line above the row. Slider
// text is read from ImGui's own text log, which records what it renders.
static void RunNumberAndRows() {
    ImGuiContext* context = ImGui::CreateContext();
    ImGuiIO& io = ImGui::GetIO();
    io.IniFilename = nullptr;
    io.LogFilename = nullptr;
    io.DisplaySize = ImVec2(1000, 800);
    io.DeltaTime = 1.0f / 60.0f;
    io.BackendFlags |= ImGuiBackendFlags_RendererHasTextures;
    io.Fonts->AddFontDefault();
    struct Case { double value; const char* shown; };
    for (const Case c : {Case{30, "30"}, Case{0.5, "0.5"}, Case{0.125, "0.125"}, Case{1.23456, "1.235"}}) {
        Panel panel;
        panel.id = "number";
        panel.nodes.emplace_back();
        Node& slider = panel.nodes.back();
        slider.kind = SCO_WIDGET_SLIDER; slider.id = 1; slider.label = "Speed";
        slider.minimum = 0; slider.maximum = 100; slider.value = c.value;
        std::string logged;
        for (int frame = 0; frame < 2; ++frame) {
            ImGui::NewFrame();
            ImGui::SetNextWindowPos(ImVec2(0, 0));
            ImGui::SetNextWindowSize(ImVec2(1000, 800));
            ImGui::Begin("root");
            ImGui::LogToBuffer();
            std::vector<Event> events;
            ImGuiRenderer(false).Render(panel, events);
            CHECK(events.empty());
            logged = ImGui::GetCurrentContext()->LogBuffer.c_str();
            ImGui::LogFinish();
            ImGui::End();
            ImGui::EndFrame();
        }
        CHECK(logged.find(c.shown) != std::string::npos);
        CHECK(std::string(c.shown).find('.') != std::string::npos || logged.find(std::string(c.shown) + ".") == std::string::npos);
    }
    // An unlabelled ROW of two buttons is exactly one button high.
    Panel panel;
    panel.id = "rows";
    for (uint32_t id : {1u, 2u, 3u}) {
        panel.nodes.emplace_back();
        Node& node = panel.nodes.back();
        node.id = id; node.kind = id == 1 ? SCO_WIDGET_ROW : SCO_WIDGET_BUTTON; node.parent = id == 1 ? 0 : 1;
        node.label = id == 1 ? "" : id == 2 ? "Save" : "Load";
    }
    float rowHeight = 0, frameHeight = 0;
    for (int frame = 0; frame < 2; ++frame) {
        ImGui::NewFrame();
        ImGui::SetNextWindowPos(ImVec2(0, 0));
        ImGui::SetNextWindowSize(ImVec2(1000, 800));
        ImGui::Begin("root");
        std::vector<Event> events;
        ImGuiRenderer(false).Render(panel, events);
        rowHeight = ImGui::GetItemRectSize().y;   // the ROW's group
        frameHeight = ImGui::GetFrameHeight();
        ImGui::End();
        ImGui::EndFrame();
    }
    CHECK(std::fabs(rowHeight - frameHeight) < 0.5f);
    std::printf("number slider text and unlabelled row: row %.0f px, button %.0f px\n", rowHeight, frameHeight);
    ImGui::DestroyContext(context);
}

int main() {
    Run(false);
    Run(true);
    RunAlignment();
    RunTwoColumnCombos();
    RunNumberAndRows();
    std::printf("widgets imgui smoke: %d passed, %d failed\n", pass, fail);
    return fail ? 1 : 0;
}
