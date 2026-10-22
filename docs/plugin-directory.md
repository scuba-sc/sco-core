# Community plugin directory

Plugins live in their authors' own repositories. This directory lists them with the
compatibility information their authors reported, so you can see which SDK release a
plugin was built and checked against before you try it. The source of truth is
[`plugin-directory.json`](plugin-directory.json); the tables below are generated from it
by [`tools/plugin_directory.py`](../tools/plugin_directory.py).

## What a listing means

A listing is **compatibility information, not an endorsement**. It records what the author
reported and what the listing rules below require. It does not mean:

- **Verified in game.** Nothing in this directory is verified in game. The *In game* column
  only ever says "not run in game" or "not verified in game"; there is no verified value.
- **Reviewed or safe.** sco-core does not build, sign, host or distribute listed plugins.
  Native plugins are trusted in-process code with the game's full rights
  ([native trust](../sdk/docs/plugin-rules.md#native-trust-and-supported-usage)). Read the
  source before you run one.
- **Compatible with every SDK.** *SDK tested* is the published SDK release the plugin was
  built and checked against. Compare it with the SDK and host you use; [Plugin API v1](api-v1.md)
  explains what stays compatible across minor versions.

The *sco-plugin-check* column is the last run of the SDK's checker as reported: **full** is
the default run against the checker's stand-in host, **short** is the same run with
`--short-tables` (a 1.0-extent host). The checker never starts the game; a pass is development
evidence, not gameplay validation. *Game pack* is the Star Citizen game-pack version of that SDK
release when the plugin uses `game.*` services, or "none". *Offline* is the author's confirmation
that the plugin follows the offline-only [plugin rules](../sdk/docs/plugin-rules.md).

## Listings

<!-- plugin-directory:start -->
### First-party

| Plugin | Description | Plugin ids | Author | Licence | Languages | SDK tested | Game pack | sco-plugin-check | Offline | In game | Listed |
|---|---|---|---|---|---|---|---|---|---|---|---|
| [sc-offline plugin suite](https://github.com/scuba-sc/sc-offline) | sc-offline's plugins and shipped data packs: build placement, creative, crew, inspector, loadout, mining, NPCs, quantum, spawn, teleport, picker catalogs and optional local bridges. First-party product plugins, built in-tree with the host from a pinned sco-core commit (SDK baseline 2.3.0), not from a published SDK archive. Some ship disabled (Discord presence is blocked pending review); per-feature in-game status is on the repository's own status page. | `build`, `contracts`, `creative`, `crew`, `discord`, `inspector`, `inspector_lua`, `loadout`, `loadout_lists`, `mining`, `npc`, `picker_catalog`, `quantum`, `quantum_drive`, `spawn`, `teleport`, `titanlink`, `voxel_bridge` | scubamount | `GPL-3.0-only` | C++, Lua, data pack | 2.3.0 | 0.6.0 | not recorded | confirmed | not verified in game | 2026-10-21 |
| [sc-mp private multiplayer co-presence](https://github.com/scuba-sc/sc-mp) | Private co-presence for players running sc-offline, over sco.net: each game shows remote players as ghost entities it spawns itself. First-party; the repository's CI runs sco-plugin-check on the plugin, but no dated result is recorded here. | `multiplayer` | scubamount | `GPL-3.0-only` | C++ | 2.4.0 | 0.8.0 | not recorded | confirmed | not run in game | 2026-10-21 |

### Community

No community plugins are listed yet. [Request a listing](#request-a-listing).
<!-- plugin-directory:end -->

First-party entries are listed because they exist publicly today. They follow the same fields,
and their checker column shows only what is recorded here ("not recorded" where no dated run is
recorded); each repository keeps its own status page. They are not held to listing rule 1: the
sc-offline plugins are built in-tree with the host from a pinned sco-core commit, so their
*SDK tested* value is that commit's SDK baseline, not a published archive (the entry's note says
so).

## Listing rules

A community plugin is listed when all of these hold:

1. **Built from a published SDK archive.** Use a release archive from the
   [SDK releases](https://github.com/scuba-sc/sco-core/releases), checksum-verified as in
   [Start your first plugin](getting-started.md), not an integration branch or a source checkout.
   *SDK tested* names that release.
2. **Passes `sco-plugin-check` in full mode** on the built plugin folder, with the checker from
   that same archive ([how to run it](../sdk/README.md#check-a-plugin)). For a native plugin, report
   the short-mode (`--short-tables`) result too; a failure there is shown, not hidden. Short mode
   applies to native plugins only and is recorded as "not recorded" for Lua plugins and data packs.
   For a Lua plugin the checker only reads `plugin.ini` and checks that the entry script exists, and
   for a data pack it only lists the files, so a Lua plugin also reports a run of the archive's
   `tools/lua-check.lua` (and a `luac -p` syntax check) in the request.
3. **Follows the [plugin rules](../sdk/docs/plugin-rules.md), including offline-only.** No
   connection to Cloud Imperium Games' servers or online services, no anti-cheat bypass, no
   account or entitlement tampering, no telemetry, no personal data, and no network or shared
   memory outside `sco.net` and the IPC service.
4. **Source in a public repository**, at an `https` URL anyone can read without an account.
5. **States its licence** in the repository, given in the listing as the SPDX identifier the
   author uses (for example `GPL-3.0-only` or `MIT`). sco-core is GPL-3.0-only, and the
   [plugin rules](../sdk/docs/plugin-rules.md#license) say the project expects plugins that include
   its headers or compile its SDK sources to be distributed under GPL-3.0-compatible terms; this is
   not legal advice. The validator only checks that an SPDX licence is stated; whether a licence
   fits is the maintainer's decision when reviewing the request.
6. **Ships no game files**: no parts of `StarCitizen.exe`, no dumps, nothing extracted from
   `Data.p4k`. Entity and class names in text files are fine.
7. **Unique plugin ids.** Each `plugin.ini` id appears once in the whole directory and is not a
   reserved id (`sco`, `host`, `menu`, `game`; see [plugins](plugins.md#the-folder)).

## Request a listing

Open a [plugin listing request](https://github.com/scuba-sc/sco-core/issues/new?template=plugin-listing.yml).
The form ([source](../.github/ISSUE_TEMPLATE/plugin-listing.yml)) asks for exactly the fields of
the index plus the checker output you ran:

- name, plugin ids, repository URL, author, licence (SPDX id), languages;
- the SDK release you built with and, when you use `game.*` services, its game-pack version;
- the full-mode and short-mode `sco-plugin-check` results (short mode is "not applicable" for Lua
  plugins and data packs), the date you ran them and the output, plus the `lua-check.lua` output
  for a Lua plugin;
- a short description, the in-game status (not run, or not verified) and the offline-only
  confirmation.

A maintainer reads the repository, may ask you to re-run the checker, and adds the entry to
`plugin-directory.json` in a pull request, running `python3 tools/plugin_directory.py --write`
and `--check`. The maintainer sets the *Listed* date and may add a one-line caveat (`note`); a
short mode that was not run is recorded as "not recorded". Listing is a maintainer decision; a
request is not a listing.

## Updating or removing an entry

- **Update** with a new listing request for the same plugin ids, for example after you rebuild
  against a newer SDK release. The entry's *Listed* date becomes the date of the update.
- **Older entries stay as reported.** A new SDK release does not change any entry; *SDK tested*
  keeps naming the release the author used.
- **Removal** happens on the author's request, or when the repository is gone or no longer
  public, the licence is withdrawn, the plugin breaks a listing rule (online play, anti-cheat,
  game files) or a reported checker failure is not fixed. Removals go through a pull request that
  says why. Report a security problem in a listed plugin as described in
  [SECURITY.md](../SECURITY.md).

## Checking the index

```sh
python3 tools/plugin_directory.py --check   # validate the index; fail if the tables drifted
python3 tools/plugin_directory.py --write   # validate the index; rewrite the tables
python3 tools/test_plugin_directory.py
```

`--check` validates the schema (documented in the file's own `fields` object), `https`
repository URLs, unique plugin ids and names, no reserved id (`sco`, `host`, `menu`, `game`),
plain one-line text cells (no `|`, `<`, `>`, backtick, `[` or `]`, so no HTML, links or code spans
reach the page), a stated SPDX licence, the language set, SDK and
game-pack versions no newer than this checkout, checker results and dates (real, UTC, not in the
future), the offline confirmation and the in-game field. CI runs it next to the game status
registry check.
