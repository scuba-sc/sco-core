// SPDX-License-Identifier: GPL-3.0-only
// Standalone ABI-boundary tests: no renderer, game or host link required.
#include "scosdk/widgets.hpp"
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <memory>
#ifdef SCO_TEST_IMGUI_ADAPTER
#include "sco/widgets_imgui.h"
#include "imgui.h"
#include <string>
#include <vector>
#include <cstdlib>
#endif

static int pass = 0, fail = 0;
#define CHECK(...) do { if (__VA_ARGS__) ++pass; else { ++fail; std::printf("FAIL %d: %s\n", __LINE__, #__VA_ARGS__); } } while (0)
static const void* service = nullptr;
static sco_result answer = SCO_OK;
static sco_result Query(const char* name, uint32_t version, const void** out) {
    CHECK(std::strcmp(name, SCO_WIDGETS_NAME) == 0);
    CHECK(version == SCO_WIDGETS_VERSION_1_0);
    *out = service;
    return answer;
}
static sco_plugin* expected_self = nullptr;
static sco_widget_event_fn saved_fn = nullptr;
static void* saved_ctx = nullptr;
static uint32_t expected_stride = sizeof(sco_widget_node);
static uint32_t reported_features = SCO_WIDGETS_FEATURE_STABLE_TAGS;
static sco_result Set(sco_plugin* self, const char* id, const char* title,
                      const sco_widget_node* nodes, uint32_t count, uint32_t stride,
                      sco_widget_event_fn fn, void* ctx) {
    CHECK(self == expected_self);
    CHECK(std::strcmp(id, "test.main") == 0);
    CHECK(std::strcmp(title, "Panel") == 0);
    CHECK(nodes && count == 1 && stride == expected_stride);
    saved_fn = fn;
    saved_ctx = ctx;
    return SCO_TOO_MANY; // Result must be passed through, not converted to bool.
}
static sco_result Remove(sco_plugin* self, const char* id) {
    CHECK(self == expected_self && std::strcmp(id, "test.main") == 0);
    return SCO_NOT_FOUND;
}
static sco_result Update(sco_plugin* self, const char* id, const sco_widget_node* nodes, uint32_t count, uint32_t stride) {
    CHECK(self == expected_self && std::strcmp(id, "test.main") == 0);
    CHECK(nodes && count == 1 && stride == sizeof(sco_widget_node_1_2) && nodes->size == stride);
    return SCO_WRONG_THREAD;
}
static sco_result RemoveNodes(sco_plugin* self, const char* id, const uint32_t* ids, uint32_t count) {
    CHECK(self == expected_self && std::strcmp(id, "test.main") == 0 && count == 1 && ids && ids[0] == 7);
    return SCO_NOT_FOUND;
}
static sco_result Caps(sco_plugin* self, uint32_t* caps) {
    CHECK(self == expected_self && caps && caps[0] == SCO_WIDGETS_CAPS_1_2);
    caps[0] = 2; caps[1] = reported_features;
    return SCO_OK;
}
struct Receiver {
    int calls = 0;
    void OnEvent(const char* panel, const sco_widget_event& event) {
        CHECK(std::strcmp(panel, "test.main") == 0 && event.node_id == 7);
        ++calls;
        throw 42; // Must not cross the C boundary.
    }
};
#ifdef SCO_TEST_IMGUI_ADAPTER
// Link-time ImGui test shim records adapter API calls. The real renderer/library
// is deliberately not part of the portable SDK test.
static std::vector<std::string> texts;
static std::vector<int> numeric_ids;
static std::vector<std::string> captions, button_ids;
static std::vector<ImVec2> button_sizes;
static float available_width = 1000.0f;
static std::vector<bool> disabled_stack;
static std::string panel_namespace;
static int group_depth = 0, id_depth = 0, same_lines = 0, cells = 0;
static int input_mode = 0;
static int style_depth = 0, tooltips = 0;
static std::string numeric_format;
static double numeric_input = 74.5;
static bool Disabled() {
    for (bool value : disabled_stack) if (value) return true;
    return false;
}
ImGuiIO::ImGuiIO() : WantTextInput(false) { MouseDown[0] = false; }
namespace ImGui {
void MemFree(void* ptr) { std::free(ptr); }
ImGuiIO& GetIO() { static ImGuiIO io; return io; }
bool IsItemActive() { return false; }
void PushID(const char* id) { panel_namespace = id; ++id_depth; }
void PushID(int id) { numeric_ids.push_back(id); ++id_depth; }
void PopID() { --id_depth; }
void TextUnformatted(const char* text, const char* end) {
    texts.emplace_back(text, end ? static_cast<size_t>(end - text) : std::strlen(text));
}
void BeginGroup() { ++group_depth; }
void EndGroup() { --group_depth; }
void SameLine(float, float) { ++same_lines; }
void BeginDisabled(bool disabled) { disabled_stack.push_back(disabled); }
void EndDisabled() { disabled_stack.pop_back(); }
bool Button(const char* id, const ImVec2& size) {
    button_ids.emplace_back(id); button_sizes.push_back(size); return !Disabled();
}
ImGuiStyle& GetStyle() { static ImGuiStyle style; return style; }
float GetFontSize() { return 17.0f; }
float GetFrameHeight() { return 17.0f + 2 * GetStyle().FramePadding.y; }
ImVec2 GetContentRegionAvail() { return ImVec2(available_width, 1000.0f); }
ImVec2 GetCursorScreenPos() { return ImVec2(0, 0); }
ImVec2 CalcTextSize(const char* begin, const char* end, bool hide, float) {
    CHECK(!hide);
    const char* finish = end ? end : begin + std::strlen(begin);
    size_t longest = 0, current = 0, lines = 1;
    for (const char* p = begin; p != finish; ++p) {
        if (*p == '\n') { if (current > longest) longest = current; current = 0; ++lines; }
        else ++current;
    }
    if (current > longest) longest = current;
    return ImVec2(float(longest) * 8.0f, 17.0f * float(lines));
}
ImU32 GetColorU32(ImGuiCol, float) { return 0xffffffffu; }
void* MemAlloc(size_t n) { return std::malloc(n); }
ImDrawList* GetWindowDrawList() { static ImDrawList list(nullptr); return &list; }
bool Checkbox(const char*, bool* value) { *value = !*value; return !Disabled(); }
bool SliderScalar(const char*, ImGuiDataType type, void* value, const void* minimum, const void* maximum, const char* format, ImGuiSliderFlags flags) {
    CHECK(flags & ImGuiSliderFlags_AlwaysClamp);
    if (flags & ImGuiSliderFlags_NoRoundToFormat) {
        CHECK(type == ImGuiDataType_Double);
        CHECK(*static_cast<const double*>(minimum) <= numeric_input && *static_cast<const double*>(maximum) >= numeric_input);
        numeric_format = format; *static_cast<double*>(value) = numeric_input;
    } else {
        CHECK(type == ImGuiDataType_Double);
        *static_cast<double*>(value) = 0.75;
    }
    return !Disabled();
}
bool BeginCombo(const char*, const char*, ImGuiComboFlags) { return true; }
void EndCombo() {}
bool BeginListBox(const char*, const ImVec2&) { return true; }
void EndListBox() {}
bool Selectable(const char*, bool selected, ImGuiSelectableFlags, const ImVec2&) { return !selected && !Disabled(); }
bool InputText(const char*, char* buffer, size_t size, ImGuiInputTextFlags, ImGuiInputTextCallback, void*) {
    CHECK(size == SCO_WIDGETS_MAX_TEXT + 1);
    if (input_mode == 1) std::strcpy(buffer, "\xED\xA0\x80"); // UTF-8 encoded surrogate
    else if (input_mode == 2) {
        std::memset(buffer, 'x', size - 4);
        std::memcpy(buffer + size - 4, "\xE2\x9C\x93", 4); // exactly 255 bytes
    } else std::strcpy(buffer, "edited \xE2\x9C\x93");
    return !Disabled();
}
bool BeginTable(const char*, int columns, ImGuiTableFlags, const ImVec2&, float) { CHECK(columns == 2); return true; }
void EndTable() {}
void TableNextRow(ImGuiTableRowFlags, float) {}
bool TableNextColumn() { ++cells; return true; }
void PushStyleColor(ImGuiCol, const ImVec4&) { ++style_depth; }
void PopStyleColor(int count) { style_depth -= count; }
bool BeginItemTooltip() { ++tooltips; return true; }
void EndTooltip() {}
} // namespace ImGui
ImGuiStyle::ImGuiStyle() { FramePadding = ImVec2(9, 5); }
ImDrawList::ImDrawList(ImDrawListSharedData*) {}
ImDrawList::~ImDrawList() {}
void ImDrawListSplitter::ClearFreeMemory() {}
void ImDrawList::AddText(ImFont*, float, const ImVec2& pos, ImU32,
                         const char* begin, const char* end, float, const ImVec4* clip) {
    captions.emplace_back(begin, end);
    CHECK(clip && !button_sizes.empty() && clip->z == button_sizes.back().x && clip->w == button_sizes.back().y);
    CHECK(pos.x == ImGui::GetStyle().FramePadding.x && pos.y == ImGui::GetStyle().FramePadding.y);
}
static void TestAdapter() {
    sco::widgets::Panel panel;
    panel.id = "test.main";
    panel.title = "Title %s##plain";
    auto add = [&](uint32_t kind, uint32_t id, uint32_t parent = 0) -> sco::widgets::Node& {
        panel.nodes.emplace_back();
        auto& node = panel.nodes.back();
        node.kind = kind; node.id = id; node.parent = parent;
        node.label = "Label %n##plain";
        return node;
    };
    add(SCO_WIDGET_ROW, 1);
    add(SCO_WIDGET_LABEL, 2, 1);
    add(SCO_WIDGET_COLUMN, 3, 1);
    add(SCO_WIDGET_BUTTON, 4, 3);
    add(SCO_WIDGET_TOGGLE, 5, 3);
    auto& slider = add(SCO_WIDGET_SLIDER, 6, 3); slider.maximum = 1;
    auto& combo = add(SCO_WIDGET_COMBO, 7); combo.items = {"zero##plain", "one%s"};
    auto& list = add(SCO_WIDGET_LIST, 8); list.items = {"zero##plain", "one%s"};
    add(SCO_WIDGET_TEXT_INPUT, 9).text = "start";
    auto& table = add(SCO_WIDGET_TABLE, 10); table.columns = 2; table.items = {"a%s", "b##", "c", "d"};
    add(SCO_WIDGET_BUTTON, 11).flags = SCO_WIDGET_DISABLED;
    sco::widgets::ImGuiRenderer renderer;
    std::vector<sco::widgets::Event> events;
    renderer.Render(panel, events);
    CHECK(events.size() == 6);
    CHECK(panel_namespace == panel.id);
    CHECK(id_depth == 0 && group_depth == 0 && disabled_stack.empty());
    CHECK(same_lines >= 1 && cells == 4);
    CHECK(!texts.empty() && texts.front() == panel.title);
    size_t labels = 0;
    for (const auto& text : texts) if (text == "Label %n##plain") ++labels;
    CHECK(labels == panel.nodes.size() - 2);
    CHECK(captions.size() == 2 && captions[0] == "Label %n##plain" && captions[1] == "Label %n##plain");
    CHECK(numeric_ids.size() >= panel.nodes.size());
    const uint32_t kinds[] = {SCO_WIDGET_BUTTON, SCO_WIDGET_TOGGLE, SCO_WIDGET_SLIDER,
                             SCO_WIDGET_COMBO, SCO_WIDGET_LIST, SCO_WIDGET_TEXT_INPUT};
    for (size_t i = 0; i < events.size() && i < 6; ++i) {
        CHECK(events[i].kind == kinds[i]);
        CHECK(events[i].node_id == i + 4);
    }
    if (events.size() == 6) {
        CHECK(events[0].index == 0 && events[0].value == 0 && events[0].text.empty());
        CHECK(events[1].value == 1 && events[2].value == 0.75);
        CHECK(events[3].index == 1 && events[4].index == 1);
        CHECK(events[5].text == "edited \xE2\x9C\x93" && events[5].text.size() <= SCO_WIDGETS_MAX_TEXT);
    }
    sco::widgets::Panel input_panel;
    input_panel.id = "test.input";
    sco::widgets::Node input;
    input.id = 42; input.kind = SCO_WIDGET_TEXT_INPUT; input.label = "Text";
    input_panel.nodes.push_back(input);
    input_mode = 1;
    events.clear();
    renderer.Render(input_panel, events);
    CHECK(events.empty()); // Invalid UTF-8 is never emitted.
    input_mode = 2;
    renderer.Render(input_panel, events);
    CHECK(events.size() == 1 && events[0].text.size() == SCO_WIDGETS_MAX_TEXT);
    input_panel.nodes[0].text.assign(SCO_WIDGETS_MAX_TEXT + 1, 'x');
    events.clear();
    renderer.Render(input_panel, events);
    CHECK(events.empty()); // Never truncate oversized source text.
    input_mode = 0;
    panel.nodes[0].flags = SCO_WIDGET_DISABLED;
    events.clear();
    renderer.Render(panel, events);
    CHECK(events.size() == 3); // Nested button/toggle/slider inherit row disable.
    for (const auto& event : events) CHECK(event.node_id >= 7);
    CHECK(id_depth == 0 && group_depth == 0 && disabled_stack.empty());
}
static void TestButtonCaptions() {
    struct CaptionCase { const char* text; float width, height; };
    const CaptionCase cases[] = {
        {"Launch ship", 88.0f, 17.0f},
        {"Создать корабль", 232.0f, 17.0f},
        {"100% ##literal###visible", 192.0f, 17.0f},
        {"First line\nSecond line", 88.0f, 34.0f},
        {"%n %s", 40.0f, 17.0f},
        {"", 64.0f, 17.0f},
    };
    for (const auto& test : cases) for (bool disabled : {false, true}) {
        const char* text = test.text;
        sco::widgets::Panel p; p.id = "buttons";
        sco::widgets::Node n; n.kind = SCO_WIDGET_BUTTON; n.id = 77; n.label = text;
        n.flags = disabled ? SCO_WIDGET_DISABLED : 0;
        n.severity = SCO_WIDGET_SEVERITY_WARNING; p.nodes.push_back(n);
        for (float width : {1.0f, 80.0f, 1000.0f}) {
            available_width = width; captions.clear(); button_ids.clear(); button_sizes.clear(); texts.clear();
            std::vector<sco::widgets::Event> events;
            sco::widgets::ImGuiRenderer().Render(p, events);
            CHECK(captions.size() == 1 && captions[0] == (*text ? text : "Activate"));
            CHECK(button_ids.size() == 1 && button_ids[0] == "##action");
            // Natural size like ImGui::Button, independent of the available width.
            CHECK(button_sizes.size() == 1);
            if (button_sizes.size() == 1) {
                // Independent expected glyph metrics for this test shim (8 px per UTF-8 byte).
                CHECK(button_sizes[0].x == test.width + 2 * ImGui::GetStyle().FramePadding.x &&
                      button_sizes[0].y == test.height + 2 * ImGui::GetStyle().FramePadding.y);
            }
            CHECK(events.size() == (disabled ? 0u : 1u));
            if (!events.empty()) CHECK(events[0].node_id == 77 && events[0].kind == SCO_WIDGET_BUTTON);
            CHECK(p.nodes[0].label == text && id_depth == 0 && group_depth == 0 && disabled_stack.empty());
            bool warning = false; for (const auto& t : texts) warning |= t == "Warning:";
            CHECK(warning); // Severity stays visible even though the duplicate label is removed.
        }
    }
    available_width = 1000.0f;
}
// 1.1 presentation: severity as text plus color, tooltips, and paged LIST/TABLE windows.
static void TestAdapter11() {
    sco::widgets::Panel panel;
    panel.id = "test.paged";
    sco::widgets::Node list;
    list.id = 1; list.kind = SCO_WIDGET_LIST; list.label = "Pick"; list.index = -1;
    list.items = {"r0", "r1", "r2", "r3", "r4"}; list.visible_rows = 2;
    list.severity = SCO_WIDGET_SEVERITY_WARNING; list.tooltip = "Choose a row";
    panel.nodes.push_back(list);
    sco::widgets::Node grid;
    grid.id = 2; grid.kind = SCO_WIDGET_TABLE; grid.label = "Grid"; grid.columns = 2;
    grid.items = {"a", "b", "c", "d", "e", "f"}; grid.visible_rows = 1; grid.first_row = 2;
    panel.nodes.push_back(grid);
    texts.clear(); cells = 0; tooltips = 0; style_depth = 0;
    sco::widgets::ImGuiRenderer renderer;
    std::vector<sco::widgets::Event> events;
    renderer.Render(panel, events);
    CHECK(id_depth == 0 && group_depth == 0 && disabled_stack.empty() && style_depth == 0);
    CHECK(tooltips == 1 && cells == 2);
    auto shown = [&](const char* text) { for (const auto& t : texts) if (t == text) return true; return false; };
    CHECK(shown("Warning:") && shown("Choose a row") && shown("r0") && shown("r1") && !shown("r2"));
    CHECK(shown("Rows 1-2 of 5") && shown("e") && shown("f") && !shown("a") && shown("Rows 3-3 of 3"));
    CHECK(events.size() == 3);
    if (events.size() == 3) {
        CHECK(events[0].kind == SCO_WIDGET_LIST && events[0].index == 1);
        CHECK(events[1].kind == SCO_WIDGET_EVENT_PAGE && events[1].node_id == 1 && events[1].row == 2);
        CHECK(events[2].kind == SCO_WIDGET_EVENT_PAGE && events[2].node_id == 2 && events[2].row == 1);
    }
    panel.nodes[0].flags = panel.nodes[1].flags = SCO_WIDGET_DISABLED;
    events.clear();
    renderer.Render(panel, events);
    CHECK(events.empty() && disabled_stack.empty());
}
static void TestAdapter13() {
    sco::widgets::Panel panel; panel.id = "test.integer";
    sco::widgets::Node node; node.kind = SCO_WIDGET_SLIDER; node.id = 7; node.label = "Darkness";
    node.minimum = 20; node.maximum = 95; node.value = 74;
    panel.nodes.push_back(node);
    sco::widgets::ImGuiRenderer renderer;
    for (uint32_t mode : {SCO_WIDGET_NUMERIC_INTEGER, SCO_WIDGET_NUMERIC_INTEGER_PERCENT}) {
        panel.nodes[0].presentation = mode;
        std::vector<sco::widgets::Event> events;
        renderer.Render(panel, events);
        CHECK(events.size() == 1 && events[0].value == 75);
        CHECK(numeric_format == (mode == SCO_WIDGET_NUMERIC_INTEGER ? "%.0f" : "%.0f%%"));
    }
    panel.nodes[0].minimum = -95; numeric_input = -0.5;
    std::vector<sco::widgets::Event> negative;
    renderer.Render(panel, negative);
    CHECK(negative.size() == 1 && negative[0].value == -1);
}
#endif
int main() {
    sco_api api{};
    api.size = sizeof(api);
    api.query_service = Query;
    // Opaque plugin identity is passed through only; never dereferenced here.
    int identity = 0;
    auto* self = reinterpret_cast<sco_plugin*>(&identity);
    sco::sdk::Widgets widgets;
    CHECK(!widgets);
    CHECK(widgets.Open(nullptr, self) == SCO_BAD_ARG);
    CHECK(widgets.Open(&api, nullptr) == SCO_BAD_ARG);
    CHECK(widgets.Open(&api, self) == SCO_UNAVAILABLE); // successful query with NULL table
    CHECK(!widgets);
    expected_self = self;
    sco_widget_node node{};
    node.size = sizeof(node);
    node.kind = SCO_WIDGET_BUTTON;
    node.id = 7;
    node.label = "Press";
    CHECK(widgets.SetPanel("test.main", "Panel", &node, 1) == SCO_UNAVAILABLE);
    CHECK(widgets.RemovePanel("test.main") == SCO_UNAVAILABLE);
    // Physically short API: reading even one byte of query_service is out of bounds.
    alignas(sco_api) unsigned char short_api[offsetof(sco_api, query_service)]{};
    uint32_t size = sizeof(short_api);
    std::memcpy(short_api, &size, sizeof(size));
    CHECK(widgets.Open(reinterpret_cast<const sco_api*>(short_api), self) == SCO_UNAVAILABLE);
    api.size = offsetof(sco_api, query_service) + sizeof(api.query_service) - 1;
    CHECK(widgets.Open(&api, self) == SCO_UNAVAILABLE);
    api.size = sizeof(api);
    api.query_service = nullptr;
    CHECK(widgets.Open(&api, self) == SCO_UNAVAILABLE);
    api.query_service = Query;

    // A genuine header-only table has no readable method pointers.
    struct Header { uint32_t size, pad; } header{sizeof(Header), 0};
    service = &header;
    CHECK(widgets.Open(&api, self) == SCO_OK);
    CHECK(widgets.SetPanel("test.main", "Panel", &node, 1) == SCO_UNAVAILABLE);
    CHECK(widgets.RemovePanel("test.main") == SCO_UNAVAILABLE);
    struct SetOnly {
        uint32_t size, pad;
        decltype(sco_widgets_v1::set_panel) set_panel;
    } short_table{sizeof(SetOnly), 0, Set};
    static_assert(sizeof(SetOnly) == offsetof(sco_widgets_v1, remove_panel));
    service = &short_table;
    CHECK(widgets.Open(&api, self) == SCO_OK);
    CHECK(widgets.SetPanel("test.main", "Panel", &node, 1) == SCO_TOO_MANY);
    CHECK(widgets.RemovePanel("test.main") == SCO_UNAVAILABLE);
    sco_widgets_v1 table{sizeof(sco_widgets_v1), 0, nullptr, nullptr, nullptr, nullptr, nullptr};
    service = &table;
    CHECK(widgets.Open(&api, self) == SCO_OK);
    CHECK(widgets.SetPanel("test.main", "Panel", &node, 1) == SCO_UNAVAILABLE);
    CHECK(widgets.RemovePanel("test.main") == SCO_UNAVAILABLE);
    table.set_panel = Set;
    table.remove_panel = Remove;
    Receiver receiver;
    CHECK(widgets.SetPanel("test.main", "Panel", &node, 1, receiver) == SCO_TOO_MANY);
    sco_widget_event event{sizeof(sco_widget_event), 7, SCO_WIDGET_BUTTON, 0, 0, nullptr};
    CHECK(saved_fn && saved_ctx == &receiver);
    if (saved_fn) saved_fn("test.main", &event, saved_ctx);
    CHECK(receiver.calls == 1);
    CHECK(widgets.RemovePanel("test.main") == SCO_NOT_FOUND);
    sco_widget_node_1_2 extended{};
    extended.base.base = node; extended.base.base.size = sizeof(extended); extended.tag = "action.apply";
    uint32_t ids[]{7}, caps[SCO_WIDGETS_CAPS_1_2]{SCO_WIDGETS_CAPS_1_2};
    CHECK(widgets.UpdateNodes("test.main", &extended.base.base, 1, sizeof(extended)) == SCO_UNAVAILABLE);
    CHECK(widgets.RemoveNodes("test.main", ids, 1) == SCO_UNAVAILABLE);
    CHECK(widgets.Capabilities(caps) == SCO_UNAVAILABLE);
    table.update_nodes = Update; table.remove_nodes = RemoveNodes; table.capabilities = Caps;
    CHECK(widgets.UpdateNodes("test.main", &extended.base.base, 1, sizeof(extended)) == SCO_WRONG_THREAD);
    CHECK(widgets.RemoveNodes("test.main", ids, 1) == SCO_NOT_FOUND);
    CHECK(widgets.Capabilities(caps) == SCO_OK && caps[0] == 2 && caps[1] == SCO_WIDGETS_FEATURE_STABLE_TAGS);
    sco_widget_node_1_3 numeric{};
    numeric.base.base.base.size = sizeof(numeric);
    numeric.presentation = SCO_WIDGET_NUMERIC_INTEGER_PERCENT;
    CHECK(widgets.SetPanel13("test.main", "Panel", &numeric, 1) == SCO_UNAVAILABLE);
    reported_features |= SCO_WIDGETS_FEATURE_NUMERIC_PRESENTATION;
    expected_stride = sizeof(numeric);
    CHECK(widgets.SetPanel13("test.main", "Panel", &numeric, 1) == SCO_TOO_MANY);
    expected_stride = sizeof(sco_widget_node);
    // Exact allocations make an accidental partial function-pointer read observable under ASan.
    for (uint32_t extent = 4; extent < sizeof(table); ++extent) {
        auto bytes = std::make_unique<unsigned char[]>(extent);
        std::memcpy(bytes.get(), &table, extent);
        std::memcpy(bytes.get(), &extent, sizeof(extent));
        service = bytes.get();
        CHECK(widgets.Open(&api, self) == SCO_OK);
        CHECK(widgets.UpdateNodes("test.main", &extended.base.base, 1, sizeof(extended)) ==
            (extent >= 32 ? SCO_WRONG_THREAD : SCO_UNAVAILABLE));
        CHECK(widgets.RemoveNodes("test.main", ids, 1) == (extent >= 40 ? SCO_NOT_FOUND : SCO_UNAVAILABLE));
        CHECK(widgets.Capabilities(caps) == SCO_UNAVAILABLE);
    }
    answer = SCO_NOT_FOUND;
    CHECK(widgets.Open(&api, self) == SCO_NOT_FOUND);
    CHECK(!widgets && widgets.Table() == nullptr);
    CHECK(widgets.RemovePanel("test.main") == SCO_UNAVAILABLE);
#ifdef SCO_TEST_IMGUI_ADAPTER
    TestAdapter();
    TestAdapter11();
    TestAdapter13();
    TestButtonCaptions();
#endif
    std::printf("widgets SDK: %d passed, %d failed\n", pass, fail);
    return fail ? 1 : 0;
}
