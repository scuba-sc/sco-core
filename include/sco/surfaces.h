#pragma once
// sco::surfaces: the host side of the "sco.ui.surfaces" service (include/sco_ui_surfaces.h): the
// validated registry of where plugin panels are shown (2D HUD rectangles, camera-facing 3D
// billboards, world-anchored 3D quads). C++ and internal: the product's renderer reads the
// registry and projects each surface itself (docs/ui.md, "Surfaces"); plugins only see the C
// table. sco-core has no renderer.
//
//   sco::ui::Start();                                    // owner cleanup rides sco.ui's release hook
//   sco::surfaces::Start();                              // publishes sco.ui.surfaces 1.0 (sco::app::Start does)
//   ... each frame, in the renderer:
//   if (sco::surfaces::Revision() != seen) { list = sco::surfaces::Surfaces(); seen = ...; }
//   for (const sco::surfaces::SurfaceInfo& s : list) ProjectAndDraw(s);   // the product's code
//   sco::surfaces::Stop();                               // after every plugin unloaded, before ui::Stop
//
// Owners are runtime owners: a plugin's sco_plugin handle. Release(owner) withdraws every surface
// it registered (through sco.ui's release hook, so this service takes no hook slot of its own).
#include "sco_ui_surfaces.h"
#include "sco/runtime.h"
#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

namespace sco::surfaces {

constexpr size_t kMaxSurfaces = SCO_UI_SURFACES_MAX_TOTAL;
constexpr size_t kMaxPerPlugin = SCO_UI_SURFACES_MAX_PER_PLUGIN;

// Publishes "sco.ui.surfaces" 1.0 under the host's id. Any thread. Unavailable: sco.ui is not
// started (its release hook withdraws surfaces on unload). BadArg: already started, or the name
// already published. Nothing changes unless Ok.
Result Start();

// Withdraws "sco.ui.surfaces" and clears every surface and error message; later calls through
// the table answer SCO_UNAVAILABLE. No-op unless started. Any thread.
void Stop();

bool Started();

// The service table (what query_service hands out). Valid for the life of the process.
const sco_ui_surfaces_v1* Table();

// A validated pose, copied out of the plugin's sco_surface_pose.
struct Pose {
    uint32_t    flags = 0;
    double      pos[3] = { 0, 0, 0 };
    double      normal[3] = { 0, 0, 0 };
    double      up[3] = { 0, 0, 0 };
    double      widthM = 0;
    double      heightM = 0;
    double      maxDistanceM = 0;
    std::string panelId;
};

struct SurfaceInfo {
    uint32_t    id = 0;
    uint32_t    kind = 0;   // SCO_SURFACE_*
    std::string owner;      // the plugin id
    Pose        pose;
};

// Every live surface, by ascending id. Any thread; a snapshot.
std::vector<SurfaceInfo> Surfaces();

// Grows on every register, pose update, unregister, owner release and Stop, so a renderer can
// skip rebuilding its list when nothing changed. Any thread.
uint64_t Revision();

// The rules of sco_ui_surfaces.h for kind and pose, without the panel-ownership check. Ok and
// out filled; BadArg (out untouched) with why set. Used by the table; exposed for products
// and tests that build poses themselves. Any thread.
Result CheckPose(uint32_t kind, const sco_surface_pose* pose, Pose& out, std::string& why);

namespace detail {
// Withdraws every surface of owner. Called by sco.ui's release hook (src/ui/ui.cpp).
void ReleaseOwner(const void* owner);
}  // namespace detail

}  // namespace sco::surfaces
