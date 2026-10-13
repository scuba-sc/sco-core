# Runtime diagnostics sources (#141)

Implemented source publishers, not a transport or authorization layer. Builds and
fixtures are synthetic; **not run in game**. The frozen `sco_api` 1.1 is unchanged.

## Boundaries

- Star Citizen owns `sco::game::diagnostics::CopyMinimal` in `sco_game_diagnostics`.
  It publishes the compiled game-pack version, optional actual host-observed
  game-build label, and host monotonic observation time. The historical verified
  build constant is deliberately not a claim about the running game. No entity,
  player, account, address, peer, game memory, scanner or SDK call is read. The
  host already supplies copied lifecycle/capability/service/signature metadata.
- The neutral host owns `sco.diagnostics.publisher` 1.0. It is a push-only **source
  ingress**, not a client observation endpoint. Exactly one live plugin selected
  by trusted host ID may submit `UNKNOWN`, `READY` or `DEGRADED` advisory health.
  No arbitrary JSON, text, names, values, IDs, time, paths or callback is accepted.
  The host records its own timestamp and only one observation/second. Advisory
  health never changes authoritative host/game readiness or fault flags.
- Sources enter only `Snapshot::sources` through the existing game-thread
  construction path. Workers consume immutable completed host copies. Capture
  never queries a service, invokes a plugin callback or requests a refresh.
  There is no new file/network/IPC/transport sink, read method or side channel.
- Extra plugin-authored fields remain **internal host source data**, NOT new
  fields in the six closed runtime JSON v1 schemas. Serialization of advisory
  health needs a separately reviewed output/schema version; this change does not
  enlarge runtime observation scope. Existing game-pack version/build feed the
  already approved host/resolution metadata fields.

## Configuration and lifecycle

`Platform::diagnosticSources` defaults to empty/off. Host source options are
`gamePack=false`, `diagnosticsPlugin=false`, `offlineAuthorized=false`, empty
selected `diagnosticsPluginId`, unknown `gameBuild`. Plugin/game-service enablement
alone enables neither source. `offlineAuthorized` is a positive **trusted product
fact**, not a request to override offline/EAC policy. The endpoint still requires
independent admission, disclosure consent, credentials and protected audit; these
source options grant none of those. Host must stop/close on admission revocation.

`StartPublisher` publishes nothing unless plugin source and positive offline
fact are set. Invalid/reserved ID refuses. Configure before plugins load; first
record is absent until selected live plugin submits. The game-pack function is
independently off unless game source plus offline fact are set; kernel-only builds
never include it, even if host config requests it. Sources/config are startup
composition, not MCP-enable tools or hot settings.

`submit` validates live handle/owner, complete record size and enum before copying.
It is synchronous game-thread-only, has no callback, allocation transfer or held
pointer, and stores at most one record. Wrong thread returns `WRONG_THREAD`;
invalid/released/unselected owner, short/null record or unknown enum `BAD_ARG`;
stopped/terminal admission `UNAVAILABLE`; excess rate `TOO_MANY` without overwrite.
The table is process-lifetime; service reopening starts empty. Calls do not imply
service availability, fresh observations or successful game behavior.

Owner release/quarantine suppresses the source. New load handles cannot inherit
old observations or rate budgets. Host Stop invalidates completed snapshots and
stops ingress **before** game.exit/unload callbacks. Existing shared snapshots
remain historical immutable objects, not a valid new-session observation. Endpoint
session invalidation must prevent handing them out after teardown/revocation.

Source `observedMs` is the host timestamp when submitted, not the timestamp of a
later snapshot. Future source timestamps are refused; a consumer of a future
version must retain source age/unknownness separately rather than making an old
advisory record fresh on every tick. No stable personal/session identifier is
accepted. Fixed enums minimize accidental disclosure, not malicious native
encoding: trusted native modules already have full process privileges.

## SDK/package and evidence

Plain C table/record in `sco_diagnostics_publisher.h`; guarded native C++ route in
`scosdk/diagnostics_publisher.hpp`; no C#/Lua binding. Complete member extent 16,
full-sized null callbacks, physically truncated root/service/record, failed reopen,
wrong owner/thread, one-record quota, immutable copies, unload and app startup/stop
are synthetic fixture cases in `tests/test_diagnostics_publishers.cpp`.
`tests/abi_diagnostics_publisher.c` pins C/C++/short-enum x64 layout.

The optional `sdk/examples/diagnostics_publisher` example includes only public SDK
headers and ships a `disabled` marker. The fixture dynamically loads the real
independently compiled module, exercises absent/refused and selected/enabled
paths, and checks capture made zero QueryService and plugin callback calls.
Core packaging includes public declarations, wrapper and disabled example;
core-source includes neutral implementation/fixtures. Game publisher implementation
stays out of the kernel source kit. Nothing is a fifth MIT wire header.

## Decision pending owner

The narrow safe extra-field v1 source contract is fixed advisory health, one
selected plugin and one observation/second. Wider fields/owners or output schemas
require separate review; no implicit permission to disclose gameplay/identities.
Product settings/install policy, actual offline verdict/revocation wiring, endpoint
security/consent/audit and Windows acceptance remain downstream. Existing transport
work and runtime JSON schemas are untouched by this source implementation.
