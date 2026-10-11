# Plugin licensing: options for decision

**Status: option A chosen by the maintainer on 2026-10-11, pending counsel's review**
([decision](release-decisions/2026-10-11-plugin-licence.md)). The statements now use option A's wording. This document lays out the options;
it is not legal advice, and none of its descriptions of how licences apply has been reviewed by a
lawyer.

## The question

Four documents say plugins built against `sco_api.h` are GPL-3.0, with no linking exception
(`README.md`, `docs/api-v1.md`, `sdk/README.md`, `sdk/docs/plugin-rules.md`). The release-hardening
plan (finding F-13) asks whether that statement is accurate for every kind of plugin, and whether it
is the policy the project wants.

How the GPL reaches a plugin is not settled by the statement itself: it depends on the plugin's
kind and on how copyright law treats it, which differs between jurisdictions.

| Plugin kind | What it uses from sco-core | Consideration |
|---|---|---|
| Native C | Includes `sco_api.h` (struct and function-pointer declarations, constants); is loaded as a separate DLL; calls through a table the host passes at run time | Whether including an interface header and being loaded into a GPL host makes the plugin a derivative work is the classic open question |
| Native C++ | Also includes `include/scosdk/` (header-only; template and inline code is compiled into the plugin) | More sco-core code is compiled into the plugin than with C |
| C# NativeAOT | Compiles `sdk/csharp/Sco.Sdk` (sco-core source) into the plugin | sco-core source becomes part of the plugin binary |
| Lua | A script run by sco-lua inside the host; uses no sco-core source | The script is data to the host; the GPL's reach to interpreted programs is commonly read as narrow |
| Data pack | Files only, no code | Not a program |

## Option A: keep GPL-3.0-only, state it accurately

- sco-core stays `GPL-3.0-only` throughout, plus the four MIT interface headers.
- The four statements are reworded to describe what the project expects rather than assert a legal
  result, for example: *"sco-core is GPL-3.0-only. The project expects plugins that include its
  headers or compile its SDK sources to be distributed under GPL-3.0-compatible terms. Whether the
  GPL applies to a particular plugin depends on how it is built and on applicable law."*
- **For:** no relicensing; copyleft keeps plugin source available, which suits a modding community;
  matches sc-offline (GPL-3.0, from ChrisWareOffline).
- **Against:** uncertainty stays for plugin authors, especially closed-source ones; some authors may
  stay away.
- **Needs:** only a wording change, which the copyright holder can make.

## Option B: GPL-3.0-only plus a plugin exception

- sco-core stays `GPL-3.0-only`. A written **additional permission** under GPL-3.0 section 7 is
  added: a plugin that interacts with sco-core only through the public SDK (the files `sdk/components.json`
  marks as public: `sco_api.h`, the service headers, `include/scosdk/`, `sdk/csharp/Sco.Sdk`) may be
  distributed under terms of the author's choice. The host, the loader, the runtime and the game pack
  stay GPL, and changes to them stay GPL.
- SPDX for the covered files would be `GPL-3.0-only WITH <exception>`; a custom exception needs its
  own text in the repository (an existing one such as `GCC-exception-3.1` could serve as a model,
  but its wording is specific to GCC and would need adapting).
- **For:** clear terms for plugin authors, including closed-source ones; the SDK becomes usable
  more widely.
- **Against:** closed-source plugins become possible; a custom exception needs careful drafting;
  sc-offline, which is GPL-3.0 and carries ChrisWareOffline code, is not affected, but its own
  built-in plugins would need checking against the exception's scope.
- **Needs:** the copyright holder's decision (as the sole holder of sco-core's own code, the
  maintainer can add a permission), a reviewed exception text, and the third-party code checked to
  be compatible (Lua, SQLite, toml++ and ImGui are MIT or public domain, so they don't restrict it).

## What changes under each

| | Option A | Option B |
|---|---|---|
| `LICENSE` | Unchanged | Unchanged; exception text added beside it |
| SPDX on SDK files | `GPL-3.0-only` | `GPL-3.0-only WITH <exception>` |
| The four statements (F-13) | Reworded as above | Replaced by a description of the exception |
| `sdk/components.json` licence field | `GPL-3.0-only` (as now) | The SPDX expression with the exception |
| sc-offline | No change | No change to its licence; check its built-ins against the exception |

## Recommendation for the decision

The engineering work doesn't depend on the choice. Whichever option is chosen, the decision should
be recorded under `docs/legal/release-decisions/` with the date and who made it, before an SDK
release states a plugin licence.
