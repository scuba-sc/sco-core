#pragma once
// The address behind natural mining: CBiomeBuilder::BuildLargeScaleEcoSystem, the per-cell
// function the world builder calls for every large-scale ecosystem cell. Resolved by
// a host registry's Resolve(); the row is in src/game/mining_sigs.cpp, and docs/game/mining.md says what it
// checks.
//
// Rows are grouped into capabilities like sco/game/world.h: a feature asks for its group with
// sco::caps::SetFromSignatures(c.name, c.rows, c.count) and runs only when it's ready.
//
// Rows only find addresses. Hooking and writing stay in the product.
#include <cstddef>
#include <cstdint>

namespace sco::game::mining {

struct Capability {
    const char*        name;    // "game.mining"
    const char* const* rows;    // signature ids, all registered by RegisterGameSignatures(registry)
    size_t             count;
};

// Every capability of this feature, in a fixed order.
const Capability* Capabilities(size_t& count);

// mining.cell: CBiomeBuilder::BuildLargeScaleEcoSystem. The one lea r8 of its profiler label
//   (found at +0x9B of the function, whose start comes from .pdata). The row's checks pin the
//   signature and every field the hook reads (4.10.196.36804):
//     void cell(builder rcx, cell rdx, component r8, uint32 lod r9d, uint32 flags [stack 5th],
//               uint64 page [stack 6th], uint8 option [stack 7th])
//   Seven integer/pointer arguments; the hook sees the flags as a plain argument and the native
//   function reads them once, at +0xFD, into locals (and passes their address on at +0x929).
constexpr size_t kCellBuilderType  = 0x08;     // int32 on the builder: 0 here, 1/2/3 are planet-side entity spawning
constexpr size_t kCellSpawnGate    = 0x29;     // byte on the builder: the native spawn branch runs only when it is 0
constexpr size_t kCellHarvestLod   = 0x16A8;   // int32 on the cell: the harvestable LOD already built
constexpr uint32_t kRuleFlags      = 0x6;      // the flags natural cells arrive with (physics bits)
constexpr uint32_t kRuleSpawnBit   = 0x8;      // ORed in, the native code then builds the harvestables

}  // namespace sco::game::mining
