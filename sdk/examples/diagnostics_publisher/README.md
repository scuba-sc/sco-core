# Optional diagnostic publisher

Build from the core SDK with `cmake -S . -B build` and `cmake --build build`.
The native module includes only public `sco_api.h` and
`sco_diagnostics_publisher.h`; it never opens IPC, observes a game entity or reads
another plugin. Install its folder with the normal native plugin process.

The folder ships a `disabled` marker. Do not remove it until the trusted host has
explicitly enabled `Platform::diagnosticSources.diagnosticsPlugin`, selected ID
`diagnostics_publisher`, and supplied positive offline admission. This source
configuration does not authorize runtime clients or enable the diagnostics
endpoint. The host may still refuse disclosure/audit admission. Unknown/offline
false or source off leaves the service absent; the plugin is unavailable, not a
hidden fallback. Do not enable this folder automatically in product packaging.

The example submits fixed advisory `READY` once during load: only its own
initialization succeeded. It is not host or game readiness and never certifies
health/compatibility/gameplay. An observation retains the host's capture time;
subsequent host snapshots do not make it fresh. Production publishers can submit
from normal game-thread callbacks at most once/second; capture never calls them.
Unload has no pending callback or resource to release; the host stops retaining
the source on owner release/quarantine and clears it before shutdown callbacks.
The host-owned table is process-lifetime; plugin modules providing other services
may separately require restart. No generic hot-reload guarantee is implied.

The closed runtime v1 output schemas do not include advisory extra fields. They
remain internal host source data pending a separately reviewed output version.
There is no direct peer channel, read method, arbitrary text/JSON/name/value or
engine pointer. Trusted native code still has full process rights: this is not a
sandbox or a guarantee against malicious encoding.

Evidence: synthetic native module load and layout checks only; **not run in game**.
Windows/MSVC and product packaging acceptance remain owner-run work.
