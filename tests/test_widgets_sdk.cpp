// SPDX-License-Identifier: GPL-3.0-only
// Standalone ABI-boundary tests: no renderer, game or host link required.
#include "scosdk/widgets.hpp"
#include <cstdio>
#include <cstring>
#ifdef SCO_TEST_IMGUI_ADAPTER
#include "sco/widgets_imgui.h"
#include "imgui.h"
#include <string>
#include <vector>
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
static sco_result Set(sco_plugin* self, const char* id, const char* title,
                      const sco_widget_node* nodes, uint32_t count, uint32_t stride,
                      sco_widget_event_fn fn, void* ctx) {
    CHECK(self == expected_self);
    CHECK(std::strcmp(id, "test.main") == 0);
    CHECK(std::strcmp(title, "Panel") == 0);
    CHECK(nodes && count == 1 && stride == sizeof(sco_widget_node));
    saved_fn = fn;
    saved_ctx = ctx;
    return SCO_TOO_MANY; // Result must be passed through, not converted to bool.
}
static sco_result Remove(sco_plugin* self, const char* id) {
    CHECK(self == expected_self && std::strcmp(id, "test.main") == 0);
    return SCO_NOT_FOUND;
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
static std::vector<bool> disabled_stack;
static std::string panel_namespace;
static int group_depth = 0, id_depth = 0, same_lines = 0, cells = 0;
static int input_mode = 0;
static bool Disabled() {
    for (bool value : disabled_stack) if (value) return true;
    return false;
}
namespace ImGui {
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
bool Button(const char*, const ImVec2&) { return !Disabled(); }
bool Checkbox(const char*, bool* value) { *value = !*value; return !Disabled(); }
bool SliderScalar(const char*, ImGuiDataType type, void* value, const void*, const void*, const char*, ImGuiSliderFlags) {
    CHECK(type == ImGuiDataType_Double);
    *static_cast<double*>(value) = 0.75;
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
} // namespace ImGui
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
    CHECK(labels == panel.nodes.size());
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
    sco_widgets_v1 table{sizeof(sco_widgets_v1), 0, nullptr, nullptr};
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
    answer = SCO_NOT_FOUND;
    CHECK(widgets.Open(&api, self) == SCO_NOT_FOUND);
    CHECK(!widgets && widgets.Table() == nullptr);
    CHECK(widgets.RemovePanel("test.main") == SCO_UNAVAILABLE);
#ifdef SCO_TEST_IMGUI_ADAPTER
    TestAdapter();
#endif
    std::printf("widgets SDK: %d passed, %d failed\n", pass, fail);
    return fail ? 1 : 0;
}
