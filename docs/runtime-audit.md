<!-- SPDX-License-Identifier: GPL-3.0-only -->
# Runtime diagnostics protected audit (step 3)

**Windows OS security: NOT VERIFIED locally. Not run in game.** This adapter is
project-authored GPL-3.0-only host-private code, not an SDK service or a new MIT
header. It does not provision a collector or enable runtime inspection. Product
admission/integration, package membership and the administrator's acceptance
record remain separate prerequisites. Keep production diagnostics default off
until the Windows fixture below passes under the actual supported identities.
Fake collectors and portable receipt tests are **not** OS-security evidence.

## Collector and caller contract

`src/ipc/diagnostics_audit.h` defines:

```cpp
std::shared_ptr<audit::Collector> audit::OpenWindowsCollector(const audit::WindowsOptions&);
audit::Receipt Collector::Verify(uint64_t deadlineMs);
audit::Receipt Collector::Append(const ipc::AuditEvent&, const audit::Coordinates&, uint64_t deadlineMs);
bool audit::Protected(const audit::Receipt&, uint64_t deadlineMs, uint64_t observedNow);
bool audit::Acknowledged(const audit::Receipt&, uint64_t deadlineMs, uint64_t observedNow);
```

The methods are virtual for fixture injection. `Receipt` contains `status`,
`completedMs`, `protectedSink` and `effectiveIdentity`; statuses are `Verified`,
`Accepted`, `Unavailable`, `IdentityDenied`, `ProtectionDenied`, `Deadline`,
`Full`, `Invalid`, `Busy`. Only `Accepted` can acknowledge a read. Merely returning
`Verified` or reporting a successful queue operation cannot acknowledge it.

Deadlines and completion/observation use the existing Windows IPC clock,
`GetTickCount64()` milliseconds. Production methods accept a future deadline no
more than 2000 ms away. The caller passes the **original** request deadline,
never a freshly extended one, and samples the clock after the method returns:

```cpp
auto receipt = collector->Append(event, coordinates, deadline);
auto now = platform::NowMs();
if (!audit::Acknowledged(receipt, deadline, now)) {
    // refuse data; invalidate/revoke admission as host policy requires
}
// Recheck offline admission, session/epoch and deadline BEFORE releasing data.
```

Both proof flags must be true, completion must be nonzero and no later than
`observedNow`, and `observedNow` must be strictly before the deadline. A fake
receipt is a fixture capability, not a production ACL attestation. Production
must use the OS adapter, not the step-2 `FixtureAudit` boolean. Host ownership
should retain a `shared_ptr<Collector>` for the call; no raw lifetime promise.

`Coordinates` adds session ID, trusted safe build alias, schema (exactly 1),
explicit aggregate denial count (nonzero), and an optional sanitized filter ID
without changing the existing `ipc::AuditEvent`. Audit data is limited to those
coordinates plus request ID, opcode, fixed allowlisted outcome, monotonic time
and response bytes. No payload, log message, credential, MAC, PID or caller path
is accepted. Build/filter characters and bounds are checked; meaningful safe
aliases/PII minimization remain the trusted host's responsibility. Counts are
supplied by the host rate aggregator; this adapter does not silently aggregate
or drop events. Windows adds its normal local Event Log system metadata, such as
computer name and wall-clock time; these are not runtime response fields and
must not be forwarded to a model.

## Windows implementation

The only production sink is an **administrator-provisioned dedicated classic
Windows Event Log**, accessed locally (`nullptr` server/session handles), with
one administrator-created, globally unique source. Names must begin `SCO` and
contain only ASCII letters, digits or underscore, length 4..64. Application,
System and Security are never fallbacks. Options specify exact `log`, `source`
and `capacityBytes` (`MaxSize`, at least 64 KiB, multiple of 64 KiB); zero capacity
is invalid. The factory creates no channel, source, directory, file or registry
entry. There are no sockets, production child processes or ordinary-file writes.
Non-Windows production returns `Unavailable` for every operation.

For each Verify/Append the worker checks:

- Effective calling identity is the process token; thread impersonation is
  refused. Elevated tokens, UAC-linked/filtered administrator tokens (including
  deny-only Administrators groups), SYSTEM and available dangerous privileges
  such as backup/restore/take-ownership/debug/impersonation are refused.
- `AccessCheck(MAXIMUM_ALLOWED)` evaluates actual token group/deny semantics,
  not an ACL string comparison. Owners must be Administrators or SYSTEM; missing
  or NULL DACLs and unsupported callback/object ACE semantics fail closed.
- Channel access permits event publication (`0x2`) and optionally read (`0x1`),
  but not clear (`0x4`), delete, owner or DACL changes. Read permission is needed
  for exact protected-target readback, not for arbitrary log-tail exposure.
- Registry source/log/ancestor keys deny effective writes, child creation,
  deletion and ACL/owner changes. Source uniqueness is checked across at most
  256 registered logs. Ambiguity/inaccessible configuration is a refusal.
- `Retention` must be exactly 604800 seconds; `MaxSize` must match configured
  capacity. The administrator owns rotation and overflow policy. This finite
  capacity does not guarantee seven days of availability under arbitrary floods.
