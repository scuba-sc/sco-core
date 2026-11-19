# Scoped game cvars

`game.cvars` reads four allowlisted boolean game cvars on the game thread. `game.cvars_write` sets them with leases and restores the prior values on release. Neither is `sco.settings`, and neither accepts arbitrary console text.

| Service | Header | Table | Capability | Default |
|---|---|---|---|---|
| `game.cvars` 1.0 | [`sc_cvars.h`](../../include/sc_cvars.h) | `sc_cvars_v1`, 16 bytes (`get_bool`) | `game.cvars.read` | OFF |
| `game.cvars_write` 1.0 | [`sc_cvars_write.h`](../../include/sc_cvars_write.h) | `sc_cvars_write_v1`, 24 bytes (`set_bool`, `restore_all`) | `game.cvars.write` | OFF |

The native read adapter (Windows, reference-integer) is opt-in with `SCO_CVARS_NATIVE=1`. Production native writes are refused, even when the environment variable asks for them. Status: [status](status.md). Not run in game.

## Reading: `game.cvars`

`get_bool` needs a live plugin handle. Only these exact, case-sensitive names are accepted:

- `i_target_selector.targeting2_enabled`
- `pl_lensdisplay.minimap_enabled`
- `pl_lensdisplay.greebles_enabled`
- `g_squadron_frontend`

The request type must be `SC_CVAR_BOOL`. The output is a copied `uint32_t`, 0 or 1, and is unchanged on failure.

| Result | Cause |
|---|---|
| `SCO_BAD_ARG` | Unknown name or type, or an invalid or released handle |
| `SCO_WRONG_THREAD` | Called off the game thread |
| `SCO_UNAVAILABLE` | Missing readiness, capability or admission, or a stopped runtime |
| `SCO_FAILED` | Reentrant call |
| `SCO_CRASHED` | Reported by the adapter. It latches restart-required: no further adapter calls, replacement or restart in that process. |

C and C++ are supported. C++ `scosdk::game::Cvars` checks the complete member extent and null callbacks, and clears stale state when a reopen fails. C# and Lua routes are explicitly unsupported.

## Writing: `game.cvars_write`

`scosdk::game::CvarsWrite` validates the complete table extent and both callbacks, and a failed reopen discards the old table. C# and Lua remain unsupported.

Writes need all of these: independently verified write callbacks, offline admission, the read and write capabilities, and the existing spawn release dispatcher. They are unavailable if that dispatcher failed to install.

Leases:

- There is one lease per exact name. The first canonical prior value is saved before the engine is mutated, and repeated same-owner updates keep it.
- Another owner receives `SCO_UNAVAILABLE` and the engine is not touched.
- Every write requires the readback to match.
- Explicit `restore_all` removes only the caller's leases and is idempotent when none exist.
- Restoration ignores loss of the feature capability so that cleanup can proceed. Public calls are refused once session or terminal admission closes.
- The provider does no load-time mutation.

Cleanup:

- Ordinary unload or crash cleanup is forwarded by `spawn.cpp` to `ReleaseCvarsOwner`.
- A `game.exit` subscription and `StopCvars` restore all leases before closing admission. The host must emit session exit or stop while the engine context is still valid, before replacing or releasing it. An ended session does not reopen on `game.ready`.
- A fresh stop, install and start is required to use a new port.
- The copied port and context must outlive every outstanding lease and any retained fault.

Faults:

- A failed engine write, restore or post-write verification keeps the unrestored leases, turns both capabilities off and latches restart-required. There is no retry and no port replacement.
- Host diagnostics can inspect `PendingCvarRestores`. `StopCvars` returns `Failed` while it withdraws the tables.
- Never free retained engine context merely because discovery was withdrawn.
- A loader-retained provider fault skips ordinary resource hooks. Those leases stay until the host-owned session or stop restoration runs, if the engine is still trusted.

