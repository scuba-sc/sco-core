// SPDX-License-Identifier: GPL-3.0-only
// Unit tests for the sco.ui.surfaces service (sco/surfaces.h, sco_ui_surfaces.h,
// scosdk/ui_surfaces.hpp) through the real host table. Host build, no game, no renderer. The
// "game thread" is this test's main thread.
//   test_surfaces                             (tools/test.sh under ASan+UBSan and TSan, CTest test_surfaces)
#include "sco/host.h"
#include "sco/log.h"
#include "sco/runtime.h"
#include "sco/surfaces.h"
#include "sco/ui.h"
#include "sco_api.h"
#include "sco_ui_surfaces.h"
#include "scosdk/ui_surfaces.hpp"
#include <atomic>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <limits>
#include <string>
#include <thread>
#include <vector>

using sco::Result;

static std::atomic<int> g_fail{ 0 }, g_pass{ 0 };
#define CHECK(c) do { if (c) ++g_pass; else { ++g_fail; std::printf("FAIL %s:%d: %s\n", __FILE__, __LINE__, #c); } } while (0)

static void Sink(const char*) {}

static const sco_api*            g_api = nullptr;
static const sco_ui_surfaces_v1* g_s = nullptr;
static sco_plugin*               g_alpha = nullptr;
static sco_plugin*               g_beta = nullptr;

static std::string LastError(sco_plugin* p) {
    char buf[256];
    uint32_t n = sizeof(buf);
    const sco_result r = g_s->last_error(p, buf, &n);
    return r == SCO_OK ? std::string(buf) : "<" + std::to_string(r) + ">";
}

static sco_surface_pose Hud(const char* panel = "alpha.main") {
    sco_surface_pose p{};
    p.size = sizeof(p);
    p.pos[0] = 0.5; p.pos[1] = 0.25;
    p.width_m = 0.3; p.height_m = 0.1;
    p.panel_id = panel;
    return p;
}

static sco_surface_pose Billboard(const char* panel = "alpha.main") {
    sco_surface_pose p{};
    p.size = sizeof(p);
    p.pos[0] = 100.0; p.pos[1] = -20.0; p.pos[2] = 3.5;
    p.up[2] = 1.0;
    p.width_m = 2.0; p.height_m = 1.0;
    p.max_distance_m = 500.0;
    p.flags = SCO_SURFACE_OCCLUDE_RAYCAST;
    p.panel_id = panel;
    return p;
}

static sco_surface_pose Quad(const char* panel = "alpha.main") {
    sco_surface_pose p = Billboard(panel);
    p.normal[0] = 1.0;
    p.up[0] = 0.0; p.up[1] = 0.0; p.up[2] = 1.0;
    return p;
}

static sco_result Register(sco_plugin* self, uint32_t kind, const sco_surface_pose& pose, uint32_t* id) {
    const sco_surface_desc desc{ sizeof(sco_surface_desc), kind, &pose };
    return g_s->register_surface(self, &desc, id);
}

// ---- lifecycle -------------------------------------------------------------------------------

static void TestStart() {
    const void* t = nullptr;
    CHECK(sco::surfaces::Start() == Result::Unavailable);   // sco.ui not started
    CHECK(g_api->query_service(SCO_UI_SURFACES_NAME, SCO_UI_SURFACES_VERSION_1_0, &t) == SCO_NOT_FOUND);
    CHECK(sco::ui::Start() == Result::Ok);
    CHECK(sco::surfaces::Start() == Result::Ok);
    CHECK(sco::surfaces::Start() == Result::BadArg);   // already
    CHECK(sco::surfaces::Started());
    CHECK(g_api->query_service(SCO_UI_SURFACES_NAME, SCO_UI_SURFACES_VERSION_1_0, &t) == SCO_OK &&
          t == sco::surfaces::Table());
    CHECK(g_api->query_service(SCO_UI_SURFACES_NAME, 0x00010001, &t) == SCO_UNAVAILABLE);
    g_s = sco::surfaces::Table();
    CHECK(g_s->size == sizeof(sco_ui_surfaces_v1));
    CHECK(g_s->register_surface && g_s->update_surface_pose && g_s->unregister_surface && g_s->last_error);
}

// ---- register: every kind, ids, snapshot -----------------------------------------------------

