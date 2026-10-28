# IP policy

How sco-core keeps track of where its code comes from and what it may distribute. It is an
engineering policy, not legal advice; the maintainer decides release readiness.

## Licence

- sco-core's own code is licensed under the **GNU General Public License, version 3 only**
  (`GPL-3.0-only`, [LICENSE](LICENSE)). "Only" is deliberate: the maintainer, as the copyright
  holder, keeps the decision about any later GPL version.
- Four interface headers are **MIT** by a per-file grant (`SPDX-License-Identifier: MIT` in the
  file): `include/sc_ipc.h`, `include/sc_net.h`, `include/sc_titanlink.h`,
  `include/sc_voxel_bridge.h`. See [CONTRIBUTING.md § License](CONTRIBUTING.md#license).
- Vendored third-party code keeps its own licence and notices: [THIRD_PARTY_NOTICES.md](THIRD_PARTY_NOTICES.md).
- Plugins: no linking exception is granted. The project expects plugins that include sco-core's
  headers or compile its SDK sources to be distributed under GPL-3.0-compatible terms; whether the
  GPL applies to a particular plugin depends on how it is built and on applicable law. This is option A
  of [docs/legal/plugin-licensing-options.md](docs/legal/plugin-licensing-options.md), chosen by the
  maintainer on 2026-10-11 ([decision](docs/legal/release-decisions/2026-10-11-plugin-licence.md)).

## Copyright holder

The maintainer (GitHub `scubamount`) holds the copyright in sco-core's own code. The commit identity
`yerts` in the history is the same person, by the maintainer's statement recorded in
[docs/legal/provenance.json](docs/legal/provenance.json). Git author names alone are never taken
as proof of authorship.

## Where code may come from

| Origin | Allowed | Record |
|---|---|---|
| Written for sco-core | Yes | `project-authored` in `sdk/components.json` and `docs/legal/provenance.json` |
| Third-party open source with a licence compatible with GPL-3.0 | Yes, after review | Upstream URL, version, hashes and licence in [docs/legal/third-party-inventory.md](docs/legal/third-party-inventory.md); notices in THIRD_PARTY_NOTICES.md; every local change marked in the code |
| Interoperability facts about the game: byte patterns, the names of strings and functions the game references, structure offsets, record and field names | Yes, as facts needed to work with the game, without copying its code | `interoperability-derived`; how it was found is described in the PR (`docs/adding-signatures.md`) |
| Pieces of the game: `StarCitizen.exe` bytes beyond a search pattern, asset or data dumps, extracted `Game2.dcb` content, memory dumps | **No** | Refused in review and by the artifact checks |
| Code from another project without a known licence, or copied from a decompiler's output | **No** | Refused |
| Code an employer, client or other party may own | Only with written permission | Recorded in `docs/legal/provenance.json` before merge |

## Contributions

Every pull request answers, in its description (the PR template asks):

1. **Source.** Did you write this yourself? If any part comes from elsewhere, where, and under what licence?
2. **Rights.** Do you have the right to contribute it under GPL-3.0-only (MIT for the four interface headers)? Does an employer or anyone else have a claim on it?
3. **Game material.** Does it add anything taken from the game? Only interoperability facts (patterns, names) are allowed; say how you found them.

A maintainer doesn't merge a PR whose answers are missing or unclear.

## Unresolved or disputed material

1. Open an issue labelled `provenance` with the file, the question and what is known.
2. Mark the file `unresolved` in `docs/legal/provenance.json`. A release that would ship it is blocked
   (release hardening Gate B) until it is resolved, rewritten or removed.
3. A takedown request or rights claim goes to the maintainer at once; the affected file is withheld
   from the next release while it is assessed. Git history is never rewritten to hide where something
   came from.

## Records

- `sdk/components.json`: every SDK file's origin, licence and release profile.
- `docs/legal/provenance.json`: origin and review status by path, for the whole repository. Review is by path scope; individual files are re-reviewed when a PR changes them.
- `docs/legal/third-party-inventory.md`: vendored code with versions and hashes.
- `docs/legal/release-hardening-baseline.md`: the starting point of this work.
- `REUSE.toml` and `LICENSES/`: the same record in the [REUSE](https://reuse.software/) format, generated from `provenance.json` (`tools/ip_audit.py reuse-toml`).

## Checks

`tools/ip_audit.py` checks these records against the repository and every SDK zip (coverage, unresolved
entries, refused binaries, vendored-file hashes, licences and notices, core isolation, SPDX SBOMs);
`.github/workflows/ip-compliance.yml` runs it with `reuse lint` and the negative tests
(`tools/test_ip_audit.py`) on every pull request. A release also needs a person's approval record:
[docs/legal/release-checklist.md](docs/legal/release-checklist.md). Passing checks are evidence for
that decision, not a substitute for it.
