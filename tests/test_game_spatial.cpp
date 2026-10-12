// Synthetic game-pack spatial fixtures; no game memory or engine execution.
#include "../src/game/services/spatial_model.h"
// Assertions perform fixture operations and must remain active in Release builds.
#ifdef NDEBUG
#undef NDEBUG
#endif
#include <cassert>
#include <cmath>
#include <cstdlib>
#include <limits>
#include <cstdio>
extern "C" int sco_game_spatial_route(const sc_game_spatial_v1*, unsigned);

static sco::engine::ZoneTree* routeTree;
static sco::game::services::spatial::Snapshot* routeSnapshot;
static int Pose(uint64_t a, uint64_t b, const double* p, const double* q, double* op, double* oq) {
    return sco::game::services::spatial::Convert(*routeTree, a, b, p, q, op, oq);
}
static int Record(uint64_t id, sc_game_zone* out) { return routeSnapshot->Get(id, out); }
static int Find(const char* name, sc_game_zone* out) { return routeSnapshot->Find(name, out); }
static int List(uint64_t e, uint32_t o, sc_game_zone* out, uint32_t s, uint32_t c,
                uint32_t* n, uint32_t* m, uint64_t* epoch) { return routeSnapshot->List(e, o, out, s, c, n, m, epoch); }

