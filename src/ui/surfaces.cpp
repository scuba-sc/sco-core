// sco.ui.surfaces: the validated registry behind the C table of sco_ui_surfaces.h
// (sco/surfaces.h). sco-core projects and draws nothing; the product's renderer reads Surfaces().
//
// One lock, g_lock, guards everything below. It is never held while calling into sco.ui or the
// host registry, and plugin input is copied and validated before it is taken.
#include "sco/surfaces.h"
#include "sco/host.h"
#include "sco/ui.h"
#include "../api/internal.h"
#include <algorithm>
#include <cmath>
#include <cstring>
#include <mutex>
#include <string>
#include <utility>
#include <vector>

namespace sco::surfaces {

namespace {

struct Entry {
    uint32_t    id = 0;
    uint32_t    kind = 0;
    const void* owner = nullptr;
    std::string ownerId;
    Pose        pose;
};

std::mutex                                       g_lock;
bool                                             g_started = false;
std::vector<Entry>                               g_surfaces;   // ascending id
std::vector<std::pair<const void*, std::string>> g_errors;     // owner -> last refused call
uint32_t                                         g_lastId = 0;
uint64_t                                         g_revision = 0;

sco_result C(Result r) { return static_cast<sco_result>(static_cast<uint32_t>(r)); }

// Runs a table function: nothing throws across the C ABI (out of memory is SCO_TOO_MANY).
template <class F>
sco_result Guard(F&& f) noexcept {
    try {
        return C(f());
    } catch (...) {
        return SCO_TOO_MANY;
    }
}

// ---- validation -----------------------------------------------------------------------------------

bool Finite3(const double v[3]) { return std::isfinite(v[0]) && std::isfinite(v[1]) && std::isfinite(v[2]); }

bool Zero3(const double v[3]) {
    return std::fpclassify(v[0]) == FP_ZERO && std::fpclassify(v[1]) == FP_ZERO && std::fpclassify(v[2]) == FP_ZERO;
}

double Dot(const double a[3], const double b[3]) { return a[0] * b[0] + a[1] * b[1] + a[2] * b[2]; }

bool Unit3(const double v[3]) { return std::fabs(std::sqrt(Dot(v, v)) - 1.0) <= SCO_UI_SURFACES_UNIT_TOLERANCE; }

bool InRange(double x, double lo, double hi) { return x >= lo && x <= hi; }

bool IsZero(double x) { return std::fpclassify(x) == FP_ZERO; }

// "<x>.<y>": [a-z0-9_.], 1-max bytes, at least one '.', none leading, trailing or doubled.
bool ValidDotted(const std::string& s, size_t max) {
    if (s.empty() || s.size() > max || s.front() == '.' || s.back() == '.') return false;
    bool dot = false;
    for (size_t i = 0; i < s.size(); ++i) {
        const char c = s[i];
        if (c == '.') {
            if (s[i - 1] == '.') return false;
            dot = true;
        } else if (!((c >= 'a' && c <= 'z') || (c >= '0' && c <= '9') || c == '_')) {
            return false;
        }
    }
    return dot;
}

bool OwnPanel(const std::string& id, const char* pluginId) {
    const size_t n = std::strlen(pluginId);
    return id.size() > n + 1 && id.compare(0, n, pluginId) == 0 && id[n] == '.';
}

const char* KindName(uint32_t kind) {
    switch (kind) {
        case SCO_SURFACE_HUD_2D: return "hud_2d";
        case SCO_SURFACE_BILLBOARD_3D: return "billboard_3d";
        case SCO_SURFACE_QUAD_3D: return "quad_3d";
        default: return "?";
    }
}

// The 3D rules shared by billboards and quads; normal is checked by the caller.
bool Check3D(const sco_surface_pose& p, std::string& why) {
    for (const double c : p.pos)
        if (std::fabs(c) > SCO_UI_SURFACES_MAX_COORD_M) { why = "pos is beyond SCO_UI_SURFACES_MAX_COORD_M"; return false; }
    if (!(p.width_m > 0.0 && p.width_m <= SCO_UI_SURFACES_MAX_EXTENT_M) ||
        !(p.height_m > 0.0 && p.height_m <= SCO_UI_SURFACES_MAX_EXTENT_M)) {
        why = "width_m and height_m must be in (0, SCO_UI_SURFACES_MAX_EXTENT_M]";
        return false;
    }
    if (!InRange(p.max_distance_m, 0.0, SCO_UI_SURFACES_MAX_DISTANCE_M)) {
        why = "max_distance_m must be in [0, SCO_UI_SURFACES_MAX_DISTANCE_M]";
        return false;
    }
    if (!Unit3(p.up)) { why = "up is not a unit vector"; return false; }
    return true;
}

// ---- entering a call ------------------------------------------------------------------------------

// Records owner's last refused call (g_lock held).
Result Refuse(const void* owner, Result r, std::string why) {
    for (auto& e : g_errors)
        if (e.first == owner) { e.second = std::move(why); return r; }
    g_errors.emplace_back(owner, std::move(why));
    return r;
}

// Under g_lock: Ok when the service runs and self may still call.
Result Live(sco_plugin* self) {
    if (!g_started) return Result::Unavailable;
    if (sco::detail::Released(self)) return Result::BadArg;
    return Result::Ok;
}

Entry* Find(uint32_t id) {
    const auto it = std::lower_bound(g_surfaces.begin(), g_surfaces.end(), id,
                                     [](const Entry& e, uint32_t v) { return e.id < v; });
    return it != g_surfaces.end() && it->id == id ? &*it : nullptr;
}

size_t OwnedCount(const void* owner) {
    return static_cast<size_t>(std::count_if(g_surfaces.begin(), g_surfaces.end(),
                                             [owner](const Entry& e) { return e.owner == owner; }));
}

// CheckPose plus the panel-ownership rule. why is prefixed by the caller.
Result CheckOwnedPose(uint32_t kind, const sco_surface_pose* pose, const char* pluginId, Pose& out, std::string& why) {
    if (CheckPose(kind, pose, out, why) != Result::Ok) return Result::BadArg;
    if (!OwnPanel(out.panelId, pluginId)) {
        why = "panel_id '" + out.panelId + "' is not '" + pluginId + ".<name>'";
        return Result::BadArg;
    }
    return Result::Ok;
}

// ---- the table ------------------------------------------------------------------------------------

sco_result RegisterSurface(sco_plugin* self, const sco_surface_desc* desc, uint32_t* outId) {
    return Guard([&] {
        const char* pid = host::PluginId(self);
        if (!pid) return Result::BadArg;
        std::string why;
        Pose pose;
        uint32_t kind = 0;
        Result checked = Result::Ok;
        if (!desc) {
            why = "desc is NULL";
            checked = Result::BadArg;
        } else {
            sco_surface_desc d{};
            std::memcpy(&d.size, &desc->size, sizeof(d.size));
            if (d.size < sizeof(sco_surface_desc)) {
                why = "desc.size " + std::to_string(d.size) + " is below " + std::to_string(sizeof(sco_surface_desc));
                checked = Result::BadArg;
            } else {
                std::memcpy(&d, desc, sizeof(d));
                kind = d.kind;
                checked = CheckOwnedPose(kind, d.pose, pid, pose, why);
            }
        }
        if (checked == Result::Ok && !outId) {
            why = "out_id is NULL";
            checked = Result::BadArg;
        }
        std::lock_guard<std::mutex> hold(g_lock);
        if (const Result r = Live(self); r != Result::Ok) return r;
        if (checked != Result::Ok) return Refuse(self, checked, "surface: " + why);
        if (OwnedCount(self) >= kMaxPerPlugin)
            return Refuse(self, Result::TooMany, "too many surfaces for this plugin (" + std::to_string(kMaxPerPlugin) + ")");
        if (g_surfaces.size() >= kMaxSurfaces)
            return Refuse(self, Result::TooMany, "too many surfaces (" + std::to_string(kMaxSurfaces) + ")");
        if (g_lastId == UINT32_MAX) return Refuse(self, Result::TooMany, "surface ids exhausted");
        Entry e;
        e.id = ++g_lastId;
        e.kind = kind;
        e.owner = self;
        e.ownerId = pid;
        e.pose = std::move(pose);
        g_surfaces.push_back(std::move(e));
        ++g_revision;
        *outId = g_lastId;
        return Result::Ok;
    });
}

sco_result UpdateSurfacePose(sco_plugin* self, uint32_t id, const sco_surface_pose* pose) {
    return Guard([&] {
        const char* pid = host::PluginId(self);
        if (!pid) return Result::BadArg;
        uint32_t kind = 0;
        {
            std::lock_guard<std::mutex> hold(g_lock);
            if (const Result r = Live(self); r != Result::Ok) return r;
            const Entry* e = Find(id);
            if (!e || e->owner != self)
                return Refuse(self, Result::NotFound, "no surface " + std::to_string(id) + " of this plugin");
            kind = e->kind;
        }
        std::string why;
        Pose copy;
        const Result checked = CheckOwnedPose(kind, pose, pid, copy, why);
        std::lock_guard<std::mutex> hold(g_lock);
        if (const Result r = Live(self); r != Result::Ok) return r;
        Entry* e = Find(id);
        if (!e || e->owner != self)   // unregistered meanwhile
            return Refuse(self, Result::NotFound, "no surface " + std::to_string(id) + " of this plugin");
        if (checked != Result::Ok) return Refuse(self, checked, "surface " + std::to_string(id) + ": " + why);
        e->pose = std::move(copy);
        ++g_revision;
        return Result::Ok;
    });
}

sco_result UnregisterSurface(sco_plugin* self, uint32_t id) {
    return Guard([&] {
        if (!host::PluginId(self)) return Result::BadArg;
        std::lock_guard<std::mutex> hold(g_lock);
        if (const Result r = Live(self); r != Result::Ok) return r;
        const Entry* e = Find(id);
        if (!e || e->owner != self)
            return Refuse(self, Result::NotFound, "no surface " + std::to_string(id) + " of this plugin");
        g_surfaces.erase(g_surfaces.begin() + (e - g_surfaces.data()));
        ++g_revision;
        return Result::Ok;
    });
}

sco_result LastError(sco_plugin* self, char* out, uint32_t* io) {
    return Guard([&] {
        if (!io) return Result::BadArg;
        const uint32_t capacity = *io;
        *io = 0;
        if ((!out && capacity) || !host::PluginId(self)) return Result::BadArg;
        std::string e;
        {
            std::lock_guard<std::mutex> hold(g_lock);
            if (const Result r = Live(self); r != Result::Ok) return r;
            for (const auto& x : g_errors)
                if (x.first == self) e = x.second;
        }
        const size_t need = e.size() + 1;
        *io = static_cast<uint32_t>(need);
        if (capacity < need) return Result::TooMany;
        std::memcpy(out, e.c_str(), need);
        return Result::Ok;
    });
}

const sco_ui_surfaces_v1 kTable = {
    sizeof(sco_ui_surfaces_v1), 0,
    RegisterSurface, UpdateSurfacePose, UnregisterSurface,
    LastError,
};

}  // namespace

Result CheckPose(uint32_t kind, const sco_surface_pose* pose, Pose& out, std::string& why) {
    if (kind != SCO_SURFACE_HUD_2D && kind != SCO_SURFACE_BILLBOARD_3D && kind != SCO_SURFACE_QUAD_3D) {
        why = "unknown kind " + std::to_string(kind);
        return Result::BadArg;
    }
    if (!pose) { why = "pose is NULL"; return Result::BadArg; }
    uint32_t size = 0;
    std::memcpy(&size, &pose->size, sizeof(size));
    if (size < sizeof(sco_surface_pose)) {
        why = "pose.size " + std::to_string(size) + " is below " + std::to_string(sizeof(sco_surface_pose));
        return Result::BadArg;
    }
    sco_surface_pose p;   // read once: the plugin may write its struct from another thread
    std::memcpy(&p, pose, sizeof(p));
    const std::string kindName = KindName(kind);
    if (p.flags & ~SCO_SURFACE_OCCLUDE_RAYCAST) { why = "unknown flags"; return Result::BadArg; }
    if (!Finite3(p.pos) || !Finite3(p.normal) || !Finite3(p.up) || !std::isfinite(p.width_m) ||
        !std::isfinite(p.height_m) || !std::isfinite(p.max_distance_m)) {
        why = "every number must be finite";
        return Result::BadArg;
    }
    switch (kind) {
        case SCO_SURFACE_HUD_2D:
            if (!InRange(p.pos[0], 0.0, 1.0) || !InRange(p.pos[1], 0.0, 1.0) || !IsZero(p.pos[2])) {
                why = kindName + ": pos[0], pos[1] must be in [0, 1] and pos[2] 0";
                return Result::BadArg;
            }
            if (!(p.width_m > 0.0 && p.width_m <= 1.0) || !(p.height_m > 0.0 && p.height_m <= 1.0)) {
                why = kindName + ": width_m and height_m are viewport fractions in (0, 1]";
                return Result::BadArg;
            }
            if (!Zero3(p.normal) || !Zero3(p.up)) { why = kindName + ": normal and up must be zero"; return Result::BadArg; }
            if (!IsZero(p.max_distance_m)) { why = kindName + ": max_distance_m must be 0"; return Result::BadArg; }
            if (p.flags) { why = kindName + ": takes no flags"; return Result::BadArg; }
            break;
        case SCO_SURFACE_BILLBOARD_3D:
            if (!Zero3(p.normal)) { why = kindName + ": normal must be zero (it faces the camera)"; return Result::BadArg; }
            if (!Check3D(p, why)) { why = kindName + ": " + why; return Result::BadArg; }
            break;
        default:   // SCO_SURFACE_QUAD_3D
            if (!Unit3(p.normal)) { why = kindName + ": normal is not a unit vector"; return Result::BadArg; }
            if (!Check3D(p, why)) { why = kindName + ": " + why; return Result::BadArg; }
            if (std::fabs(Dot(p.normal, p.up)) > SCO_UI_SURFACES_UNIT_TOLERANCE) {
                why = kindName + ": up is not perpendicular to normal";
                return Result::BadArg;
            }
            break;
    }
    std::string panel;
    if (p.panel_id) {
        const size_t n = strnlen(p.panel_id, SCO_UI_SURFACES_MAX_PANEL_ID + 1);
        if (n <= SCO_UI_SURFACES_MAX_PANEL_ID) panel.assign(p.panel_id, n);
    }
    if (!ValidDotted(panel, SCO_UI_SURFACES_MAX_PANEL_ID)) {
        why = "panel_id is not '<plugin id>.<name>' ([a-z0-9_.], at most 63 bytes)";
        return Result::BadArg;
    }
    Pose v;
    v.flags = p.flags;
    for (size_t i = 0; i < 3; ++i) {
        v.pos[i] = p.pos[i];
        v.normal[i] = p.normal[i];
        v.up[i] = p.up[i];
    }
    v.widthM = p.width_m;
    v.heightM = p.height_m;
    v.maxDistanceM = p.max_distance_m;
    v.panelId = std::move(panel);
    out = std::move(v);
    return Result::Ok;
}

Result Start() {
    if (!ui::Started()) return Result::Unavailable;
    {
        std::lock_guard<std::mutex> hold(g_lock);
        if (g_started) return Result::BadArg;
        g_started = true;
    }
    const Result r = host::ProvideHostService(SCO_UI_SURFACES_NAME, SCO_UI_SURFACES_VERSION_1_0, &kTable);
    if (r != Result::Ok) {
        std::lock_guard<std::mutex> hold(g_lock);
        g_started = false;
    }
    return r;
}

void Stop() {
    {
        std::lock_guard<std::mutex> hold(g_lock);
        if (!g_started) return;
        g_started = false;
        g_surfaces.clear();
        g_errors.clear();
        ++g_revision;
    }
    host::WithdrawHostService(SCO_UI_SURFACES_NAME);
}

bool Started() {
    std::lock_guard<std::mutex> hold(g_lock);
    return g_started;
}

const sco_ui_surfaces_v1* Table() { return &kTable; }

std::vector<SurfaceInfo> Surfaces() {
    std::lock_guard<std::mutex> hold(g_lock);
    std::vector<SurfaceInfo> out;
    out.reserve(g_surfaces.size());
    for (const Entry& e : g_surfaces) out.push_back({ e.id, e.kind, e.ownerId, e.pose });
    return out;
}

uint64_t Revision() {
    std::lock_guard<std::mutex> hold(g_lock);
    return g_revision;
}

namespace detail {
void ReleaseOwner(const void* owner) {
    std::lock_guard<std::mutex> hold(g_lock);
    const size_t before = g_surfaces.size();
    g_surfaces.erase(std::remove_if(g_surfaces.begin(), g_surfaces.end(), [owner](const Entry& e) { return e.owner == owner; }),
                     g_surfaces.end());
    if (g_surfaces.size() != before) ++g_revision;
    g_errors.erase(std::remove_if(g_errors.begin(), g_errors.end(), [owner](const auto& e) { return e.first == owner; }),
                   g_errors.end());
}
}  // namespace detail

}  // namespace sco::surfaces
