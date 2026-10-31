# Storage: the `sco.storage` service

Per-plugin persistent storage: a key-value store and SQL, over SQLite, in place of the loose text files plugins and features keep under `data/` today. Each plugin gets its own database file, which no other plugin can open; a crash at any moment leaves the last committed state.

It is a **host-owned service** (the reserved id `sco`, [API v1 § Host-owned services](api-v1.md#host-owned-services)) named `sco.storage`, version 1.1, found with the ordinary `query_service` of sco_api 1.1. The 128-byte 1.0 prefix is unchanged; the 144-byte table adds schema introspection at its tail. `sco_api.h` is unchanged; the table is declared in [`include/sco_storage.h`](../include/sco_storage.h) (plain C) and pinned by [`tests/abi_storage.c`](../tests/abi_storage.c).

`next_table` enumerates this plugin's user tables and views in byte order, excluding
`sco_kv` and SQLite bookkeeping objects. `next_column` copies column names, declared
types and NOTNULL/primary-key flags in declaration order. Both use caller-owned
buffers and size handshakes; neither exposes database paths, row values or another
plugin's schema. Check the table extent and function pointer before calling either
tail method. C# exposes `Storage.NextTable`/`NextColumn`; Lua exposes
`sco.store.tables`/`columns`. C++ callers can use the public C table directly; typed
C++ schema wrappers are not supplied.

The `next_table` continuation (`after`, when non-null) and `next_column` table
name must be nonempty NUL-terminated strings of at most 16,777,216 bytes,
excluding the NUL. Longer inputs return `SCO_BAD_ARG`; this is separate from the
255-byte key-value key limit. The output handshake does not truncate or skip
names at that input limit. An existing identifier longer than the limit can be
returned, but cannot be supplied as a continuation or table name; such a schema
is not fully enumerable through these calls. If SQLite cannot bind a validated
name, the call reports that failure instead of continuing with an unbound value.

## Contents

- [Using it from C](#using-it-from-c)
- [Using it from C++](#using-it-from-c-1)
- [The table](#the-table)
- [Threads](#threads)
- [Transactions and crash safety](#transactions-and-crash-safety)
- [SQL: what a plugin may run](#sql-what-a-plugin-may-run)
- [Limits](#limits)
- [Unload, crash and shutdown](#unload-crash-and-shutdown)
- [Hosting it](#hosting-it)
- [Lua](#lua)

## Using it from C

```c
#include <stddef.h>
#include "sco_storage.h"

typedef struct my_spot { double x, y, z; } my_spot;
static const sco_storage_v1* st;

sco_result sco_plugin_load(const sco_api* api, sco_plugin* self) {
    st = NULL;
    if (!api || api->size < offsetof(sco_api, query_service) + sizeof(api->query_service) ||
        !api->query_service ||
        api->query_service(SCO_STORAGE_NAME, SCO_STORAGE_VERSION_1_0, (const void**)&st) != SCO_OK) {
        st = NULL;   /* a host without storage: keep working without it */
        return SCO_OK;
    }
    if (!st) return SCO_OK;

    if (st->size >= offsetof(sco_storage_v1, put) + sizeof(st->put) && st->put) {
        my_spot home = { 1.0, 2.0, 3.0 };
        st->put(self, "spots.home", &home, sizeof home);
    }
    if (st->size >= offsetof(sco_storage_v1, get) + sizeof(st->get) && st->get) {
        my_spot got;
        uint32_t size = sizeof got;
        if (st->get(self, "spots.home", &got, &size) == SCO_OK && size == sizeof got) {
            /* Use got here; a failed put above may leave an older saved value. */
        }
    }
    if (st->size >= offsetof(sco_storage_v1, exec) + sizeof(st->exec) && st->exec) {
        if (st->exec(self, "CREATE TABLE IF NOT EXISTS visits(place TEXT, at INTEGER)",
                     NULL, 0, NULL) == SCO_OK) {
            sco_sql_value row[2] = { { SCO_SQL_TEXT, 8 }, { SCO_SQL_INT } };
            row[0].v.p = "Waypoint";
            row[1].v.i = 1;
            st->exec(self, "INSERT INTO visits VALUES (?, ?)", row, 2, NULL);
        }
    }
    return SCO_OK;
}
```

A host service stays valid for as long as any plugin is loaded (host services are withdrawn after every plugin unloaded), so keeping `st` for the plugin's life is fine. A host that doesn't publish storage answers `SCO_NOT_FOUND`.

## Using it from C++

[`scosdk/storage.hpp`](../include/scosdk/storage.hpp) wraps the table ([C++ SDK](sdk-cpp.md#storage)):

```cpp
sco::sdk::Storage store;
if (store.Open(*this) == SCO_OK) {
    store.Put("spots.home", spot);                         // a trivially copyable struct
    std::string name;
    store.Get("player.name", name);                        // any size; the handshake is done for you
    std::vector<std::string> keys;
    store.Keys("spots.", keys);
    {
        sco::sdk::StorageTransaction tx(store);            // rolled back unless committed
        store.Exec("INSERT INTO visits VALUES (?, ?)", { sco::sdk::SqlText("Lorville"), sco::sdk::SqlInt(1) });
        tx.Commit();
    }
    sco::sdk::StorageCursor c = store.Query("SELECT place FROM visits WHERE at > ?", { sco::sdk::SqlInt(0) });
    std::string place;
    while (c.Next() == SCO_OK) c.Text(0, place);
}
```

Every call is `noexcept` and answers `sco_result`; `Storage::LastError()` gives the message of the last failure.

## The table

All functions take the plugin's `self`; the plugin can only reach its own database.

| Function | Does | Results besides `SCO_OK` |
|---|---|---|
| `put(self, key, value, size)` | Writes a value (bytes), replacing any old one | `BAD_ARG` bad key or size; `TOO_MANY` quota full |
| `get(self, key, out, &size)` | Reads a value with the size handshake | `NOT_FOUND`; `TOO_MANY` + needed size |
| `del(self, key)` | Removes a key | `NOT_FOUND` |
| `next_key(self, prefix, after, out, &size)` | The first key with `prefix` that sorts after `after` (NULL: from the start), byte order | `NOT_FOUND` when there are no more |
| `begin` / `commit` / `rollback` | One transaction per plugin, over key-value and SQL alike | `BAD_ARG` wrong state |
| `exec(self, sql, params, n, &changes)` | Runs one statement; rows are dropped | `BAD_ARG` refused or malformed; `FAILED` ran and failed; `TOO_MANY` quota |
| `query(self, sql, params, n, &cursor)` | Opens a cursor before the first row | as `exec`; `TOO_MANY` 64 cursors open |
| `step(self, cursor)` | Next row | `NOT_FOUND` no more rows (or not this plugin's cursor) |
| `column_count` / `column_name` | Cursor metadata; available before the first `step` and for an empty result set, without a current row. Column names use the size handshake | `NOT_FOUND` invalid cursor; `BAD_ARG` invalid arguments or name index out of range |
| `column` | Reads a value from the current row; TEXT and BLOB are copied with the size handshake | `NOT_FOUND` invalid cursor; `BAD_ARG` no current row, invalid arguments or index out of range |
| `close(self, cursor)` | Closes a cursor | `NOT_FOUND` |
| `last_error(self, out, &size)` | The message of the plugin's last failed call | |

The rules from `sco_api.h` hold, plus:

- **Ids, never pointers.** A query hands back a cursor id (an opaque `uint64_t`, unique in the process); column values are copied into the caller's buffer. Nothing points into SQLite or the host ([lesson 6](framework.md#6-services-hand-out-ids-never-pointers)). Another plugin's cursor id answers `SCO_NOT_FOUND`.
- **The size handshake** is the raw handlers' one: `*inout_size` is the buffer's capacity on the way in, the bytes written on the way out, or with `SCO_TOO_MANY` the bytes needed. Pass `out = NULL` with `*inout_size = 0` to ask the size. Keys, column names, error messages and TEXT columns come back NUL-terminated, and the NUL counts.
- **Keys** are 1-255 bytes and sort by their bytes; **values** are 0 bytes to 1 MiB. Parameters (`sco_sql_value`) are copied during the call.
- **The key-value store is a table** in the plugin's database, `sco_kv(key TEXT PRIMARY KEY, value BLOB)`. SQL may read and change its rows; its schema is the host's.

## Threads

Any thread. Calls are synchronous and serialised per plugin database: a call acquires an untimed mutex and may wait for another thread of the same plugin. The statement-execution budget (1 s by default, `Options::budgetMs`) starts only after that lock is acquired and the database is opened; SQLite's progress handler interrupts an over-budget statement with `SCO_FAILED`. It does not bound mutex wait, database opening or total call latency, so it is not a guarantee that the game thread returns within one second.

The transaction and the cursors belong to the plugin, not to the thread: a `put` from a second thread while the first has a transaction open joins that transaction. A plugin that writes from several threads and uses transactions coordinates its own threads.

## Transactions and crash safety

- Databases run in WAL mode with `synchronous = FULL`: a `put`, `del` or `exec` outside a transaction, and a `commit`, are durable when they return, across a crash of the game or of the machine. (The host can choose `NORMAL`, `Options::durable = false`: still consistent after any crash, but a power cut may drop the last commits.)
- Outside a transaction every call is its own transaction. Between `begin` and `commit` nothing is durable; process termination, ordinary unload cleanup or `rollback` undoes all of it. A restart-required retained-provider fault is not immediate rollback (see [lifetime](#unload-crash-and-shutdown)). `begin` takes the write lock at once (`BEGIN IMMEDIATE`).
- A batch of writes is much faster inside one transaction: each autocommit write waits for the disk.
- Transactions go through `begin`, `commit` and `rollback` only; `BEGIN`, `COMMIT`, `SAVEPOINT` and `RELEASE` in SQL are refused, so the host always knows the state.

`tests/test_storage.cpp` kills a child process (SIGKILL / `TerminateProcess`) in the middle of a 6 MiB transaction and in a loop of committing transactions, then checks that only committed state is there, never half a transaction, and that `PRAGMA integrity_check` says `ok`.

## SQL: what a plugin may run

Any SQLite statement on its own database, one per call (trailing whitespace and comments are fine), with bound parameters (`?`, `?NNN`, `:name`, in index order; their number must match). Refused with `SCO_BAD_ARG`:

| Refused | Why |
|---|---|
| `ATTACH`, `DETACH` (also with URI names or a bound path) | No way into another file. On top of the authorizer, the connection allows 0 attached databases |
| `VACUUM`, `VACUUM INTO` | Rewrites the file, or writes a copy anywhere |
| `BEGIN`, `COMMIT`, `END`, `ROLLBACK`, `SAVEPOINT`, `RELEASE` | Use `begin` / `commit` / `rollback` |
| `CREATE` / `DROP` / `ALTER` of tables, views, indexes or triggers named `sco_*`, or on a `sco_*` table | The host's (the key-value table) |
| `CREATE VIRTUAL TABLE`, `DROP` of one | Not needed; keeps the module surface small |
| Every `PRAGMA` except: `table_info`, `table_xinfo`, `table_list`, `index_list`, `index_info`, `index_xinfo`, `foreign_key_list`, `foreign_key_check`, `integrity_check`, `quick_check`, `user_version`, `application_id`, `foreign_keys`, `data_version`, `freelist_count`, `page_count`, and reading `page_size` | Journal, sync, page, cache, mmap and file settings are the host's |

The connection is also built defensively: `SQLITE_DBCONFIG_DEFENSIVE` on, untrusted schema, double-quoted strings off, no URI file names, no symlinked database file, no extension loading (not compiled in), temporary data in memory. The host-owned storage directory is created and canonicalized before opening a database, so directory aliases such as macOS `/var` work. The database filename itself is never canonicalized; `SQLITE_OPEN_NOFOLLOW` still rejects existing or dangling database-file symlinks. See [third_party/sqlite](../third_party/sqlite/README.md) for the compile options.

## Limits

| Limit | Value |
|---|---|
| Key | 1-255 bytes |
| Value | 1 MiB (`SCO_STORAGE_MAX_VALUE`) |
| Statement text | 1,000,000 bytes |
| One TEXT or BLOB in SQL | 16 MiB |
| Open cursors | 64 per plugin (`SCO_STORAGE_MAX_CURSORS`) |
| Database size | 64 MiB per plugin by default (`Options::quotaBytes`, as `max_page_count`); a write past it is `SCO_TOO_MANY`, deleting still works |
| Statement-execution budget | 1 s by default (`Options::budgetMs`), checked by SQLite's progress handler; over-budget execution returns `SCO_FAILED` "statement ran past its time budget". Not a mutex-wait or total-call deadline |
| Waiting for a lock another process holds | 1 s by default (`Options::busyTimeoutMs`), then `SCO_FAILED` |

## Unload, crash and shutdown

Ordinary unload, ordinary failed-load cleanup and non-retained `crashed` cleanup run the resource-release hook (through `sco::Release(self)` or the fault-resource path). It rolls back the plugin's open transaction and closes its cursors and database. Restart-required exposed-provider faults deliberately skip that hook: host scheduling/lookup is withdrawn, but storage resources and any open transaction remain retained until terminal storage shutdown. A failed load is a separate exception: if a nested consumer already obtained the provider's table, the loader records `refused` with `backing state retained` and returns without `Release(self)`, resource hooks, export clearing or module closure. Neither case promises immediate rollback or closure. After ordinary owner release, calls naming that `self` return `SCO_BAD_ARG` from any thread. A successful reload uses a new handle, opens the same file and sees committed data; old cursor ids are not valid for it.

The host reads and writes plugin memory (keys, values, SQL, buffers) only while it holds no lock, so a bad pointer from a plugin faults in plugin-facing code with nothing held, and the crash guard can quarantine the owner without those locks held; whether resource hooks run still depends on the retention rules above.

At terminal shutdown `sco::app::Stop` closes runtime admission and unloads still-loaded plugins (retained faulted instances are not entered), then stops storage: `sco.storage` is withdrawn, any transaction still open is rolled back and every database is closed, including databases retained by the fault/failed-load exceptions above. Calls through a held storage table then answer `SCO_UNAVAILABLE`; this does not authorise calls through a retained peer-provider table.

## Hosting it

The host kit starts storage when the product sets a data folder ([API: sco/app.h](api.md#scoapph-the-host-kit)):

```cpp
sco::app::Platform pf;
pf.dataRoot = "data";              // sc-offline: databases in data/storage/<plugin id>.db
sco::app::Start(pf);               // publishes sco.storage before any plugin loads
...
sco::app::Stop();                  // after UnloadAll: storage::Stop, host::WithdrawHostServices
```

Without `dataRoot` there is no storage, and plugins get `SCO_NOT_FOUND`. A product that doesn't use the host kit calls [`sco::storage::Start` / `Stop`](api.md#scostorageh-the-scostorage-service) itself (library `sco_storage`).

Data lives in `<dataRoot>/storage/<plugin id>.db` (plus SQLite's `-wal` and `-shm` files), outside the plugin's folder, so updating, reinstalling or disabling a plugin keeps its data. Plugin ids are `[a-z0-9_]`, so file names never clash or escape the folder. Removing a plugin's data is an explicit action (delete its three files while the game is closed).

## Lua

Lua plugins use storage through `sco.store` in sco-lua ([Lua reference](../sdk/docs/lua.md#scostore)): `get`, `put`, `delete`, `keys`, `begin` / `commit` / `rollback`, `exec`, and `sql`, which returns rows as tables (at most 1000) or passes each row to a function. Errors are `nil, err, message` with this service's `last_error`. Each call and each row read counts against the script's step budget; `sql` closes its cursor on every path, including a script error in the row function and the budget running out. The binding queries the service on each call, so a host without it answers `nil, "unavailable"`. Covered by `tests/test_lua.cpp` (round trip, two scripts isolated, SQL with parameters, cursors after errors and an overrun, an older host) and the SDK example `examples/notebook`.
