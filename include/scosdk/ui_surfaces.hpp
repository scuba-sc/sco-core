// SPDX-License-Identifier: GPL-3.0-only
// scosdk/ui_surfaces.hpp: the host service "sco.ui.surfaces" (sco_ui_surfaces.h) for C++ plugins.
// Header-only C++20. Where a sco.widgets panel is shown: a 2D HUD rectangle, a camera-facing 3D
// billboard or a world-anchored 3D quad. Every call is a size-checked pass-through; the host
// validates the pose (sco_ui_surfaces.h lists the rules).
//
//   sco::sdk::UiSurfaces surfaces;
//   uint32_t id = 0;
//   if (surfaces.Open(*this) == SCO_OK)
//       surfaces.Register(SCO_SURFACE_HUD_2D, sco::sdk::UiSurfaces::Hud(0.5, 0.1, 0.3, 0.1, "hello.main"), id);
#ifndef SCOSDK_UI_SURFACES_HPP
#define SCOSDK_UI_SURFACES_HPP
#include "plugin.hpp"
#include "sco_ui_surfaces.h"
#include <cstddef>
#include <string>

namespace sco::sdk {
class UiSurfaces {
public:
    sco_result Open(const Plugin& plugin) noexcept { return Open(plugin.Api(), plugin.Self()); }
    sco_result Open(const sco_api* api, sco_plugin* self) noexcept {
        table_ = nullptr;
        self_ = self;
        if (!api || !self) return SCO_BAD_ARG;
        if (api->size < offsetof(sco_api, query_service) + sizeof(api->query_service) ||
            !api->query_service) return SCO_UNAVAILABLE;
        const void* out = nullptr;
        const sco_result result = api->query_service(SCO_UI_SURFACES_NAME, SCO_UI_SURFACES_VERSION_1_0, &out);
        if (result != SCO_OK) return result;
        if (!out) return SCO_UNAVAILABLE;
        table_ = static_cast<const sco_ui_surfaces_v1*>(out);
        return SCO_OK;
    }
    explicit operator bool() const noexcept { return table_ != nullptr; }
    const sco_ui_surfaces_v1* Table() const noexcept { return table_; }

    // A HUD rectangle: centre and size in viewport fractions.
    static sco_surface_pose Hud(double cx, double cy, double w, double h, const char* panel_id) noexcept {
        sco_surface_pose p = Blank(panel_id);
        p.pos[0] = cx; p.pos[1] = cy;
        p.width_m = w; p.height_m = h;
        return p;
    }
    // A billboard at pos (world metres), turned toward the camera with up as its up direction.
    static sco_surface_pose Billboard(const double (&pos)[3], const double (&up)[3], double w, double h,
                                      const char* panel_id, double max_distance_m = 0, uint32_t flags = 0) noexcept {
        sco_surface_pose p = Blank(panel_id);
        Place(p, pos, up, w, h, max_distance_m, flags);
        return p;
    }
    // A world-anchored quad facing along normal (unit, perpendicular to up).
    static sco_surface_pose Quad(const double (&pos)[3], const double (&normal)[3], const double (&up)[3], double w,
                                 double h, const char* panel_id, double max_distance_m = 0, uint32_t flags = 0) noexcept {
        sco_surface_pose p = Blank(panel_id);
        Place(p, pos, up, w, h, max_distance_m, flags);
        for (int i = 0; i < 3; ++i) p.normal[i] = normal[i];
        return p;
    }

    sco_result Register(uint32_t kind, const sco_surface_pose& pose, uint32_t& id) noexcept {
        if (!Has(offsetof(sco_ui_surfaces_v1, register_surface) + sizeof(table_->register_surface)) ||
            !table_->register_surface) return SCO_UNAVAILABLE;
        const sco_surface_desc desc{ sizeof(sco_surface_desc), kind, &pose };
        return table_->register_surface(self_, &desc, &id);
    }
    sco_result UpdatePose(uint32_t id, const sco_surface_pose& pose) noexcept {
        if (!Has(offsetof(sco_ui_surfaces_v1, update_surface_pose) + sizeof(table_->update_surface_pose)) ||
            !table_->update_surface_pose) return SCO_UNAVAILABLE;
        return table_->update_surface_pose(self_, id, &pose);
    }
    sco_result Unregister(uint32_t id) noexcept {
        if (!Has(offsetof(sco_ui_surfaces_v1, unregister_surface) + sizeof(table_->unregister_surface)) ||
            !table_->unregister_surface) return SCO_UNAVAILABLE;
        return table_->unregister_surface(self_, id);
    }
    // The message of this plugin's last refused call; "" when none or the table lacks last_error.
    std::string LastError() const {
        if (!Has(offsetof(sco_ui_surfaces_v1, last_error) + sizeof(table_->last_error)) || !table_->last_error) return {};
        std::string out(256, '\0');
        uint32_t size = static_cast<uint32_t>(out.size());
        sco_result r = table_->last_error(self_, out.data(), &size);
        if (r == SCO_TOO_MANY) {
            out.assign(size, '\0');
            r = table_->last_error(self_, out.data(), &size);
        }
        if (r != SCO_OK || size == 0) return {};
        out.resize(size - 1);
        return out;
    }

private:
    bool Has(size_t extent) const noexcept { return table_ && table_->size >= extent; }
    static sco_surface_pose Blank(const char* panel_id) noexcept {
        sco_surface_pose p{};
        p.size = sizeof(sco_surface_pose);
        p.panel_id = panel_id;
        return p;
    }
    static void Place(sco_surface_pose& p, const double (&pos)[3], const double (&up)[3], double w, double h,
                      double max_distance_m, uint32_t flags) noexcept {
        for (int i = 0; i < 3; ++i) {
            p.pos[i] = pos[i];
            p.up[i] = up[i];
        }
        p.width_m = w;
        p.height_m = h;
        p.max_distance_m = max_distance_m;
        p.flags = flags;
    }

    const sco_ui_surfaces_v1* table_ = nullptr;
    sco_plugin* self_ = nullptr;
};
} // namespace sco::sdk
#endif