For host teardown, ordinary fresh admission closes before unload. A separate game-thread-only cvar cleanup scope is established only after a fresh positive inspection. The optional native restore callback rechecks the full offline admission predicate before access, immediately before mutation, and after setter callbacks. Ordinary plugin reads and writes cannot use this scope. If admission is really lost, restoration is refused and the lease is retained with restart-required. The cleanup callback verifies the readback itself before the provider clears a lease. Legacy injected ports without it use the ordinary write and readback restore path.

## Engine boundary

The game-pack-only `src/game/services/cvars.h` defines a host-injected, reviewed port. Callbacks get a bounded index, never arbitrary command text or engine pointers. Adapters must guard native faults, return `SCO_CRASHED`, and validate the actual engine type before copying a bool.

`SCO_CVARS_NATIVE=1` requests the read-only adapter, which needs the console, lookup, reference-integer vtable and method identities to resolve. Every read re-queries the exact allowlisted name, validates its receiver, type and method identities, checks fresh host admission, and copies only a canonical 0 or 1. Receiver drift is refused. Native exceptions latch restart-required. Capabilities are published only after successful service registration.

The new route uses independently checked reference-integer methods and rejects arbitrary nonzero values instead of normalizing them. It is not the ABI of the legacy product's float `GetCVarNow` and `SetCVarNow` calls. Native setters preserve engine callbacks, and direct backing-storage writes are not a substitute.

Production native writes stay unavailable. Address, vtable and name equality do not rule out native object destruction and same-address reuse across setter callbacks or a retained lease. The adapter's setter and restore mechanics run only against synthetic objects, and no native lifetime pin or generation contract is claimed. Do not enable the production write port until object and backing-storage lifetime are grounded.

Not included: arbitrary console execution, networking or anti-cheat cvars, quantum keeper values, optional bridge values, game extracts and `USER.cfg` writes. `game.cvars` carries none of the quantum cvars.

The `game.quantum` backend, and only it, may write `v_qdrive2.quantumTravelAllowed`, `v_qdrive2.quantumBoostAllowed`, `v_qdrive2.setting_ignoreBlockedBoost`, `v_qdrive2.setting_ignoreBlockedTravel` and `p_enable_physical_quantum_travel`, and the shared drive parameter cell (the drive's parameter record at `+0xC0`, set to `1e9` as sc-offline v0.7.7 did). The conditions are in [the amended rule](../quantum.md#amended-rule-2026-10-15): fresh positive offline admission before every write, the first prior value saved first, every value restored on every release path, the engine type verified before any access, writes only while it holds control, and no console execution. The default adapter owns four of the five cvars (int storage rows). `p_enable_physical_quantum_travel` has no grounded route and is not written.

## Evidence

All evidence is synthetic: the SDK host and loader are real, and the engine callbacks are synthetic.

- Portable tests use the actual game-pack provider, neutral host registry and native loader. Extent tests physically truncate pointer tails under ASan/UBSan. macOS module loading is not Windows SEH or MSVC evidence.
- The write fixtures cover native load, write and unload twice, actual runtime owner release, the actual loader retained-provider verdict, first-prior preservation, contention, session exit and reset, stop, wrong thread and live handles, unavailable admission, failed restore, failed write and false-success readback. The portable release dispatcher is a labeled stand-in.
- The Windows native-boundary fixtures cover callback-time admission, console and method changes, rejected active or faulted reconfiguration, receiver drift and retained leases. The host-stop fixture runs actual host and app `Stop` with the production spawn cleanup dispatcher. It checks restoration while offline evidence stays valid, no write after real admission loss, and refusal of ordinary service calls and worker-thread cleanup during teardown.
- The production-route fixture (`tests/test_cvars_production.cpp`, CTest `cvars_production_refusal`) uses the real signature registry with synthetic resolver outputs and positive synthetic offline admission. It checks that production configuration succeeds, that a canonical read works through the configured provider, that a write answers `SCO_UNAVAILABLE` even with `SCO_CVARS_NATIVE=1`, and that no synthetic setter runs.

No Windows run and no real-value observation of the four cvars exists yet.