static void TestRegister() {
    const uint64_t rev0 = sco::surfaces::Revision();
    uint32_t hud = 0, bb = 0, quad = 0;
    CHECK(Register(g_alpha, SCO_SURFACE_HUD_2D, Hud(), &hud) == SCO_OK && hud != 0);
    CHECK(Register(g_alpha, SCO_SURFACE_BILLBOARD_3D, Billboard(), &bb) == SCO_OK && bb > hud);
    CHECK(Register(g_alpha, SCO_SURFACE_QUAD_3D, Quad(), &quad) == SCO_OK && quad > bb);
    CHECK(sco::surfaces::Revision() == rev0 + 3);
    const auto list = sco::surfaces::Surfaces();
    CHECK(list.size() == 3);
    CHECK(list.size() == 3 && list[0].id == hud && list[1].id == bb && list[2].id == quad);
    CHECK(list.size() == 3 && list[0].kind == SCO_SURFACE_HUD_2D && list[0].owner == "alpha" &&
          list[0].pose.panelId == "alpha.main" && list[0].pose.pos[0] == 0.5 && list[0].pose.widthM == 0.3);
    CHECK(list.size() == 3 && list[1].kind == SCO_SURFACE_BILLBOARD_3D && list[1].pose.flags == SCO_SURFACE_OCCLUDE_RAYCAST &&
          list[1].pose.maxDistanceM == 500.0 && list[1].pose.up[2] == 1.0);
    CHECK(list.size() == 3 && list[2].kind == SCO_SURFACE_QUAD_3D && list[2].pose.normal[0] == 1.0);

    // A larger caller struct (a newer header) is accepted; its tail is ignored.
    struct Bigger { sco_surface_pose base; double extra[4]; } big{};
    big.base = Hud();
    big.base.size = sizeof(big);
    big.extra[0] = std::numeric_limits<double>::quiet_NaN();
    uint32_t id = 0;
    CHECK(Register(g_alpha, SCO_SURFACE_HUD_2D, big.base, &id) == SCO_OK);
    CHECK(g_s->unregister_surface(g_alpha, id) == SCO_OK);

    for (const auto& s : list) CHECK(g_s->unregister_surface(g_alpha, s.id) == SCO_OK);
    CHECK(sco::surfaces::Surfaces().empty());
}

// ---- validation: every field, nothing changes on refusal -------------------------------------

struct Bad {
    const char*      what;
    uint32_t         kind;
    sco_surface_pose pose;
};

