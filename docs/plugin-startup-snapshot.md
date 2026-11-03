# Historical plugin startup snapshot

`sco::app::Platform::pluginStartupSnapshot` is an explicit, default-off host option. On the ordinary
qualified `StartDetailed` path it writes `<dataRoot>/plugin-startup.json`. The file is a bounded,
historical summary of one plugin load pass. It is not live status, proof that a game is running, an
attestation of native code, or authorization to activate a provider.

The strict `app::detail::StartOwned` path refuses this option. That path currently disallows plugin
loads and requires its own startup gate; this writer does not weaken or bypass it. A product that
does not reach a qualified plugin load pass will not produce a new record. Writer errors are
best-effort and never change startup results.

## Lifecycle and file ownership

After `StartDetailed` passes its runtime gate, the host has discovered plugins and prepared the
data-pack index. Immediately before invoking the plugin load pass, the opted-in writer tries to
take a nonblocking exclusive OS lock on `plugin-startup.lock` beside the output file and atomically
publishes a `starting` record. It holds that cross-process lock through the load pass. A second
cooperating process using the same data root skips publication while the lock is held. Once
`LoadPlugins` returns, the host publishes `complete` only from the final core plugin list. If the
host catches a C++ exception from the load pass it may publish `failed` with counts but no rows. If
the final list cannot be represented under this contract it publishes `incomplete` with counts but
no rows. A process crash may leave `starting`; it is never changed to a `running` state.

Each publication is written to a same-directory, session-specific temporary file, flushed, and
atomically renamed over the target while the lock is held. Readers should open only the exact
`plugin-startup.json` path, cap reads at 65,536 bytes, and close promptly. On Windows, readers that
keep a handle open across replacement must allow delete sharing; otherwise replacement can fail
before publication and the prior record remains. A POSIX directory-flush error can occur after the
rename already published a complete JSON record, so it reports a write failure with durability
uncertain; it still cannot fail startup. A final update failure can leave the earlier `starting`
record even after the load pass returns, so readers must not interpret `starting` as proof of a
crash. Crash-left temporary files are ignored by readers. The lock file is persistent: deleting it
after unlock could split the cross-process lock among two inodes. After acquiring the lock, the
next writer removes only regular files whose names exactly match this writer's session-temporary
format.

The lock and temporary-file leaf names reject symlinks/reparse points where the platform supports
that check, and cleanup skips non-regular entries. On Windows, an existing target reparse point or
directory is rejected before replacement; on POSIX, rename replaces the final symlink entry rather
than following it. The configured parent directory is resolved by the operating system normally;
point `dataRoot` at a host-controlled directory if untrusted users can create or replace path
components.

The lock coordinates cooperating writers that use the same data root and OS lock path. A process
with write access to the data directory can forge or replace this file. Do not treat it as
authenticated, tamper-resistant, or a live channel.

## Reader contract, schema version 1

The JSON object contains exactly these fields:

| Field | Value |
| --- | --- |
| `schema_version` | integer `1` |
| `record_state` | `complete`, `starting`, `failed`, or `incomplete` |
| `session_id` | exactly 32 lowercase hexadecimal characters |
| `started_at_utc` | `YYYY-MM-DDTHH:MM:SSZ` |
| `completed_at_utc` | same timestamp form; `null` only for `starting` |
| `plugins_enabled_at_start` | boolean copied from host configuration |
| `plugin_count` | count known to the host at this record state; `starting` publishes zero |
| `loaded_count` | known number of plugins whose host state is `loaded`; `starting` publishes zero |
| `plugins` | array only for `complete`; otherwise `null` |

`complete` has at most 256 rows; both counts match the rows and `loaded_count` matches their
`loaded` states. Each row contains exactly `id`, `manifest_valid`, `version`, `kind`, `state`,
`reason_code`, and `restart_required`.

- A valid manifest carries its core ID, a 1--31 character ASCII version from `[A-Za-z0-9._+-]`,
  and kind `native`, `lua`, `data`, or `builtin`.
- An invalid manifest carries a collision-free session-local `unknown_N` alias, `version: null`,
  and `kind: "unknown"`. The raw folder name and parse error are not published.
- States are `off`, `disabled`, `refused`, `ready`, `loaded`, `crashed`, `unloaded`, or `faulted`.
- Reasons are fixed codes only and agree with the state: `off` maps to `plugins_off`, `disabled` to
  `plugin_disabled`, `refused` to `invalid_manifest` for an invalid manifest or `plugin_refused`
  otherwise, `crashed` to `plugin_crashed`, and `faulted` to `provider_fault_restart_required`.
  The remaining states have `reason_code: null`. Invalid manifests in `off` or `disabled` retain
  those state-consistent reasons; `manifest_valid`, the alias, and the `unknown` kind carry the
  manifest status. The broader runtime-diagnostics projection currently gives invalid manifests
  reason-code precedence over state; this file intentionally follows the v1 launcher's
  state-consistency rule. `restart_required` is true exactly for `faulted`.
- IDs, versions and reason codes are ASCII-validated before serialization. No free-form reason,
  author, display name, filesystem path, or plugin log is included.

If a future core state or metadata cannot be projected without loss, use `incomplete` and `plugins:
null`; do not guess, truncate a row, or present a partial array as complete.

## Current product integration

This is an opt-in core capability, not yet wired by the checked-out sc-offline host or launcher.
The current sc-offline startup path routes through its owned host gate, which refuses plugin/native
activation until reader and worker lifetime barriers are proven. Its launcher therefore has no
current host-authored snapshot to read. A later integration must opt in only on a path that really
completes the load pass and keep the native activation refusal intact.
