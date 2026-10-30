// Synthetic test of the game-read binding lifetime. No game image is used.
// Non-MSVC builds compile reads.cpp with -D__fastcall=.
#include "sco/game/reads.h"
#include "sco/game/teleport.h"
#include <cstdio>
#include <cstdint>

static int passed = 0, failed = 0;
#define CHECK(c) do { if (c) ++passed; else { ++failed; std::printf("FAIL %d: %s\n", __LINE__, #c); } } while (0)

static bool available = false;
static sco::game::TeleportAddrs binding;
static int slotChecks = 0, callsA = 0, callsB = 0;
static bool slotsMatch = false;

namespace sco::game {
bool TeleportAddresses(TeleportAddrs& out) {
    if (!available) return false;
    out = binding;
    return true;
}
}
namespace sco {
bool BytesMatch(const uint8_t*, const char*) { ++slotChecks; return slotsMatch; }
}

static uintptr_t FindA(uintptr_t, uint64_t id) { ++callsA; return uintptr_t(id + 10); }
static uintptr_t FindB(uintptr_t, uint64_t id) { ++callsB; return uintptr_t(id + 20); }
static const double* Rotation(uintptr_t, double* out, uint8_t) { out[0] = 1; return out; }

int main() {
    using namespace sco::game::reads;
    uintptr_t tableA[0x2D0 / sizeof(uintptr_t)]{};
    uintptr_t tableB[0x2D0 / sizeof(uintptr_t)]{};
    uint8_t fakeCode = 0;
    tableA[0x120 / sizeof(uintptr_t)] = reinterpret_cast<uintptr_t>(&FindA);
    tableB[0x120 / sizeof(uintptr_t)] = reinterpret_cast<uintptr_t>(&FindB);
    tableA[0x2B0 / sizeof(uintptr_t)] = reinterpret_cast<uintptr_t>(&fakeCode);
    tableA[0x2C0 / sizeof(uintptr_t)] = reinterpret_cast<uintptr_t>(&fakeCode);
    tableA[0x2C8 / sizeof(uintptr_t)] = reinterpret_cast<uintptr_t>(&Rotation);
    uintptr_t objA = reinterpret_cast<uintptr_t>(tableA);
    uintptr_t objB = reinterpret_cast<uintptr_t>(tableB);
    uintptr_t systemA = reinterpret_cast<uintptr_t>(&objA);
    uintptr_t systemB = reinterpret_cast<uintptr_t>(&objB);
    uintptr_t manager = 0;
    binding.clientMgr = &manager;
    binding.entitySystem = &systemA;
    binding.handleFromId = &fakeCode;

    Reset();
    CHECK(!Ready());
    available = true;
    CHECK(Init() && Ready());
    CHECK(EntityFromId(7) == 17 && callsA == 1 && callsB == 0);

    // Failed slot validation is cached until rebinding or reset.
    double rotation[4]{};
    CHECK(!EntityRotation(reinterpret_cast<uintptr_t>(&objA), rotation) && slotChecks == 1);
    CHECK(!EntityRotation(reinterpret_cast<uintptr_t>(&objA), rotation) && slotChecks == 1);
    CHECK(Init() && Ready());
    CHECK(!EntityRotation(reinterpret_cast<uintptr_t>(&objA), rotation) && slotChecks == 2);

    // A failed rebind must not leave the prior binding ready or usable.
    available = false;
    CHECK(!Init() && !Ready());
    CHECK(EntityFromId(7) == 0 && callsA == 1 && callsB == 0);
    uintptr_t actor = 0, entity = 0;
    uint64_t actorId = 42;
    CHECK(!LocalPlayer(actor, entity, actorId) && actorId == 0);
    CHECK(!EntityRotation(reinterpret_cast<uintptr_t>(&objA), rotation) && slotChecks == 3);

    available = true;
    binding.entitySystem = &systemB;
    CHECK(Init() && Ready());
    CHECK(EntityFromId(7) == 27 && callsA == 1 && callsB == 1);
    Reset();
    CHECK(!Ready() && EntityFromId(7) == 0 && callsB == 1);
    CHECK(!EntityRotation(reinterpret_cast<uintptr_t>(&objA), rotation) && slotChecks == 4);
    Reset();
    CHECK(!Ready());

    // A previously successful slot check must also be discarded on reset.
    CHECK(Init() && Ready());
    slotsMatch = true;
    CHECK(EntityRotation(reinterpret_cast<uintptr_t>(&objA), rotation) && rotation[0] == 1 && slotChecks == 7);
    slotsMatch = false;
    CHECK(EntityRotation(reinterpret_cast<uintptr_t>(&objA), rotation) && slotChecks == 7);
    Reset();
    CHECK(!EntityRotation(reinterpret_cast<uintptr_t>(&objA), rotation) && slotChecks == 8);
    std::printf("game reads lifecycle: %d passed, %d failed\n", passed, failed);
    return failed ? 1 : 0;
}