static std::vector<Bad> BadPoses() {
    const double nan = std::numeric_limits<double>::quiet_NaN();
    const double inf = std::numeric_limits<double>::infinity();
    std::vector<Bad> v;
    auto add = [&](const char* what, uint32_t kind, sco_surface_pose p) { v.push_back({ what, kind, p }); };
    sco_surface_pose p;

    p = Hud(); p.size = sizeof(p) - 8;              add("pose.size short", SCO_SURFACE_HUD_2D, p);
    p = Hud();                                      add("kind 0", 0, p);
    p = Hud();                                      add("kind 4", 4, p);
    p = Hud(); p.pos[0] = nan;                      add("hud NaN", SCO_SURFACE_HUD_2D, p);
    p = Hud(); p.pos[0] = 1.5;                      add("hud x>1", SCO_SURFACE_HUD_2D, p);
    p = Hud(); p.pos[1] = -0.1;                     add("hud y<0", SCO_SURFACE_HUD_2D, p);
    p = Hud(); p.pos[2] = 0.5;                      add("hud z", SCO_SURFACE_HUD_2D, p);
    p = Hud(); p.width_m = 0;                       add("hud width 0", SCO_SURFACE_HUD_2D, p);
    p = Hud(); p.height_m = 1.01;                   add("hud height>1", SCO_SURFACE_HUD_2D, p);
    p = Hud(); p.normal[2] = 1;                     add("hud normal", SCO_SURFACE_HUD_2D, p);
    p = Hud(); p.up[1] = 1;                         add("hud up", SCO_SURFACE_HUD_2D, p);
    p = Hud(); p.max_distance_m = 10;               add("hud distance", SCO_SURFACE_HUD_2D, p);
    p = Hud(); p.flags = SCO_SURFACE_OCCLUDE_RAYCAST; add("hud flags", SCO_SURFACE_HUD_2D, p);
    p = Billboard(); p.normal[0] = 1;               add("billboard normal", SCO_SURFACE_BILLBOARD_3D, p);
    p = Billboard(); p.up[2] = 2;                   add("billboard up length", SCO_SURFACE_BILLBOARD_3D, p);
    p = Billboard(); p.up[2] = 0;                   add("billboard up zero", SCO_SURFACE_BILLBOARD_3D, p);
    p = Billboard(); p.up[2] = inf;                 add("billboard up inf", SCO_SURFACE_BILLBOARD_3D, p);
    p = Billboard(); p.width_m = 0;                 add("billboard width 0", SCO_SURFACE_BILLBOARD_3D, p);
    p = Billboard(); p.height_m = -1;               add("billboard height<0", SCO_SURFACE_BILLBOARD_3D, p);
    p = Billboard(); p.width_m = 1000.5;            add("billboard width big", SCO_SURFACE_BILLBOARD_3D, p);
    p = Billboard(); p.max_distance_m = -1;         add("billboard distance<0", SCO_SURFACE_BILLBOARD_3D, p);
    p = Billboard(); p.max_distance_m = 2e7;        add("billboard distance big", SCO_SURFACE_BILLBOARD_3D, p);
    p = Billboard(); p.max_distance_m = nan;        add("billboard distance NaN", SCO_SURFACE_BILLBOARD_3D, p);
    p = Billboard(); p.pos[1] = -2e12;              add("billboard coord big", SCO_SURFACE_BILLBOARD_3D, p);
    p = Billboard(); p.flags = 0x2;                 add("billboard flag", SCO_SURFACE_BILLBOARD_3D, p);
    p = Quad(); p.normal[0] = 0.5;                  add("quad normal length", SCO_SURFACE_QUAD_3D, p);
    p = Quad(); p.normal[0] = 0;                    add("quad normal zero", SCO_SURFACE_QUAD_3D, p);
    p = Quad(); p.up[0] = 1; p.up[2] = 0;           add("quad up parallel", SCO_SURFACE_QUAD_3D, p);
    p = Quad(); p.up[0] = 0.1; p.up[2] = std::sqrt(1 - 0.01); add("quad up tilted", SCO_SURFACE_QUAD_3D, p);
    p = Hud(nullptr);                               add("panel NULL", SCO_SURFACE_HUD_2D, p);
    p = Hud("beta.main");                           add("panel of another plugin", SCO_SURFACE_HUD_2D, p);
    p = Hud("alpha");                               add("panel without name", SCO_SURFACE_HUD_2D, p);
    p = Hud("alpha.Main");                          add("panel upper case", SCO_SURFACE_HUD_2D, p);
    p = Hud("alpha..main");                         add("panel doubled dot", SCO_SURFACE_HUD_2D, p);
    p = Hud("alphabet.main");                       add("panel prefix only", SCO_SURFACE_HUD_2D, p);
    static const std::string kLong = "alpha." + std::string(58, 'x');   // 64 bytes
    p = Hud(kLong.c_str());                         add("panel 64 bytes", SCO_SURFACE_HUD_2D, p);
    return v;
}

