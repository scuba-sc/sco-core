# Plugin licence: option A (F-13)

Decision: keep sco-core GPL-3.0-only with no plugin or linking exception, and replace the categorical "plugins are GPL-3.0" statements with an accurate description of what the project expects
Decided by: scubamount (maintainer, copyright holder of sco-core's own code)
Date: 2026-10-11
Counsel review: pending. The maintainer chose option A as the preferred one to put to counsel; until counsel has reviewed it, a release states the wording below and nothing stronger.
Options: [../plugin-licensing-options.md](../plugin-licensing-options.md) (option B, a GPL section 7 plugin exception, not taken)

## What changed

The statement in `README.md`, `docs/api-v1.md`, `sdk/README.md`, `sdk/docs/plugin-rules.md` and the header comment of
`include/sco_api.h` is now, in substance:

> sco-core is GPL-3.0-only. The project expects plugins that include its headers or compile its SDK
> sources to be distributed under GPL-3.0-compatible terms. Whether the GPL applies to a particular
> plugin depends on how it is built and on applicable law. No linking exception is granted.

Before, it said plugins built against `sco_api.h` *are* GPL-3.0. That was a legal conclusion the
project can't make for every kind of plugin (C, header-only C++, C# NativeAOT, Lua, data packs).

## What didn't change

- `LICENSE`, the GPL-3.0-only grant, and the four MIT interface headers.
- The ABI: `sco_api.h`'s change is in its comment only, and `tests/abi_v1.c` pins the same layout.
- The `GPL-3.0-only` licence field of every SDK component, and the SPDX/REUSE records.
- The per-file "GPL-3.0, like sco-core" notes, which describe each file's own licence, not a plugin's.

## Open

Counsel's review of the wording. Until that review, a release should not describe the plugin
licence in stronger terms than the wording above.
