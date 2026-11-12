# Optional copied Profile and Storage observations

Host-source `diagnostics::OptionalSources` may contain `profile` and `storage`.
Both are absent by default. These copies grant no channel, IPC/MCP operation or
native activation. Runtime JSON v1 serializers and opcodes remain unchanged and
do not include these fields.

Through `app::Platform::diagnosticSources`, supply a Profile observation with an
actual safe host-assigned identity and nonzero profile generation, and/or enable
`storage`. The app copies that explicit identity, observes actual roster counts,
timestamps the observation and copies the current storage session's existing
owner summaries. Identity is never inferred from paths, load order or snapshot
generation. Strict owned startup refuses these new source options; native refusal
and teardown ordering are preserved.

`SnapshotStore::Begin` validates/copies optional sections and `Advance` advances
host-owned observations. Neither invokes plugins, reads provider tables, queries
services or runs SQL for these fields. IDs use the existing safe grammar and a
31-byte limit. Zero source generations, future times, invalid/duplicate IDs,
inconsistent known roster counts, excessive inventories and invalid numeric
facts drop the affected section. Invalid input never becomes successful zero.

A missing section is unavailable/not enabled. A present empty Storage inventory
is known zero observed owners; optional count/size fields distinguish unknown
from zero. Storage inventories cap at 1,024 owners with explicit truncation.
Source generations identify their source sessions, not diagnostic copies or
authorization tokens. Only safe identities/counts/sizes are copied, never paths,
SQL, raw handles, database values or free-form errors. The cheap storage copy
does not run SQL; table/row/allocated-byte facts can legitimately remain unknown.

Reset/Stop revokes discovery/pagination before plugin exit/unload callbacks. Held
immutable snapshots remain historical and caller mutation/restart cannot rewrite
them. Use source time and generation rather than treating retained copies as live.
This is interval metadata, not an atomic database transaction. Not run in game.