static void TestValidation() {
    const uint64_t rev = sco::surfaces::Revision();
    for (const Bad& b : BadPoses()) {
        uint32_t id = 0xdeadu;
        const sco_result r = Register(g_alpha, b.kind, b.pose, &id);
        if (r != SCO_BAD_ARG) std::printf("  accepted: %s\n", b.what);
        CHECK(r == SCO_BAD_ARG);
        CHECK(id == 0xdeadu);
        CHECK(LastError(g_alpha).rfind("surface: ", 0) == 0);
    }
    CHECK(sco::surfaces::Revision() == rev);   // no refusal changed anything
    // Exactly at the limits is fine.
    static const std::string kMax = "alpha." + std::string(57, 'x');   // 63 bytes
    sco_surface_pose edge = Billboard(kMax.c_str());
    edge.width_m = SCO_UI_SURFACES_MAX_EXTENT_M;
    edge.max_distance_m = SCO_UI_SURFACES_MAX_DISTANCE_M;
    edge.pos[0] = -SCO_UI_SURFACES_MAX_COORD_M;
    edge.up[2] = 1.0 + 0.5e-3;
    uint32_t id = 0;
    CHECK(Register(g_alpha, SCO_SURFACE_BILLBOARD_3D, edge, &id) == SCO_OK);
    CHECK(g_s->unregister_surface(g_alpha, id) == SCO_OK);
    sco_surface_pose corner = Hud();
    corner.pos[0] = 0; corner.pos[1] = 1; corner.width_m = 1; corner.height_m = 1;
    CHECK(Register(g_alpha, SCO_SURFACE_HUD_2D, corner, &id) == SCO_OK);
    CHECK(g_s->unregister_surface(g_alpha, id) == SCO_OK);

    // desc and out_id themselves.
    const uint64_t revRefusals = sco::surfaces::Revision();
    const sco_surface_pose ok = Hud();
    CHECK(g_s->register_surface(g_alpha, nullptr, &id) == SCO_BAD_ARG);
    CHECK(LastError(g_alpha) == "surface: desc is NULL");
    sco_surface_desc desc{ sizeof(sco_surface_desc) - 8, SCO_SURFACE_HUD_2D, &ok };
    CHECK(g_s->register_surface(g_alpha, &desc, &id) == SCO_BAD_ARG);
    desc.size = sizeof(sco_surface_desc);
    desc.pose = nullptr;
    CHECK(g_s->register_surface(g_alpha, &desc, &id) == SCO_BAD_ARG);
    CHECK(LastError(g_alpha) == "surface: pose is NULL");
    desc.pose = &ok;
    CHECK(g_s->register_surface(g_alpha, &desc, nullptr) == SCO_BAD_ARG);
    CHECK(LastError(g_alpha) == "surface: out_id is NULL");
    sco_surface_pose upBad = Billboard();
    upBad.up[2] = 2;
    CHECK(Register(g_alpha, SCO_SURFACE_BILLBOARD_3D, upBad, &id) == SCO_BAD_ARG);
    CHECK(LastError(g_alpha) == "surface: billboard_3d: up is not a unit vector");

    // A pointer NewPlugin didn't return.
    int fake = 0;
    CHECK(Register(reinterpret_cast<sco_plugin*>(&fake), SCO_SURFACE_HUD_2D, ok, &id) == SCO_BAD_ARG);

    CHECK(sco::surfaces::Surfaces().empty());
    CHECK(sco::surfaces::Revision() == revRefusals);   // nothing changed

    // CheckPose, as a renderer or product would call it.
    sco::surfaces::Pose out;
    std::string why;
    const sco_surface_pose q = Quad();
    CHECK(sco::surfaces::CheckPose(SCO_SURFACE_QUAD_3D, &q, out, why) == Result::Ok && out.normal[0] == 1.0);
    CHECK(sco::surfaces::CheckPose(SCO_SURFACE_BILLBOARD_3D, &q, out, why) == Result::BadArg && !why.empty());
}

// ---- update and unregister ---------------------------------------------------------------------

static void TestUpdate() {
    uint32_t quad = 0, betaHud = 0;
    CHECK(Register(g_alpha, SCO_SURFACE_QUAD_3D, Quad(), &quad) == SCO_OK);
    CHECK(Register(g_beta, SCO_SURFACE_HUD_2D, Hud("beta.hud"), &betaHud) == SCO_OK);

    sco_surface_pose moved = Quad("alpha.other");
    moved.pos[0] = -5.0;
    moved.normal[0] = 0; moved.normal[1] = 1;
    const uint64_t rev = sco::surfaces::Revision();
    CHECK(g_s->update_surface_pose(g_alpha, quad, &moved) == SCO_OK);
    CHECK(sco::surfaces::Revision() == rev + 1);
    auto list = sco::surfaces::Surfaces();
    CHECK(list.size() == 2 && list[0].pose.pos[0] == -5.0 && list[0].pose.normal[1] == 1.0 &&
          list[0].pose.panelId == "alpha.other");

    // The pose is checked against the surface's kind; a refusal keeps the old pose.
    const sco_surface_pose hud = Hud();
    CHECK(g_s->update_surface_pose(g_alpha, quad, &hud) == SCO_BAD_ARG);
    CHECK(LastError(g_alpha).rfind("surface " + std::to_string(quad) + ": quad_3d: ", 0) == 0);
    CHECK(g_s->update_surface_pose(g_alpha, quad, nullptr) == SCO_BAD_ARG);
    list = sco::surfaces::Surfaces();
    CHECK(list.size() == 2 && list[0].pose.pos[0] == -5.0);
    CHECK(sco::surfaces::Revision() == rev + 1);

    // Ownership: ids of other plugins and unknown ids are NOT_FOUND, for update and unregister.
    CHECK(g_s->update_surface_pose(g_alpha, betaHud, &hud) == SCO_NOT_FOUND);
    CHECK(g_s->update_surface_pose(g_beta, quad, &moved) == SCO_NOT_FOUND);
    CHECK(g_s->update_surface_pose(g_alpha, 0, &moved) == SCO_NOT_FOUND);
    CHECK(g_s->unregister_surface(g_beta, quad) == SCO_NOT_FOUND);
    CHECK(LastError(g_beta) == "no surface " + std::to_string(quad) + " of this plugin");
    CHECK(g_s->unregister_surface(g_alpha, 0xffffffffu) == SCO_NOT_FOUND);

    CHECK(g_s->unregister_surface(g_alpha, quad) == SCO_OK);
    CHECK(g_s->unregister_surface(g_alpha, quad) == SCO_NOT_FOUND);
    CHECK(g_s->unregister_surface(g_beta, betaHud) == SCO_OK);

    // Ids are never reused.
    uint32_t again = 0;
    CHECK(Register(g_alpha, SCO_SURFACE_QUAD_3D, Quad(), &again) == SCO_OK && again > betaHud);
    CHECK(g_s->unregister_surface(g_alpha, again) == SCO_OK);
}

