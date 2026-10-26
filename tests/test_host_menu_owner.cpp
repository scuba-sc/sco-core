// SPDX-License-Identifier: GPL-3.0-only
#include "sco/host.h"
#include "sco/runtime.h"
#include "sco/widgets.h"
#include <cstdio>
#include <cstring>
#include <thread>

static int passed = 0, failed = 0;
#define CHECK(c) do { if (c) ++passed; else { ++failed; std::printf("FAIL %d: %s\n", __LINE__, #c); } } while (0)

int main() {
    sco::SetGameThread();
    const sco_api* api = sco::host::BuildApi({"menu-owner test"});
    CHECK(api != nullptr);
    CHECK(sco::widgets::Start() == sco::Result::Ok);
    CHECK(sco::host::NewPlugin("menu") == nullptr);

    // Competing starts can admit only one fixed host owner.
    sco_plugin* first = nullptr;
    sco_plugin* second = nullptr;
    std::thread a([&] { first = sco::host::NewMenuOwner(); });
    std::thread b([&] { second = sco::host::NewMenuOwner(); });
    a.join(); b.join();
    CHECK((first == nullptr) != (second == nullptr));
    sco_plugin* menu = first ? first : second;
    CHECK(menu && std::strcmp(sco::host::PluginId(menu), "menu") == 0);
    CHECK(sco::host::NewMenuOwner() == nullptr);
    sco_plugin* plugin = sco::host::NewPlugin("example");
    CHECK(plugin != nullptr);

    const void* found = nullptr;
    CHECK(api->query_service(SCO_WIDGETS_NAME, SCO_WIDGETS_VERSION_1_0, &found) == SCO_OK);
    const auto* widgets = static_cast<const sco_widgets_v1*>(found);
    sco_widget_node label{};
    label.size = sizeof(label); label.kind = SCO_WIDGET_LABEL; label.id = 1; label.label = "Settings";
    CHECK(widgets->set_panel(plugin, "menu.main", "Menu", &label, 1, sizeof(label), nullptr, nullptr) == SCO_BAD_ARG);
    CHECK(widgets->set_panel(menu, "menu.main", "Menu", &label, 1, sizeof(label), nullptr, nullptr) == SCO_OK);
    CHECK(sco::widgets::OwnedBy(menu, "menu.main"));
    CHECK(!sco::widgets::OwnedBy(plugin, "menu.main"));
    CHECK(!sco::widgets::OwnedBy(menu, "menu.status"));
    CHECK(!sco::widgets::OwnedBy(menu, nullptr));
    CHECK(!sco::widgets::OwnedBy(nullptr, "menu.main"));
    CHECK(sco::Release(menu) == sco::Result::Ok);
    CHECK(!sco::widgets::OwnedBy(menu, "menu.main"));
    sco_plugin* replacement = sco::host::NewMenuOwner();
    CHECK(replacement && replacement != menu);
    CHECK(!sco::widgets::OwnedBy(replacement, "menu.main"));
    CHECK(widgets->set_panel(replacement, "menu.main", "Menu", &label, 1, sizeof(label), nullptr, nullptr) == SCO_OK);
    CHECK(sco::widgets::OwnedBy(replacement, "menu.main"));
    CHECK(!sco::widgets::OwnedBy(menu, "menu.main"));
    CHECK(sco::Release(replacement) == sco::Result::Ok);
    CHECK(sco::Release(plugin) == sco::Result::Ok);
    sco::widgets::Stop();

    // Host handles share the existing process-lifetime bound and are never recycled.
    size_t issued = 0;
    while (auto* owner = sco::host::NewMenuOwner()) {
        ++issued;
        CHECK(sco::Release(owner) == sco::Result::Ok);
    }
    CHECK(issued == sco::host::kMaxPlugins - 3);
    CHECK(sco::host::NewPlugin("after_limit") == nullptr);
    std::printf("host menu owner: %d passed, %d failed\n", passed, failed);
    return failed ? 1 : 0;
}
