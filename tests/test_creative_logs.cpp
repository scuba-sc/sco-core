// SPDX-License-Identifier: GPL-3.0-only
// game.creative's switch-off logging (src/game/services/creative.cpp): every toggle that goes off
// writes exactly one "[game] creative: <who>: <thing> off" line, whichever way it goes off (the
// plugin's own call, its unload, the service's Reset). The service is compiled into this test with
// SCO_CREATIVE_TESTING, which only makes the game-facing fly request and magazine hook succeed
// without game memory; the capabilities are set by hand and the real switch logic runs.
// Synthetic plugins only; nothing here is in-game evidence.
#ifndef SCO_CREATIVE_TESTING
#define SCO_CREATIVE_TESTING 1
#endif
#include "../src/game/services/creative.cpp"
#include "sco/host.h"
#include <string>
#include <vector>

namespace sco::game::services { bool SpawnReleaseHooked() { return true; } }
using namespace sco::game::services;

namespace {

int passed = 0, failed = 0;
#define CHECK(x) do { if (x) ++passed; else { ++failed; std::printf("FAIL line %d: %s\n", __LINE__, #x); } } while (0)

std::vector<std::string> g_lines;
void Capture(const char* line) { g_lines.emplace_back(line); }

struct Case {
    const char* thing;                         // as it appears in the log line
    sco_result (*set)(sco_plugin*, int);
    bool (*on)();
};
const Case kCases[] = {
    { "god mode",           SvcSetGodMode,         [] { return g_god.on; } },
    { "fly",                SvcSetFlyMode,         [] { return g_fly.on; } },
    { "infinite ammo",      SvcSetInfiniteAmmo,    [] { return g_ammo.on; } },
    { "infinite ship ammo", SvcSetInfiniteShipAmmo, [] { return g_ship.on; } },
};

void Arm() {
    Reset();
    g_c.god = g_c.fly = g_c.ammo = g_c.ship = true;
    g_started = true;
    g_lines.clear();
}

// The lines that report something going off.
std::vector<std::string> OffLines() {
    std::vector<std::string> out;
    for (const std::string& l : g_lines)
        if (l.size() > 4 && l.compare(l.size() - 4, 4, " off") == 0) out.push_back(l);
    return out;
}

std::string Expect(const std::string& who, const char* thing) { return "[game] creative: " + who + ": " + thing + " off"; }

}  // namespace

int main() {
    sco::SetGameThread();
    sco::SetLogSink(Capture);
    g_testNoGame = true;
    sco_plugin* a = sco::host::NewPlugin("creative_logs_a");
    sco_plugin* b = sco::host::NewPlugin("creative_logs_b");

    for (const Case& t : kCases) {
        // On then off through the service: one line, naming the plugin.
        Arm();
        CHECK(t.set(a, 1) == SCO_OK && t.on());
        CHECK(OffLines().empty());
        g_lines.clear();
        CHECK(t.set(a, 0) == SCO_OK && !t.on());
        CHECK(OffLines() == std::vector<std::string>{ Expect("creative_logs_a", t.thing) });
        // Off again with nothing on: no second line.
        g_lines.clear();
        CHECK(t.set(a, 0) == SCO_OK && OffLines().empty());

        // The owner unloads while it is on: one "owner unloaded" line; another plugin's unload logs nothing.
        Arm();
        CHECK(t.set(a, 1) == SCO_OK && t.on());
        g_lines.clear();
        ReleaseCreativeOwner(b);
        CHECK(t.on() && OffLines().empty());
        ReleaseCreativeOwner(a);
        CHECK(!t.on() && OffLines() == std::vector<std::string>{ Expect("owner unloaded", t.thing) });
        g_lines.clear();
        ReleaseCreativeOwner(a);
        CHECK(OffLines().empty());

        // Reset (Stop) with it on: one "stop" line.
        Arm();
        CHECK(t.set(a, 1) == SCO_OK && t.on());
        g_lines.clear();
        CHECK(Reset() && !t.on());
        CHECK(OffLines() == std::vector<std::string>{ Expect("stop", t.thing) });
    }

    // Reset with everything off: no off line at all (ammo and ship ammo are always cleared, silently).
    Arm();
    g_lines.clear();
    CHECK(Reset());
    CHECK(OffLines().empty());

    // Reset with every toggle on: one line each.
    Arm();
    for (const Case& t : kCases) CHECK(t.set(a, 1) == SCO_OK);
    g_lines.clear();
    CHECK(Reset());
    CHECK(OffLines().size() == std::size(kCases));
    for (const Case& t : kCases) {
        size_t n = 0;
        for (const std::string& l : OffLines()) n += l == Expect("stop", t.thing);
        CHECK(n == 1);
    }

    sco::SetLogSink(nullptr);
    std::printf("creative switch-off logs: %d passed, %d failed\n", passed, failed);
    return failed ? 1 : 0;
}