// ---- limits ------------------------------------------------------------------------------------

static void TestLimits() {
    std::vector<uint32_t> ids;
    uint32_t id = 0;
    for (uint32_t k = 0; k < SCO_UI_SURFACES_MAX_PER_PLUGIN; ++k) {
        CHECK(Register(g_alpha, SCO_SURFACE_HUD_2D, Hud(), &id) == SCO_OK);
        ids.push_back(id);
    }
    CHECK(Register(g_alpha, SCO_SURFACE_HUD_2D, Hud(), &id) == SCO_TOO_MANY);
    CHECK(LastError(g_alpha) == "too many surfaces for this plugin (32)");
    CHECK(Register(g_beta, SCO_SURFACE_HUD_2D, Hud("beta.hud"), &id) == SCO_OK);   // per plugin
    CHECK(g_s->unregister_surface(g_beta, id) == SCO_OK);
    for (uint32_t k : ids) CHECK(g_s->unregister_surface(g_alpha, k) == SCO_OK);

    // The host-wide limit, over SCO_UI_SURFACES_MAX_TOTAL / MAX_PER_PLUGIN plugins.
    std::vector<sco_plugin*> owners;
    std::vector<std::string> panels;
    const uint32_t plugins = SCO_UI_SURFACES_MAX_TOTAL / SCO_UI_SURFACES_MAX_PER_PLUGIN;
    for (uint32_t p = 0; p < plugins; ++p) {
        const std::string name = "lim" + std::to_string(p);
        owners.push_back(sco::host::NewPlugin(name.c_str()));
        panels.push_back(name + ".hud");
    }
    for (uint32_t p = 0; p < plugins; ++p)
        for (uint32_t k = 0; k < SCO_UI_SURFACES_MAX_PER_PLUGIN; ++k)
            CHECK(Register(owners[p], SCO_SURFACE_HUD_2D, Hud(panels[p].c_str()), &id) == SCO_OK);
    CHECK(sco::surfaces::Surfaces().size() == SCO_UI_SURFACES_MAX_TOTAL);
    CHECK(Register(g_beta, SCO_SURFACE_HUD_2D, Hud("beta.hud"), &id) == SCO_TOO_MANY);
    CHECK(LastError(g_beta) == "too many surfaces (256)");

    // Release withdraws only the released owner's surfaces, through sco.ui's release hook.
    const uint64_t rev = sco::surfaces::Revision();
    CHECK(sco::Release(owners[0]) == Result::Ok);
    CHECK(sco::surfaces::Surfaces().size() == SCO_UI_SURFACES_MAX_TOTAL - SCO_UI_SURFACES_MAX_PER_PLUGIN);
    CHECK(sco::surfaces::Revision() > rev);
    CHECK(Register(owners[0], SCO_SURFACE_HUD_2D, Hud(panels[0].c_str()), &id) == SCO_BAD_ARG);   // released
    char buf[8];
    uint32_t n = sizeof(buf);
    CHECK(g_s->last_error(owners[0], buf, &n) == SCO_BAD_ARG);
    CHECK(Register(g_beta, SCO_SURFACE_HUD_2D, Hud("beta.hud"), &id) == SCO_OK);   // room again
    CHECK(g_s->unregister_surface(g_beta, id) == SCO_OK);
    for (size_t p = 1; p < owners.size(); ++p) CHECK(sco::Release(owners[p]) == Result::Ok);
    CHECK(sco::surfaces::Surfaces().empty());
}