int main() {
    sco::engine::ZoneTree tree;
    routeTree = &tree;
    using namespace sco::engine;
    const Quatd turn = Quatd::FromAxisAngle({0, 0, 1}, std::acos(-1.0) / 2);
    tree.Set(10, 0, "synthetic-root", {{1e11, 0, 0}, {}, 1});
    tree.Set(20, 10, "synthetic-room", {{5, 0, 0}, turn, 1});
    tree.Set(30, 10, "synthetic-target", {{0, 3, 0}, {}, 1});
    const double p[3] = {1, 0, 0}, q[4] = {0, 0, 0, 1};
    double out[3] = {}, rot[4] = {};
    assert(sco::game::services::spatial::Convert(tree, 20, 30, p, q, out, rot));
    assert(std::abs(out[0] - 5) < 1e-12 && std::abs(out[1] + 2) < 1e-12);
    assert(std::abs(rot[2] - std::sqrt(0.5)) < 1e-12 && std::abs(rot[3] - std::sqrt(0.5)) < 1e-12);
    using sco::game::services::spatial::Snapshot;
    using sco::game::services::spatial::ParentMatches;
    assert(ParentMatches(0, false, 0, false));
    assert(ParentMatches(10, true, 10, true));
    assert(!ParentMatches(0, true, 0, false)); // unnameable parent is not world
    assert(!ParentMatches(10, false, 0, false));
    assert(!ParentMatches(10, true, 11, true));
    assert(!ParentMatches(10, true, 10, false));
    Snapshot snapshot;
    assert(snapshot.Observe(tree, 20));
    assert(snapshot.Observe(tree, 30));
    sc_game_zone zone = {}; zone.size = sizeof(zone);
    assert(snapshot.Get(20, &zone));
    assert(zone.parent_id == 10 && zone.root_id == 10 && zone.system_id == 0);
    assert(zone.flags & SC_GAME_ZONE_SYSTEM_UNKNOWN);
    assert(snapshot.Find("synthetic-room", &zone));
    const uint64_t epoch = zone.epoch;
    uint32_t count = 99, more = 99; uint64_t pageEpoch = 0;
    assert(snapshot.List(epoch, 0, &zone, sizeof(zone), 1, &count, &more, &pageEpoch));
    assert(count == 1 && more == 2 && zone.zone_id == 10 && pageEpoch == epoch);
    snapshot.Reset();
    assert(!snapshot.Get(20, &zone));
    assert(!snapshot.List(epoch, 0, &zone, sizeof(zone), 1, &count, &more, &pageEpoch));
    assert(snapshot.Observe(tree, 20));
    tree.Set(40, 0, "synthetic-other-root", {{}, {}, 1});
    tree.Set(10, 40, "synthetic-root", {{}, {}, 1});
    assert(snapshot.Observe(tree, 10));
    assert(!snapshot.Get(20, &zone));
    tree.Set(50, 0, std::string(95, 'x'), {{}, {}, 1});
    assert(snapshot.Observe(tree, 50));
    assert(snapshot.Get(50, &zone));
    // A fully occupied copied engine name could be truncated before observation.
    assert(zone.flags & SC_GAME_ZONE_NAME_TRUNCATED);
    assert(!snapshot.Find(std::string(95, 'x').c_str(), &zone));

    // Missing/cyclic ancestry must not publish partial records.
    tree.Set(60, 61, "synthetic-missing", {{}, {}, 1});
    tree.Set(70, 71, "synthetic-cycle-a", {{}, {}, 1});
    tree.Set(71, 70, "synthetic-cycle-b", {{}, {}, 1});
    assert(!snapshot.Observe(tree, 60) && !snapshot.Observe(tree, 70));
    tree.Set(80, 0, "duplicate", {{}, {}, 1});
    tree.Set(81, 0, "duplicate", {{}, {}, 1});
    assert(snapshot.Observe(tree, 80) && snapshot.Observe(tree, 81));
    const auto sentinel = zone;
    assert(!snapshot.Find("duplicate", &zone));
    assert(std::memcmp(&zone, &sentinel, sizeof(zone)) == 0);

    // Complete validation precedes writes; preserve caller size and extra tail.
    struct Extended { sc_game_zone record; uint64_t tail; } page[2] = {};
    page[0].record.size = sizeof(Extended); page[0].tail = 0x1122334455667788ULL;
    page[1].record.size = sizeof(sc_game_zone) - 1;
    count = more = 999; pageEpoch = 999;
    assert(!snapshot.List(0, 0, &page[0].record, sizeof(Extended), 2, &count, &more, &pageEpoch));
    assert(count == 999 && more == 999 && pageEpoch == 999 && page[0].record.zone_id == 0);
    page[1].record.size = sizeof(Extended); page[1].tail = page[0].tail;
    assert(snapshot.List(0, 0, &page[0].record, sizeof(Extended), 2, &count, &more, &pageEpoch));
    assert(page[0].record.size == sizeof(Extended) && page[0].tail == 0x1122334455667788ULL);
    assert(page[1].tail == page[0].tail && count == 2);
    assert(snapshot.List(pageEpoch, count, nullptr, sizeof(zone), 0, &count, &more, &pageEpoch));
    assert(count == 0 && more > 0);
    assert(!snapshot.List(0, 0, &zone, sizeof(zone), SC_GAME_SPATIAL_MAX_PAGE + 1, &count, &more, &pageEpoch));
    assert(!snapshot.List(0, 9999, &zone, sizeof(zone), 1, &count, &more, &pageEpoch));
    assert(!snapshot.List(0, 0, &zone, sizeof(zone) - 1, 1, &count, &more, &pageEpoch));
    assert(!snapshot.List(0, 0, nullptr, sizeof(zone), 1, &count, &more, &pageEpoch));
    assert(!snapshot.List(0, 0, nullptr, sizeof(zone), 0, nullptr, &more, &pageEpoch));
    assert(!snapshot.Get(0, &zone) && !snapshot.Get(50, nullptr));

    // Fixed admission quota, including ancestors; failure preserves epoch and data.
    snapshot.Reset(); tree.Clear();
    for (uint64_t id = 1; id <= SC_GAME_SPATIAL_MAX_ZONES; ++id) {
        tree.Set(id, 0, "synthetic", {{}, {}, 1});
        assert(snapshot.Observe(tree, id));
    }
    assert(snapshot.Get(1, &zone));
    const uint64_t fullEpoch = zone.epoch;
    tree.Set(999, 0, "overflow", {{}, {}, 1});
    assert(!snapshot.Observe(tree, 999));
    assert(snapshot.Get(1, &zone) && zone.epoch == fullEpoch && snapshot.Count() == 256);

    // Nested rotations and in-place round-trip, independent expected local axis.
    tree.Clear(); snapshot.Reset();
    tree.Set(10, 0, "synthetic-root", {{1e11, 0, 0}, turn, 1});
    tree.Set(20, 10, "synthetic-room", {{5, 0, 0}, turn, 1});
    tree.Set(30, 10, "synthetic-target", {{0, 3, 0}, {}, 1});
    tree.Set(40, 20, "synthetic-inner", {{0, 2, 0}, turn, 1});
    double inplaceP[3] = {1, 0, 0}, inplaceQ[4] = {0, 0, 0, 2};
    assert(Pose(40, 30, inplaceP, inplaceQ, inplaceP, inplaceQ));
    assert(std::abs(inplaceP[0] - 2) < 1e-12 && std::abs(inplaceP[1] + 3) < 1e-12);
    assert(std::abs(inplaceQ[2] - 1) < 1e-12 && std::abs(inplaceQ[3]) < 1e-12);
    assert(Pose(30, 40, inplaceP, inplaceQ, inplaceP, inplaceQ));
    assert(std::abs(inplaceP[0] - 1) < 1e-12 && std::abs(inplaceP[1]) < 1e-12);
    assert(std::abs(inplaceQ[3] - 1) < 1e-12);
    const double bad[3] = {std::numeric_limits<double>::infinity(), 0, 0};
    const double zero[4] = {};
    out[0] = 123; rot[0] = 456;
    assert(!Pose(20, 30, bad, q, out, rot));
    assert(!Pose(20, 30, p, zero, out, rot));
    assert(!Pose(20, 30, nullptr, q, out, rot));
    assert(!Pose(999, 30, p, q, out, rot));
    assert(out[0] == 123 && rot[0] == 456);

    // Real C route and raw C++ route share the portable provider logic.
    routeTree = &tree; routeSnapshot = &snapshot;
    assert(snapshot.Observe(tree, 20) && snapshot.Observe(tree, 30));
    const sc_game_spatial_v1 table = {sizeof(table), Pose, Record, Find, List};
    assert(table.pose_to_zone(20, 30, p, q, out, rot));
    assert(table.zone_record(20, &zone) && table.find_zone("synthetic-room", &zone));
    assert(table.list_zones(0, 0, nullptr, sizeof(zone), 0, &count, &more, &pageEpoch));
    const size_t offsets[] = {8, 16, 24, 32};
    for (unsigned method = 0; method < 4; ++method) {
        assert(sco_game_spatial_route(&table, method));
        assert(!sco_game_spatial_route(nullptr, method));
        const sc_game_spatial_v1 nulls = {sizeof(table), nullptr, nullptr, nullptr, nullptr};
        assert(!sco_game_spatial_route(&nulls, method));
        const size_t bytes = offsets[method] + sizeof(void*) - 1;
        auto* truncated = static_cast<sc_game_spatial_v1*>(std::malloc(bytes));
        assert(truncated);
        std::memcpy(truncated, &table, bytes);
        truncated->size = static_cast<uint32_t>(bytes);
        // Allocation physically ends INSIDE the pointer: ASan catches offset-only guards.
        assert(!sco_game_spatial_route(truncated, method));
        std::free(truncated);
    }
    std::puts("game_spatial: full poses, copied metadata, expiry, quota, C/C++ extents/nulls passed");
}
