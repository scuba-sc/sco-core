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
    const float column = sco::widgets::layout::LabelColumnWidth(widest, font);
    const float control = sco::widgets::layout::ControlWidth(1000.0f - 2 * padding, column, spacing, font);
    CHECK(std::fabs(short_first - (column + spacing + control)) < 1.5f);
}

int main() {
    Run(false);
    Run(true);
    RunAlignment();
    std::printf("widgets imgui smoke: %d passed, %d failed\n", pass, fail);
    return fail ? 1 : 0;
}