// ---- last_error handshake ------------------------------------------------------------------------

static void TestLastError() {
    CHECK(g_s->unregister_surface(g_alpha, 12345) == SCO_NOT_FOUND);
    const std::string want = "no surface 12345 of this plugin";
    uint32_t n = 4;
    char small[4];
    CHECK(g_s->last_error(g_alpha, small, &n) == SCO_TOO_MANY && n == want.size() + 1);
    n = 0;
    CHECK(g_s->last_error(g_alpha, nullptr, &n) == SCO_TOO_MANY && n == want.size() + 1);
    CHECK(g_s->last_error(g_alpha, nullptr, nullptr) == SCO_BAD_ARG);
    n = 8;
    CHECK(g_s->last_error(g_alpha, nullptr, &n) == SCO_BAD_ARG);
    CHECK(LastError(g_alpha) == want);
}

// ---- threads: registrations beside a renderer's snapshots (TSan) ----------------------------------

static void TestThreads() {
    std::vector<sco_plugin*> owners;
    std::vector<std::string> panels;
    for (int t = 0; t < 4; ++t) {
        const std::string name = "th" + std::to_string(t);
        owners.push_back(sco::host::NewPlugin(name.c_str()));
        panels.push_back(name + ".main");
    }
    std::atomic<bool> done{ false };
    std::atomic<int> failures{ 0 };
    std::thread reader([&] {
        while (!done.load()) {
            for (const auto& s : sco::surfaces::Surfaces())
                if (s.id == 0 || s.pose.panelId.empty()) ++failures;
            (void)sco::surfaces::Revision();
        }
    });
    std::vector<std::thread> writers;
    for (int t = 0; t < 4; ++t)
        writers.emplace_back([&, t] {
            for (int k = 0; k < 200; ++k) {
                uint32_t id = 0;
                sco_surface_pose p = Billboard(panels[static_cast<size_t>(t)].c_str());
                if (Register(owners[static_cast<size_t>(t)], SCO_SURFACE_BILLBOARD_3D, p, &id) != SCO_OK) { ++failures; continue; }
                p.pos[0] = k;
                if (g_s->update_surface_pose(owners[static_cast<size_t>(t)], id, &p) != SCO_OK) ++failures;
                if (g_s->unregister_surface(owners[static_cast<size_t>(t)], id) != SCO_OK) ++failures;
            }
        });
    for (auto& w : writers) w.join();
    done = true;
    reader.join();
    CHECK(failures.load() == 0);
    CHECK(sco::surfaces::Surfaces().empty());
    for (sco_plugin* p : owners) CHECK(sco::Release(p) == Result::Ok);
}

// ---- the C++ wrapper --------------------------------------------------------------------------------

