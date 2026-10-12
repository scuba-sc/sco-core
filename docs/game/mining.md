# Mining row: the biome builder's cell function

One row behind sc-offline's natural-mining built-in, in [`src/game/mining_sigs.cpp`](../../src/game/mining_sigs.cpp). Accessor: [`sco/game/mining.h`](../../include/sco/game/mining.h). The row is not a move of an old scan: the first mining module hooked a fixed RVA of an earlier build, so the function was found again in 4.10.196.36804 (`StarCitizen.exe`, PE timestamp 0x6AC64E6F) and pinned by what the hook relies on.

| Capability | Rows |
|---|---|
| `game.mining` | `mining.cell` |

## Row and checks

| Row | Found from | Checks |
|---|---|---|
| `mining.cell` | the one `lea r8, [label]` (`4C 8D 05`) of the profiler label `CBiomeBuilder::BuildLargeScaleEcoSystem` in .rdata, at `+0x9B` of the function whose start comes from .pdata | see below |

`mining.cell` is `void CBiomeBuilder::BuildLargeScaleEcoSystem(builder rcx, cell rdx, component r8, uint32 lod r9d, uint32 flags, uint64 page, uint8 option)`: seven integer or pointer arguments, the last three on the stack. Each check fails with its own reason, and all are relative to the function start:

| Offset | Bytes prove |
|---|---|
| `+0x000`, `+0x014` | the four register arguments are saved to their home slots, then 8 pushes and `rbp = rsp - 0x1d8`, so `[rbp+0x240]` is the fifth argument |
| `+0x03E`, `+0x052`, `+0x05C` | `lod` (r9d), `cell` (rdx) and `builder` (rcx) are the arguments the hook reads |
| `+0x0FD` | the flags are read once, from the fifth argument |
| `+0x127` | the flags are split into `& 4`, `& 8` and `& 2` |
| `+0x147` | the builder type is read at `builder+8` |
| `+0x441` | the spawn branch is skipped unless the builder byte at `+0x29` is 0 |
| `+0x4BF` | with `flags & 8`, cell `+0x16A8` > 1 and `lod <= 1` the function enters spawn mode 1 |
| `+0x929` | the flags are passed on by address, so a changed argument is what the rest of the function sees |

The constants the hook uses are in `mining.h` (`kCellBuilderType`, `kCellSpawnGate`, `kCellHarvestLod`, `kRuleFlags` = 0x6, `kRuleSpawnBit` = 0x8). They are pinned by the checks above and are not rows: they are offsets inside the function or structure, not addresses.

## What is not a row

The first module also read three context bytes (server, editor, online) and required the offline server context. Rows for them are not added: the product already knows it is offline, and the row set must not make a way to force that context. sc-offline's built-in checks its own offline state.

Hooking, and applying `flags |= 0x8`, stay in the product.

## Uniqueness in 4.10.196.36804

The label string occurs once in the image and is loaded by one `lea` in .text; the function is `mining.cell` = 0x265FB10. The row reports `AMBIG` if a patch adds a second load. Checked with `sco-sigcheck -v` on the mining branch (all 311 rows OK in `sigcheck-mining.txt`; the merged tree has 316 rows, see [status](status.md)) and with one checked byte changed in a copy of the exe (`mining.cell` fails with the reason of the `+0x441` check).
