# Storage metadata

`sco.storage.metadata` 2.0 publishes `sco_storage_v2` from `sco_storage_v2.h`.
It is separate from `sco.storage` 1.1, whose name, version, layout and behavior
remain unchanged. Query `SCO_STORAGE_V2_NAME` with `SCO_STORAGE_V2_VERSION_2_0`,
then check the complete extent and non-null pointer of each requested member.

Only the caller's existing database is inspected: no paths, stored row values,
SQL text, other-owner selector or SQLite pointer crosses this interface. Metadata
reads never open a database or create files. A valid unopened owner or stopped
storage returns Unavailable; invalid/released handles return BadArg. Missing
services return NotFound at discovery; incompatible versions return Unavailable.

- `owner_info` copies the nonzero storage-start generation, ordinary own table
  count and open cursor count. Session generations are not reused across restart.
- `next_table` returns the next ordinary main-schema table name in binary order
  and its declared column count, including generated columns. Null starts the
  iteration; empty names are valid. The name handshake includes its NUL.
- `row_count` performs budget-limited `COUNT(*)` only after schema validation and
  safe identifier quoting. Views, virtual/shadow tables and reserved `sco_*` or
  `sqlite_*` objects are excluded; missing/excluded objects return NotFound.
- `quota_info` distinguishes configured quota, effective SQLite page quota,
  allocated database pages and remaining quota capacity. Remaining capacity is
  effective minus allocated bytes, not filesystem free space or freelist reuse.
  WAL and filesystem file sizes are excluded.

To avoid asking a virtual-table module to classify its objects, inspection also
conservatively excludes any ordinary name beginning with a declared virtual
table's name plus a literal underscore, case-insensitively. An unrelated table
using that prefix is excluded too. This schema-only rule does not execute the
virtual table or prepare a view.

Set output-record size to its capacity, at least the declared struct size.
Success writes that struct only and preserves trailing caller bytes. Failure
leaves outputs untouched, except TooMany for a short next-table name buffer writes
its required size only. Names are not silently truncated. Inputs use storage
1.1's 16,777,216-byte name limit excluding NUL; SQLite statement limits also apply.
Calls serialize per owner and obey the SQL progress budget; counts may fail or
be interrupted and mutex scheduling has no hard deadline. Separate reads and
pagination are interval observations, not a transaction across concurrent writes.

C calls the table directly. C++ uses `scosdk/storage_metadata.hpp` and
`sco::sdk::StorageMetadata`; C# uses `Sco.Sdk.StorageMetadata`. Lua adds
`sco.store.metadata.owner_info()`, `next_table(after)`, `row_count(table)` and
`quota_info()` with copied results; failures return nil, result name and message.
`next_table` returns a copied `{name, flags, column_count}` record; the end of an
iteration is `nil, "not_found", message`. A Lua result exceeding `LUA_MAXINTEGER`
is refused with `too_many`, never rounded or wrapped into a negative integer.
Existing storage routes are unchanged. An old, short or null provider is
unavailable, never a successful empty result. Experimental Rust has no typed
metadata wrapper.

The cheaper host-only `sco::storage::CopyMetadata` copies existing owner summaries
and configured quotas without SQL, file creation or callbacks. Unobserved table,
row and used-byte counts remain unknown. See [diagnostic sources](diagnostics-sources.md)
for opt-in snapshot use. This service is game-neutral. Not run in game.
