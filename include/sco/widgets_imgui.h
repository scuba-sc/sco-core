// SPDX-License-Identifier: GPL-3.0-only
// HOST ONLY: excluded from plugin SDK packages. No ImGui types cross this API.
#pragma once
#include "sco/widgets.h"
namespace sco::widgets {
// Draw into the host's current ImGui window/context on the game thread. The
// product supplies and links its matching ImGui implementation and backend.
class ImGuiRenderer final : public Renderer {
public:
    explicit ImGuiRenderer(bool showTitle = true) : showTitle_(showTitle) {}
    void Render(const Panel& panel, std::vector<Event>& events) override;
private:
    bool showTitle_;
};
} // namespace sco::widgets
