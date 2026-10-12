# Copied diagnostics and log privacy

This describes the **host-only step-1 facilities**, not a deployed runtime MCP
endpoint. The ordinary `Log`/`Status` sink and `mod.log` behavior are unchanged.
There is no file tail, socket, IPC diagnostics channel, credential, audit collector
or MCP-server change. Capture is inactive by default and is not installed into any
product by this step. The existing catalogue-only server stays catalogue-only.

## Minimize before handling text

Snapshots allowlist metadata. They omit author/display names, directories/folders,
reload paths, OS exception text, owner/module/function/game pointers, addresses,
patterns and game bytes. Invalid manifests get deterministic session-list aliases,
not raw folder names. Reasons are fixed host codes, not plugin/OS prose. Version
and build labels use a deliberately narrow bounded ASCII grammar; uncertain values
are unknown, not an empty success or a claim of compatibility. Unsupported optional
publisher facts remain absent; #141 publishers require their own reviewed contracts.

`Capture::AssignKey` binds an opaque host key to a safe host-assigned ID and never
relabels an existing key. Plugin strings cannot spoof another identity by prefixing
text. Capture records are session-global sequenced and all returned entries have
`untrustedContent=true`, including host-rendered lifecycle/fault events. These are
observations, never instructions. Structured events are a closed host enum.

Free-form consent is a **separate default-off bit**, not consent inferred from
plugin enablement. The current conservative implementation only releases the benign
exact tokens `ready`, `loaded`, and `stopped` after that consent; other free-form
text is withheld and counted. This intentionally includes paths, known/unknown roots,
emails, credentials, pointer-like text, controls, malformed UTF-8, multibyte text,
long input and instruction prose. No regex is represented as perfect redaction.
No raw uncertain text is retained, so controls or incomplete UTF-8 cannot reach a
returned message, and released messages stay below 512 UTF-8 bytes. A later richer
sanitizer needs separately reviewed behavior and codepoint-safe truncation tests.

## Retention, gaps and invalidation

Capture reserves budget for at most 1024 identity keys and accounts records plus
strings and conservative overhead within a 1 MiB budget; oldest records are evicted.
Allocator/Windows process-memory and tick measurements still belong to rollout
acceptance. Tail limits are 1..100 entries and a conservative 32 KiB serialized-data
budget. `firstAvailableSequence` and `droppedCount` expose retention loss;
`redactedCount` counts considered withheld records. `nextSequence` advances across
withheld and nonmatching considered records so a resume does not repeat a redaction
gap. Host-created sequence numbers never wrap or reset into old valid tokens.
`Stop` drops records and identities. Resuming a previous capture session is refused;
a fresh tail starts with sequence zero. No captures predate an explicit `Start`.

Immutable snapshot pages retain at most two completed generations. The internal
host token table (64 tokens, 60-second expiry) binds tool, exact filter, generation
and continuation index. Tokens never decode peer-provided indices. Session reset
invalidates tokens before teardown; stale copies retain their fault/restart flags.
Native host code may hold an immutable copy longer, but a future external channel
must independently reject that payload after admission/session loss.

## Not protected audit or authorization

An in-memory capture is **not** the protected audit sink. No protected OS collector,
ACL proof, acknowledgement-before-release, human opt-in setting, offline admission,
remote-model disclosure consent or authentication is implemented here. Optional
product/game-pack metadata never establishes those gates. A future endpoint must
fail closed until the [approved design](design/runtime-mcp.md#redaction-and-protected-audit)
is satisfied. Native plugins remain trusted full-privilege in-process code;
immutable copies are not a native sandbox or recovery mechanism.

The synthetic `test_runtime_diagnostics_snapshot` fixture checks minimization,
withholding, identity separation, consent defaults, log gaps, generation pages,
physically one-byte tables and zero observation side effects. It does not prove
Windows security, steady-state game-thread latency or gameplay. **Not run in game.**
