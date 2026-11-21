# Launcher contract

What a launcher and the sco-core host kit must agree on. This page is the written form of the constants in the reference launcher (`sdk/examples/launcher/crates/scl-contract`, `examples/launcher/` in the SDK kit) and of the plugin rules in `docs/plugins.md` in the repository. Where the host kit fixes a rule, the rule is marked **fixed by the host kit**. Where a product chooses, the page says **product choice** and gives what the reference launcher does.

Status: source level and synthetic tests. The reference launcher starts no game and has not been used with one. Not run in game. See [game capability status](game/status.md).

## Who owns what

| Piece | Owner |
|---|---|
| Capability switch names and the value `1` | Host kit (fixed) |
| Plugin folder layout, `plugin.ini`, `disabled` marker | Host kit (fixed) |
| `[settings]` storage in `data/storage/<id>.db` | Host kit (fixed) |
| Zip package rules and the plugin API version | Host kit and SDK verifier (fixed) |
| Name of the launcher's own variables, ini file, data folder, event names | Product choice |
| How the game process receives the launcher's variables | Product choice (the product's own `dinput8.dll` reads them) |
| Injection, offline patches, admission, menu renderer | Product (see [building a launcher](building-a-launcher.md#7-what-a-product-brings-itself)) |

## Product config

A product decides these once and passes them to every function that builds a name or a path. In the reference launcher they are the fields of `ProductConfig`.

| Field | Reference value | Used for |
|---|---|---|
| `name` | `SCO reference launcher` | Text shown to the player |
| `slug` | `sco-reference` | Prefix of the named events |
| `env_prefix` | `SCO_REF` | Prefix of the product's own variables |
| `ini_file` | `reference-launcher.ini` | Settings file in the launcher folder |
| `data_dir` | `data` | Data folder in the launcher folder |
| `default_packs` | none | Plugin folders a fresh install is expected to carry |
| `update_source` | empty | Where a product's own updater would look. The reference launcher never contacts it |
| `version` | crate version | Version text |

## Environment variables

### Capability switches (fixed by the host kit)

The host kit reads these from its own process environment. A switch is **on only when the value is exactly `1`**. Any other value, including `true`, `on` and an empty string, is off.

| Variable | Settings key | What it turns on | Evidence | Launcher default |
|---|---|---|---|---|
| `SCO_LOADOUT_ASYNC` | `exp_loadout_async` | `game.loadout.async` production adapter | synthetic only | on |
| `SCO_INVENTORY_READ` | `exp_inventory_read` | `game.inventory.read` | synthetic only | on |
| `SCO_ACTOR_STATS_WRITE` | `exp_actor_stats_write` | `game.actor_stats.write` (guarded writes) | off pending probe | on |
| `SCO_CVARS_NATIVE` | `exp_cvars_native` | `game.cvars.read` native adapter | synthetic only | on |
| `SCO_AUDIO_NATIVE` | `exp_audio_native` | native audio adapter attempt | synthetic only | on |
| `SCO_ENTITIES_DIAG` | `exp_entities_diag` | Diagnostic hooks that patch game functions to write log lines. Not a capability | not a capability | off |

These are the reference launcher's defaults. In the host, each of the five capabilities is OFF until its switch is set (`sdk/status_registry.json`). A product chooses its own defaults. `game.actor_stats.write` has evidence level `off_pending_probe`.

The host reads one more switch, `SCO_LOADOUT_OUTFIT=1`. It enables `game.loadout.outfit` (outfit slots 22-31 of `game.loadout.async`) and has an effect only together with `SCO_LOADOUT_ASYNC=1`. The reference launcher has no setting for it. A product that wants it sets it in the game's environment.

Rules:

- The settings key is `exp_` plus the variable name without `SCO_`, in lower case.
- The launcher builds the child environment from its settings. For a switch that is on it sets `NAME=1`. For a switch that is off it **removes** the variable from the child environment, so a value set in the player's own shell cannot switch it on.
- Launcher defaults: the five capability switches are on and the diagnostic switch is off. Each is a setting the player can turn off.
- A capability that is on can still be refused at run time by the host's own checks (admission, resolved rows, bounds). Turning a switch on does not prove the behaviour.

The reference launcher prints the table with `capabilities` and the resulting block with `capabilities env`.

### Product variables (product choice)

The reference launcher hands its own `dinput8.dll` three variables. Names are `<env_prefix>_<suffix>`:

| Suffix | Value |
|---|---|
| `PLUGINS` | `on` or `off`: load plugins from the plugin folder |
| `DATA` | Absolute path of the data folder |
| `MOD_LOG` | Absolute path of the host log file |

`<env_prefix>_ROOT` names the launcher folder for the launcher itself (`--root` wins, then this variable, then the folder of the executable).

The host kit does not read these. The product's `dinput8.dll` reads them and fills `sco::app::Platform` (`pluginsEnabled`, `dataRoot`, the log sink).

## Folder layout

All paths are under the launcher folder. Nothing in the host kit requires the launcher folder to be the game folder.

```text
<launcher folder>/
  <ini_file>                   settings file (product choice)
  data/                        data root (Platform::dataRoot)
    mod.log                    host log (product choice of name)
    launcher.log               launcher log (reference launcher)
    launcher.log.1             previous launcher log
    plugins/<id>/              one folder per plugin
      plugin.ini
      disabled                 optional marker (fixed)
      <entry file, scripts, content>
    storage/<id>.db            one key-value database per plugin (fixed)
    plugin-startup.json        optional historical load record (fixed, host option)
```

- A plugin folder is a subfolder of `data/plugins/` that holds a `plugin.ini`. Its id is the folder name.
- Ids are 1 to 31 characters of `a-z`, `0-9` and `_`. The ids `sco`, `host`, `menu` and `game` are reserved.
- **Disabled marker**: a file or a folder named `disabled` inside the plugin folder keeps the plugin from loading at the next start. Enable removes it, disable creates an empty file. The launcher never edits `plugin.ini` to switch a plugin.
- Folders whose name starts with `.` and symlinked folders are skipped by the reference launcher, as the host skips symlinks. The reference launcher uses `.install-<id>` and `.replaced-<id>` for staging and removes them before it returns.
- `plugin.ini` is at most 16 KiB. Keys come before the first section; `[settings]` is the only defined section. See `docs/plugins.md` in the repository.
- `plugin-startup.json` is written only when the host option is set. It is a bounded historical record, not live status. See `docs/plugin-startup-snapshot.md` in the repository.

## Settings keys

### The launcher's settings file

`key = value` lines. A line starting with `;` or `#` is a comment, a `[section]` line is skipped, the last value of a key wins, keys are matched without regard to ASCII case, and values may be wrapped in double quotes. A UTF-8 byte order mark is allowed. Boolean values are `1`, `on`, `yes`, `true` and `0`, `off`, `no`, `false`. Keys the launcher does not know are kept for the product. A bad line is a warning and changes nothing.

The reference launcher reads `plugins` (default on) and the six `exp_*` keys above. An unknown `exp_*` key is a warning. Editing a key keeps every other line, comment and line ending as it was.

### A plugin's `[settings]` values (fixed by the host kit)

A plugin declares settings in `plugin.ini`:

```ini
[settings]
fly_speed = float 10 1..100
```

The host stores a changed value in `data/storage/<id>.db`, table `sco_kv`:

| Part | Rule |
|---|---|
| Key | `sco.settings.<name>` |
| Value | `<type>:<canonical text>`, stored as a blob |
| `<type>` | `bool`, `int`, `float`, `string` or `enum` |
| `bool` | `true` or `false` |
| `int` | optional sign, 1 to 18 digits; bounds checked |
| `float` | shortest text that round-trips (`%.17g` form); bounds checked |
| `enum` | one of the declared choices |

A launcher that shows settings reads the declaration from `plugin.ini` and the kept value from the database. A value that is not kept shows the declared default. The launcher writes a value only after checking it against the declaration with these rules.

## Stop and ready events (product choice)

The host kit has no named stop event. Its own stop path is `sco::app::Stop` on the game thread. A product that wants its launcher to ask the mod to run exit work, and to wait for the answer, defines named events. The reference launcher defines the names and does not open them, because it starts no game.

| Event | Name | Direction |
|---|---|---|
| Stop | `Local\<slug>-stop-<game pid>` | Launcher to mod: run the exit work now. Manual reset, stays signalled |
| Stopped | `Local\<slug>-stopped-<game pid>` | Mod to launcher: the exit work has run, or there is nothing left to run |
| Ready | `Local\<slug>-ready-<launcher pid>` | Launcher to its own helper process: the helper's work is done |

- The mod creates stop and stopped in the session-local namespace with the default security descriptor, scoped to its own process id. The launcher opens them by the game's process id. The stop event exists only in a process where the host is loaded.
- The name buffer of a native host is 64 wide characters including the terminator, so a name is at most 63 characters. With `slug = sco-reference` and a 10-digit pid the longest name is 36 characters. Check `slug` length when you choose it.
- The launcher must treat "event not found" as "nothing to stop".

## Package and compatibility rules

A plugin package is one zip with **the plugin folder at the root**:

```text
pilot/plugin.ini
pilot/pilot.dll
```

Do not wrap it in `data/plugins/`. The reference launcher installs by verifying first and extracting second; it never extracts a member the verifier did not report.

Verifier limits (the SDK `tools/sco-plugin-verify.py` and `scl-plugins` apply the same rules):

| Limit | Value |
|---|---|
| Archive size | 256 MiB |
| Central directory | 8 MiB |
| Entries | 4096 |
| One file | 128 MiB |
| Total uncompressed | 512 MiB |
| Compression ratio | 200 |
| Path | 1024 bytes, 255 per segment |
| `plugin.ini` | 16 KiB |

Refused: ZIP64, multi-disk archives, symlinks and special entries, encrypted members, unsafe names (absolute or traversing), duplicate and case-colliding or Unicode-colliding names, more than one top-level folder, a folder name that differs from `id`, a missing declared `entry`, size or CRC mismatches, and an archive that changed while it was read.

Rules the launcher applies on top of the verifier:

- The id is legal (see above) and not reserved.
- **Plugin API**: the package's `api = M.m` must have `M` equal to the host's major and `m` not newer than the host's minor. The reference launcher was written against `sco_api.h` 1.1 (`scl_contract::plugin_api`). The `hello` example declares 1.0 and `rust_hello` declares 1.1.
- An id that is already installed is not replaced unless the player asks (`--replace`). A replaced plugin keeps its `disabled` choice and its storage database.
- `requires` and load order are the host's job at start. The launcher does not resolve them.

What a passing verification means: archive structure and manifest are consistent and the hashes are recorded. It does not authenticate the publisher, inspect native code, or show that the plugin works with the host. Native plugins are code that runs in the game process.

## Golden vectors

The reference launcher's tests are the vectors:

| Test | Pins |
|---|---|
| `scl-contract` unit tests | Event name format and length, variable names, switch table and defaults, settings key format |
| `scl-config` unit tests | Ini rules, the default environment block text, removal of an inherited switch |
| `scl-plugins` tests and `tests/zip-corpus/cases` | Three packages that verify (native, Lua, data) and one hostile package that is refused |
| `scl-install` unit tests | Install, list, disable, enable, replace, no leftovers after a refused package |
| `scl-cli` tests | Every command, exit codes, the built binary, and that `status` writes nothing |
| `scl-mcp` tests | The four tools, redaction of the log, that the game log cannot be requested, and that the server writes nothing |
| `scl-catalogue` tests and `tests/catalogue/golden` | Signed index format with a test-only key (`sco-plugin-index-v1`) |