- The configured existing backing file is on a fixed ACL-capable local volume.
  Opened root/ancestor/file handles reject reparses and extra hard links, pin
  against deletion during append, and evaluate no effective file rewrite,
  append, deletion/replace or owner/DACL rights. Arbitrary environment variables,
  UNC/device/ADS paths and ambiguous components are refused. Only `%SystemRoot%`
  expansion uses the OS Windows directory, never a caller's environment value.
- The EventLog service security descriptor denies effective stop, reconfiguration,
  deletion and ACL/owner/control rights. No service-control attempt is made by
  production code.

Append uses `ReportEventW` and then reads back the exact source/insertion text in
the exact protected log, beyond its previous newest record. Readback is bounded
to one 64 KiB buffer / 64 records; overflow, routing mismatch, inaccessible log
or concurrent flooding refuses acknowledgement. Configuration/permissions are
rechecked after acknowledgement. A service return or process-memory queue alone
is insufficient. This is OS-service acceptance/readback, **not** a power-loss
fsync/durable-storage guarantee.

The synchronous caller waits only to its deadline on a condition variable; OS
RPC runs in an owned worker. One process-global in-flight cap prevents timed-out
calls from accumulating workers. A stuck RPC leaves subsequent attempts `Busy`;
no result is released and no automatic retry occurs. A late OS append may still
exist in the collector but its caller remains refused. Collector destruction
never joins a stuck RPC, and the module is pinned for process lifetime. This is
not a reloadable plugin. Windows scheduling/allocation is not a hard real-time
latency guarantee; measure actual timings in the fixture. Call only from the
diagnostics worker, never a game callback, game-thread tick or teardown wait.

**Identity limit:** Verify attests the process that calls it, not the identity of
an arbitrary other shared-memory peer. Supported native clients must execute the
same OS verification under their own effective identity before attachment.
Checking only the host token cannot prove a remote client is non-elevated;
secret possession/PIDs or client assertions are not token evidence. A hostile
administrator/raw peer, compromised host/plugin or agent able to elevate is
outside the no-rewrite guarantee and cannot be certified by this API. Preserve
that restriction in product/native-peer admission; do not advertise hostile
administrator rejection from host-side collector checks alone.

## Human provisioning and Windows fixture (not run locally)

A named administrator/provisioner must create a **synthetic dedicated** classic
log/source (`SCOFixtureAudit` / `SCOFixtureSource`), registered with the local Event
Log service. For this fixture set `MaxSize=8388608`, `Retention=604800`, channel
ACL owner Administrators/SYSTEM, host/agent identity read+publish but not clear,
and administrator-only config/source/rotation/disable rights. Supply an existing
absolute backing-file path with protected Administrators/SYSTEM-owned root and
ancestors; clients need security-descriptor reads but no raw-file writes. The
adapter intentionally refuses otherwise convenient defaults it cannot prove.
No installer or privileged provisioning command is run by the fixture.

Build the **one** test executable from a Windows developer shell, without game
sources, then run as the intended ordinary non-administrator agent account:

```bat
cl /std:c++20 /EHsc /W4 /WX /Isrc /Iinclude tests\test_runtime_diagnostics_audit_win.cpp src\ipc\diagnostics_audit.cpp /Fe:audit-fixture.exe advapi32.lib wevtapi.lib
audit-fixture.exe --windows-fixture-driver SCOFixtureAudit SCOFixtureSource 8388608
```

The driver starts only its own dedicated no-game child with a 15-second watchdog.
The child executes real token/config/ACL checks, appends synthetic build/session
metadata, verifies record count and collector readback, attempts clear and
retention/config changes, and probes raw file rewrite/delete/ACL rights and
service stop/config rights **individually**. Every destructive/control probe must
receive actual `ERROR_ACCESS_DENIED`; permission text is insufficient. Record
count must remain unchanged after denied operations. Probes never write arbitrary
paths or stop a service. The clear/config mutation probes run only after live
protection verification and only against dedicated `SCOFixture*` names. If ACLs
are wrong, abort before them. Never point this fixture at a real audit history.

Separately, run under an elevated administrator or UAC-filtered administrator
identity and require rejection **before append**:

```bat
audit-fixture.exe --windows-denied SCOFixtureAudit SCOFixtureSource 8388608
```

A fixture with a genuinely non-admin account must pass; a filtered administrator
account is intentionally not a substitute. Repeat under each supported host and
agent identity, and record source SHA/build alias, session, OS version, exact
log/source/capacity/retention and outcomes in an administrator-controlled ledger.
No real user names/SIDs/PIDs or raw local paths belong in the published ledger.
Keep provisioner identity details private. Re-run after identity, ACL, collector,
build or rotation changes; configuration alone is never acceptance evidence.
Full/offline collector behavior and measured timing still need Windows operation
under the provisioner's bounded synthetic fixture policy; they were not exercised
on macOS. No game, network, injection, process discovery or third-party assets.

## Local evidence ledger

- Portable narrow receipt/deadline test: implemented and executable on macOS.
- Native non-Windows collector: unavailable; no file fallback.
- Windows cross-build: syntax/link check only, not Windows execution or ACL proof.
- Windows no-game child, privilege denial, clear/retention/rewrite/service denial,
  capacity overflow/collector outage and timing: **NOT VERIFIED locally**.
- Product/default-off integration, real native-peer identity enforcement and
  manual authorized offline acceptance: owned by their separate integration
  steps; **not run in game**.