static void TestWrapper() {
    sco::sdk::UiSurfaces s;
    CHECK(s.Open(nullptr, g_alpha) == SCO_BAD_ARG && !s);
    CHECK(s.Open(g_api, g_alpha) == SCO_OK && s && s.Table() == g_s);
    uint32_t hud = 0, bb = 0, quad = 0;
    CHECK(s.Register(SCO_SURFACE_HUD_2D, sco::sdk::UiSurfaces::Hud(0.5, 0.1, 0.3, 0.1, "alpha.hud"), hud) == SCO_OK);
    const double pos[3] = { 1, 2, 3 }, up[3] = { 0, 0, 1 }, normal[3] = { 0, -1, 0 };
    CHECK(s.Register(SCO_SURFACE_BILLBOARD_3D,
                     sco::sdk::UiSurfaces::Billboard(pos, up, 2, 1, "alpha.tag", 100, SCO_SURFACE_OCCLUDE_RAYCAST), bb) == SCO_OK);
    CHECK(s.Register(SCO_SURFACE_QUAD_3D, sco::sdk::UiSurfaces::Quad(pos, normal, up, 4, 3, "alpha.screen"), quad) == SCO_OK);
    CHECK(s.UpdatePose(quad, sco::sdk::UiSurfaces::Quad(pos, normal, up, 5, 3, "alpha.screen")) == SCO_OK);
    const auto list = sco::surfaces::Surfaces();
    CHECK(list.size() == 3 && list[2].pose.widthM == 5.0 && list[1].pose.flags == SCO_SURFACE_OCCLUDE_RAYCAST);
    CHECK(s.UpdatePose(quad, sco::sdk::UiSurfaces::Quad(pos, up, up, 5, 3, "alpha.screen")) == SCO_BAD_ARG);
    CHECK(s.LastError() == "surface " + std::to_string(quad) + ": quad_3d: up is not perpendicular to normal");
    CHECK(s.Unregister(hud) == SCO_OK && s.Unregister(bb) == SCO_OK && s.Unregister(quad) == SCO_OK);
    CHECK(s.Unregister(quad) == SCO_NOT_FOUND);

    // A table shorter than a member answers SCO_UNAVAILABLE without calling it.
    static sco_ui_surfaces_v1 shortTable;
    shortTable = *g_s;
    shortTable.size = static_cast<uint32_t>(offsetof(sco_ui_surfaces_v1, unregister_surface));
    sco_api shortApi = *g_api;
    shortApi.query_service = [](const char*, uint32_t, const void** out) -> sco_result {
        *out = &shortTable;
        return SCO_OK;
    };
    sco::sdk::UiSurfaces partial;
    CHECK(partial.Unregister(1) == SCO_UNAVAILABLE);   // not opened
    CHECK(partial.Open(&shortApi, g_alpha) == SCO_OK && partial.Table() == &shortTable);
    CHECK(partial.Register(SCO_SURFACE_HUD_2D, sco::sdk::UiSurfaces::Hud(0.5, 0.5, 0.1, 0.1, "alpha.hud"), hud) == SCO_OK);
    CHECK(partial.Unregister(hud) == SCO_UNAVAILABLE && partial.LastError().empty());
    CHECK(s.Unregister(hud) == SCO_OK);
}

// ---- stop ----------------------------------------------------------------------------------------

static void TestStop() {
    uint32_t id = 0;
    CHECK(Register(g_alpha, SCO_SURFACE_HUD_2D, Hud(), &id) == SCO_OK);
    const uint64_t rev = sco::surfaces::Revision();
    sco::surfaces::Stop();
    CHECK(!sco::surfaces::Started());
    CHECK(sco::surfaces::Surfaces().empty() && sco::surfaces::Revision() > rev);
    const void* t = nullptr;
    CHECK(g_api->query_service(SCO_UI_SURFACES_NAME, SCO_UI_SURFACES_VERSION_1_0, &t) == SCO_NOT_FOUND);
    CHECK(Register(g_alpha, SCO_SURFACE_HUD_2D, Hud(), &id) == SCO_UNAVAILABLE);
    CHECK(g_s->unregister_surface(g_alpha, id) == SCO_UNAVAILABLE);
    char buf[8];
    uint32_t n = sizeof(buf);
    CHECK(g_s->last_error(g_alpha, buf, &n) == SCO_UNAVAILABLE);
    sco::surfaces::Stop();   // no-op

    // Releasing an owner while surfaces are stopped is harmless.
    sco_plugin* gamma = sco::host::NewPlugin("gamma");
    CHECK(sco::Release(gamma) == Result::Ok);

    CHECK(sco::surfaces::Start() == Result::Ok && sco::surfaces::Surfaces().empty());
    CHECK(Register(g_alpha, SCO_SURFACE_HUD_2D, Hud(), &id) == SCO_OK);
    sco::surfaces::Stop();
    sco::ui::Stop();
}

int main() {
    sco::SetLogSink(Sink);
    sco::SetGameThread();
    g_api = sco::host::BuildApi({ "test-surfaces 1.0" });
    g_alpha = sco::host::NewPlugin("alpha");
    g_beta = sco::host::NewPlugin("beta");
    TestStart();
    if (g_s) {
        TestRegister();
        TestValidation();
        TestUpdate();
        TestLimits();
        TestLastError();
        TestThreads();
        TestWrapper();
        TestStop();
    }
    std::printf("test_surfaces: %d passed, %d failed\n", g_pass.load(), g_fail.load());
    return g_fail.load() == 0 ? 0 : 1;
}
