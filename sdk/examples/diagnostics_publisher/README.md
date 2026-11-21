# Optional diagnostic publisher

A native C plugin that publishes one diagnostic status to the host. It includes only the public
`sco_api.h` and `sco_diagnostics_publisher.h`. It never opens IPC, observes a game entity or reads
another plugin.

## What it does

On load it submits one fixed `READY` status. `READY` means only that the plugin's own initialisation
succeeded. It says nothing about host or game readiness, health, compatibility or gameplay. The host
keeps the time it captured the status; later host snapshots do not make it fresh. Unload has nothing
to release: the host stops retaining the source when the owner is released or quarantined, and clears it
before shutdown callbacks. A publisher that submits later from a normal game-thread callback may do so at most once per second;
capture never calls it. There is no direct peer channel, read method, free text, name/value pair or engine
pointer.

## Build and check

From this folder, with CMake 3.20 or newer and a 64-bit C compiler (Windows: add `-A x64` to the
first command):

```sh
cmake -S . -B build
cmake --build build --config Release
cmake --install build --config Release --prefix out
sco-plugin-check out/data/plugins/diagnostics_publisher --no-load
```

Build `sco-plugin-check` as in the SDK README, [Build the examples](../../README.md#build-the-examples).
The checker's stand-in host has no diagnostics publisher service, so a full run would fail to load the
plugin. `--no-load` checks the folder only.

## Enable it

The folder ships with a `disabled` marker. Remove the marker only when all of these hold:

- The trusted host has enabled `Platform::diagnosticSources.diagnosticsPlugin` and selected the id
  `diagnostics_publisher`.
- The host has positive offline admission.

If the source is off or offline admission is unknown or false, the service is absent and the plugin is
unavailable. There is no fallback. The host may still refuse disclosure or audit admission. This
configuration does not authorise runtime clients or turn on the diagnostics endpoint. Do not remove
the marker automatically in product packaging.

## Limits

This is not a sandbox. Native plugin code has the full rights of the process, and the example is no
guarantee against a malicious encoding. The host's publisher table lives for the whole process, so
plugins that provide other services may still need a restart. No hot-reload guarantee is implied.

Not run in game. See [game capability status](../../../docs/game/status.md).
