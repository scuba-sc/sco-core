# Checking a game build with sco-sigcheck

`sco-sigcheck` runs every game signature table against a `StarCitizen.exe` file and prints the report the game writes to `mod.log` at startup. It tells you whether the mod still finds its addresses in a build without starting the game. It runs on Windows, macOS and Linux.

## Run it

```sh
tools/sigcheck.sh <StarCitizen.exe> [-v] [--catalog <runtime_catalog.inc>]
```

`tools/sigcheck.sh` builds `tests/out/sco-sigcheck` with clang or gcc and runs it. Set `CXX` to pick a compiler. In a CMake build the target is `sco-sigcheck`, and it takes the same arguments.

| Option | Does |
|---|---|
| `-v` | Also list the rows that are not OK in the table at the end (OK rows are always listed) |
| `--catalog <file>` | Label rows whose address matches an entry in an sdk_dumper `runtime_catalog.inc`. Labels only: nothing is resolved from the catalog |

The exe is usually at `StarCitizen\LIVE\Bin64\StarCitizen.exe`. It is large (about 175 MB for one older build) and the tool only reads it, so you can copy it to the machine you check on.

## Output

```
image: timestamp 0x<PE timestamp>, size 0x<image size>, preferred base 0x140000000
[core] signatures: <n>/<n> OK
OK       teleport.to_camera                   0x<RVA>
OK       system.quit                          0x<RVA>
...
```

- `image:` identifies the build. The PE timestamp and the image size change with every game build.
- The `[core]` lines are exactly what `mod.log` shows for the same build.
- The table has one line per row with its address as an RVA (offset from the image base). With `--catalog`, a matching row gets `= <name>` after it.
- The tool resolves every row of every game table: 347 rows in game pack 0.8.1. [`sdk/native_index.json`](../sdk/native_index.json) lists them. On the current verified build all of them resolve; see [the game pack page](game-pack.md#verified-game-builds).
- The `pak.*` rows need the exe's `.pdata` (the loader's start comes from its unwind data), which `sco-sigcheck` reads from the file.

A broken build looks like this:

```
[core] signatures: 4/8 OK
[core] FAILED   teleport.to_camera (layout changed at +0x2b0)
[core] BLOCKED  teleport.client_mgr (needs teleport.to_camera)
...
```

Fix the `FAILED`, `MISSING` and `AMBIG` rows. The `BLOCKED` ones follow. See [Adding a signature: when a game patch breaks a row](adding-signatures.md#when-a-game-patch-breaks-a-row).

## Exit codes

| Code | Means |
|---|---|
| 0 | Every row is OK |
| 1 | At least one row is not OK |
| 2 | Bad arguments, the file is not a 64-bit PE image, or a table failed to register |

Scripts can gate on the exit code. CI checks that the tool builds and exits 2 on a non-PE file. It cannot check a real build because the game exe is not public.

## Patch-day routine

1. Copy the new `StarCitizen.exe` from a PC with the game.
2. Run `tools/sigcheck.sh StarCitizen.exe`.
3. Exit 0: every row still resolves. Whether the features still work is a separate question (see below).
4. Exit 1: fix the rows it names, run it again, then test those features.
5. Extract `Game2.dcb` from the new `Data.p4k` and run `sco-dcb info` on it ([Checking a DataCore file](datacore.md#patch-day-routine)).

## What it cannot tell you

- Whether a feature works. A found row means the bytes match. Only a game session shows that the game still behaves the same. Not run in game. See [game capability status](game/status.md).
- Anything about addresses a product scans for itself outside the game tables.
- Whether the DataCore loader still reads its file only through open, read, seek and close. `pak.slots` proves those calls exist. The in-game load line (`[pak] ... served from its mount`) proves the file was served.
- Anything about patches applied by other means (the launcher, data files).
